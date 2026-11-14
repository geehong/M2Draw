#include "pch.h"
#include "ReferenceImport.h"
#include "D2DLayer.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace ge {

LayerNode* FindReferenceLayer(Document& doc)
{
	for (const auto& layer : doc.GetLayers()) {
		if (layer->GetName() == kReferenceLayerName)
			return layer.get();
	}
	return nullptr;
}

LayerNode* EnsureReferenceLayer(Document& doc)
{
	if (LayerNode* existing = FindReferenceLayer(doc))
		return existing;
	return doc.AddLayer(kReferenceLayerName, -1);   // -1 = 최상단
}

ImageNode* AddReferenceImage(Document& doc, std::shared_ptr<const ImageData> data, const std::wstring& name, float maxRatio, bool fitWidth)
{
	if (!data) return nullptr;
	LayerNode* layer = EnsureReferenceLayer(doc);
	if (!layer) return nullptr;

	// 원본 픽셀 크기를 알 수 없으면 기본 상자로 둔다
	float w = data->pixelW > 0 ? static_cast<float>(data->pixelW) : 100.0f;
	float h = data->pixelH > 0 ? static_cast<float>(data->pixelH) : 100.0f;
	const float ratio = (std::max)(0.01f, maxRatio);
	const float sc = fitWidth ? (std::min)(1.0f, doc.width * ratio / w)
	                          : (std::min)(1.0f, (std::min)(doc.width * ratio / w, doc.height * ratio / h));
	w *= sc;
	h *= sc;

	auto im = std::make_unique<ImageNode>();
	im->SetData(std::move(data));
	im->SetSize(w, h);
	im->SetName(name);
	im->SetTransform(Matrix2D::Translation((doc.width - w) * 0.5f, fitWidth ? 0.0f : (doc.height - h) * 0.5f));
	im->SetLocked(true);                       // 실수로 옮기거나 지우지 않도록 잠금
	im->SetOpacity(kReferenceOpacity);         // 아래 도형이 비쳐 보이도록 반투명

	// AddShape 는 활성 레이어에 넣으므로, 레퍼런스 레이어 최상단으로 옮긴다 (화면상 위치는 MoveNodeTo 가 보정)
	Node* added = doc.AddShape(std::move(im));
	if (!added) return nullptr;
	doc.MoveNodeTo(added, layer, -1);
	return static_cast<ImageNode*>(added);
}

using Microsoft::WRL::ComPtr;

bool DecodeImagePixel(const ImageData& data, unsigned x, unsigned y, Color& out)
{
	// 같은 이미지의 픽셀을 여러 번 읽을 때(마우스를 움직이는 동안의 색 미리보기) 전체를 한 번만 디코딩해 둔다
	static struct { const uint8_t* src = nullptr; size_t size = 0; unsigned w = 0, h = 0; std::vector<uint8_t> px; } cache;
	if (data.bytes.empty()) return false;
	if (cache.src != data.bytes.data() || cache.size != data.bytes.size()) {
		cache = {};
		ComPtr<IWICImagingFactory> factory;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
		ComPtr<IWICStream> stream;
		if (FAILED(factory->CreateStream(&stream))) return false;
		if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.bytes.data()), static_cast<DWORD>(data.bytes.size())))) return false;
		ComPtr<IWICBitmapDecoder> decoder;
		if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return false;
		ComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, &frame))) return false;
		ComPtr<IWICFormatConverter> conv;
		if (FAILED(factory->CreateFormatConverter(&conv))) return false;
		if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
		UINT w = 0, h = 0;
		if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) return false;
		std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
		if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data()))) return false;
		cache.src = data.bytes.data();
		cache.size = data.bytes.size();
		cache.w = w;
		cache.h = h;
		cache.px = std::move(px);
	}
	if (x >= cache.w || y >= cache.h) return false;
	const uint8_t* p = &cache.px[(static_cast<size_t>(y) * cache.w + x) * 4];
	out = Color(p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, 1.0f);
	return true;
}
bool SampleReferenceColor(Document& doc, Vec2 worldPt, Color& out)
{
	// 레퍼런스 레이어를 먼저 보고, 그다음 일반 레이어의 이미지도 위에서부터 찾는다 (사진을 그냥 가져온 경우도 스포이드로 색을 뽑을 수 있게)
	std::vector<LayerNode*> order;
	if (LayerNode* ref = FindReferenceLayer(doc)) order.push_back(ref);
	const auto& layers = doc.GetLayers();
	for (size_t i = layers.size(); i-- > 0;) {
		LayerNode* l = layers[i].get();
		if (l && std::find(order.begin(), order.end(), l) == order.end()) order.push_back(l);
	}
	for (LayerNode* layer : order) {
		if (!layer || !layer->IsVisible()) continue;
		const auto& kids = layer->GetChildren();
		for (auto it = kids.rbegin(); it != kids.rend(); ++it) {   // 위에 있는 것부터
			auto* im = dynamic_cast<ImageNode*>(it->get());
			if (!im || !im->IsVisible() || !im->GetData() || im->GetData()->pixelW <= 0 || im->GetData()->pixelH <= 0) continue;
			Matrix2D inv = im->GetWorldTransform();
			if (!inv.Invert()) continue;
			const Vec2 local = inv.TransformPoint(worldPt);
			if (local.x < 0.0f || local.y < 0.0f || local.x > im->GetWidth() || local.y > im->GetHeight()) continue;
			const ImageData& d = *im->GetData();
			const unsigned px = (std::min)(static_cast<unsigned>(d.pixelW) - 1, static_cast<unsigned>(local.x / im->GetWidth() * d.pixelW));
			const unsigned py = (std::min)(static_cast<unsigned>(d.pixelH) - 1, static_cast<unsigned>(local.y / im->GetHeight() * d.pixelH));
			return DecodeImagePixel(d, px, py, out);
		}
	}
	return false;
}

} // namespace ge
