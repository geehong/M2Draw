#include "pch.h"
#include "ClipboardImage.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// HBITMAP -> PNG 바이트 (WIC). 클립보드가 소유한 핸들이므로 복사해서 쓰고 해제하지 않는다
std::shared_ptr<ge::ImageData> BitmapToPng(HBITMAP hbm)
{
	ComPtr<IWICImagingFactory> factory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
		return nullptr;

	ComPtr<IWICBitmap> bitmap;
	if (FAILED(factory->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapUseAlpha, &bitmap)))
		return nullptr;

	UINT w = 0, h = 0;
	if (FAILED(bitmap->GetSize(&w, &h)) || w == 0 || h == 0)
		return nullptr;

	ComPtr<IStream> stream;
	if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
		return nullptr;

	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	ComPtr<IPropertyBag2> props;
	if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
	    FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
	    FAILED(encoder->CreateNewFrame(&frame, &props)) ||
	    FAILED(frame->Initialize(props.Get())) ||
	    FAILED(frame->SetSize(w, h)))
		return nullptr;

	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
	if (FAILED(frame->SetPixelFormat(&fmt)) ||
	    FAILED(frame->WriteSource(bitmap.Get(), nullptr)) ||
	    FAILED(frame->Commit()) ||
	    FAILED(encoder->Commit()))
		return nullptr;

	// 스트림에 기록된 실제 크기만큼 읽는다 (메모리 블록은 올림된 크기일 수 있음)
	STATSTG st{};
	if (FAILED(stream->Stat(&st, STATFLAG_NONAME)) || st.cbSize.QuadPart == 0)
		return nullptr;
	LARGE_INTEGER zero{};
	if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)))
		return nullptr;

	auto data = std::make_shared<ge::ImageData>();
	data->bytes.resize(static_cast<size_t>(st.cbSize.QuadPart));
	ULONG got = 0;
	if (FAILED(stream->Read(data->bytes.data(), static_cast<ULONG>(data->bytes.size()), &got)) ||
	    got != data->bytes.size())
		return nullptr;

	data->mime = "image/png";
	data->pixelW = static_cast<int>(w);
	data->pixelH = static_cast<int>(h);
	return data;
}

} // namespace

std::shared_ptr<ge::ImageData> ReadClipboardBitmapAsPng(CWnd* owner)
{
	if (!::IsClipboardFormatAvailable(CF_BITMAP) || !owner || !owner->OpenClipboard())
		return nullptr;

	std::shared_ptr<ge::ImageData> out;
	if (HBITMAP hbm = static_cast<HBITMAP>(::GetClipboardData(CF_BITMAP)))
		out = BitmapToPng(hbm);
	::CloseClipboard();
	return out;
}
