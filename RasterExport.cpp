#include "pch.h"
#include "RasterExport.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace ge {

static const unsigned kMaxSide = 16384;

static Rect2 ResolveRegion(const Document& doc, const Rect2* region)
{
	return region ? *region : Rect2{ 0.0f, 0.0f, doc.width, doc.height };
}

bool RasterExportPixelSize(const Document& doc, const RasterExportOptions& opt, const Rect2* region, unsigned& w, unsigned& h)
{
	const Rect2 rg = ResolveRegion(doc, region);
	const float scale = (std::max)(0.05f, (std::min)(opt.scale, 32.0f));
	const double pw = std::ceil(static_cast<double>(rg.right - rg.left) * scale);
	const double ph = std::ceil(static_cast<double>(rg.bottom - rg.top) * scale);
	w = h = 0;
	if (pw < 1.0 || ph < 1.0 || pw > kMaxSide || ph > kMaxSide) return false;
	w = static_cast<unsigned>(pw);
	h = static_cast<unsigned>(ph);
	return true;
}

bool ExportRaster(Document& doc, const std::wstring& path, const RasterExportOptions& opt, const Rect2* region, std::wstring* err)
{
	auto fail = [&](const wchar_t* m) { if (err) *err = m; return false; };

	unsigned W = 0, H = 0;
	if (!RasterExportPixelSize(doc, opt, region, W, H)) return fail(L"출력 크기가 올바르지 않거나 너무 큽니다 (한 변 최대 16384px).");
	const Rect2 rg = ResolveRegion(doc, region);
	const float scale = (std::max)(0.05f, (std::min)(opt.scale, 32.0f));

	ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return fail(L"WIC 를 초기화하지 못했습니다.");

	ComPtr<ID2D1Factory> d2d;
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return fail(L"Direct2D 를 초기화하지 못했습니다.");

	ComPtr<IWICBitmap> bmp;
	if (FAILED(wic->CreateBitmap(W, H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return fail(L"출력 비트맵을 만들지 못했습니다 (메모리 부족일 수 있음).");

	ComPtr<ID2D1RenderTarget> rt;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
	if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return fail(L"오프스크린 렌더 타깃을 만들지 못했습니다.");

	{
		D2DRenderer renderer;
		if (!renderer.Initialize(rt.Get(), d2d.Get())) return fail(L"렌더러를 초기화하지 못했습니다.");
		Viewport vp;
		vp.zoom = scale;
		vp.pan = Vec2{ -rg.left * scale, -rg.top * scale };

		const bool opaque = opt.jpeg || !opt.transparent;
		rt->BeginDraw();
		if (opaque) {
			const Color& c = doc.backgroundColor;
			// JPG/불투명 PNG 는 문서 배경색(알파가 낮으면 흰색 위에 합성)으로 채움
			const float a = c.a;
			rt->Clear(D2D1::ColorF(c.r * a + (1.0f - a), c.g * a + (1.0f - a), c.b * a + (1.0f - a), 1.0f));
		} else {
			rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
		}
		renderer.DrawDocument(doc, vp);
		const HRESULT hr = rt->EndDraw();
		renderer.Shutdown();
		if (FAILED(hr)) return fail(L"렌더링 중 오류가 발생했습니다.");
	}
	rt.Reset();

	// 인코딩
	ComPtr<IWICStream> stream;
	if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return fail(L"파일을 열 수 없습니다 (경로/권한 확인).");
	ComPtr<IWICBitmapEncoder> enc;
	if (FAILED(wic->CreateEncoder(opt.jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &enc))) return fail(L"인코더를 만들지 못했습니다.");
	if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return fail(L"인코더를 초기화하지 못했습니다.");

	ComPtr<IWICBitmapFrameEncode> frame;
	ComPtr<IPropertyBag2> bag;
	if (FAILED(enc->CreateNewFrame(&frame, &bag))) return fail(L"프레임을 만들지 못했습니다.");
	if (opt.jpeg && bag) {
		PROPBAG2 pb = {};
		pb.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_R4;
		v.fltVal = (std::max)(1, (std::min)(opt.jpegQuality, 100)) / 100.0f;
		bag->Write(1, &pb, &v);
	}
	if (FAILED(frame->Initialize(bag.Get()))) return fail(L"프레임을 초기화하지 못했습니다.");
	frame->SetSize(W, H);
	const double dpi = (std::max)(1.0f, (std::min)(opt.dpi, 2400.0f));
	frame->SetResolution(dpi, dpi);

	WICPixelFormatGUID fmt = opt.jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
	frame->SetPixelFormat(&fmt);
	ComPtr<IWICFormatConverter> conv;
	if (FAILED(wic->CreateFormatConverter(&conv)) ||
		FAILED(conv->Initialize(bmp.Get(), fmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		return fail(L"픽셀 형식을 변환하지 못했습니다.");
	if (FAILED(frame->WriteSource(conv.Get(), nullptr))) return fail(L"이미지를 기록하지 못했습니다.");
	if (FAILED(frame->Commit()) || FAILED(enc->Commit())) return fail(L"파일을 저장하지 못했습니다.");
	return true;
}

} // namespace ge
