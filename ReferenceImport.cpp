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
	if (data.bytes.empty()) return false;
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
	WICRect rc{ static_cast<INT>(x), static_cast<INT>(y), 1, 1 };
	BYTE px[4] = { 0, 0, 0, 0 };
	if (FAILED(conv->CopyPixels(&rc, 4, 4, px))) return false;
	out = Color(px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, 1.0f);
	return true;
}

bool SampleReferenceColor(Document& doc, Vec2 worldPt, Color& out)
{
	LayerNode* layer = FindReferenceLayer(doc);
	if (!layer) return false;
	const auto& kids = layer->GetChildren();
	for (auto it = kids.rbegin(); it != kids.rend(); ++it) {   // 위에 있는 것부터
		auto* im = dynamic_cast<ImageNode*>(it->get());
		if (!im || !im->GetData() || im->GetData()->pixelW <= 0 || im->GetData()->pixelH <= 0) continue;
		Matrix2D inv = im->GetWorldTransform();
		if (!inv.Invert()) continue;
		const Vec2 local = inv.TransformPoint(worldPt);
		if (local.x < 0.0f || local.y < 0.0f || local.x > im->GetWidth() || local.y > im->GetHeight()) continue;
		const ImageData& d = *im->GetData();
		const unsigned px = (std::min)(static_cast<unsigned>(d.pixelW) - 1, static_cast<unsigned>(local.x / im->GetWidth() * d.pixelW));
		const unsigned py = (std::min)(static_cast<unsigned>(d.pixelH) - 1, static_cast<unsigned>(local.y / im->GetHeight() * d.pixelH));
		return DecodeImagePixel(d, px, py, out);
	}
	return false;
}

} // namespace ge
