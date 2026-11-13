#pragma once
// 프로세스 내 이미지 리소스 공유 레지스트리 (6.3): Undo/Redo 스냅샷 SVG 는 이미지 바이트 대신 m2mem://image/<id> 만 기록한다.
// 레지스트리는 약한 참조만 가진다 - 데이터는 문서 노드나 스냅샷(UndoManager 가 shared_ptr 보관)이 살려 둔다.
#include "D2DGraphicEngine.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace ge {

inline const char* const kImageMemScheme = "m2mem://image/";

inline std::unordered_map<uint64_t, std::weak_ptr<const ImageData>>& ImageRegistryMap()
{
	static std::unordered_map<uint64_t, std::weak_ptr<const ImageData>> m;
	return m;
}

// 데이터 주소를 ID 로 쓴다 (데이터가 살아 있는 동안 유일)
inline uint64_t RegisterImageResource(const std::shared_ptr<const ImageData>& d)
{
	auto& m = ImageRegistryMap();
	if (m.size() > 256) {   // 만료된 항목 정리
		for (auto it = m.begin(); it != m.end();) it = it->second.expired() ? m.erase(it) : std::next(it);
	}
	const uint64_t id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(d.get()));
	m[id] = d;
	return id;
}

inline std::shared_ptr<const ImageData> LookupImageResource(uint64_t id)
{
	auto& m = ImageRegistryMap();
	auto it = m.find(id);
	return it == m.end() ? nullptr : it->second.lock();
}

} // namespace ge
