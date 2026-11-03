#pragma once

#include <string>

namespace ge {

// 에이전트가 이 앱을 이해하도록 쓰는 설명서 (AGENT_GUIDE.md 내용, UTF-8)
std::string BuildAgentGuideUtf8();

// 작업 폴더 루트: 환경변수 M2DRAW_WORKSPACE, 없으면 %USERPROFILE%\M2Draw_Agent (MCP 서버와 같은 규칙)
std::wstring AgentWorkspaceRoot();

} // namespace ge
