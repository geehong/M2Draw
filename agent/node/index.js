#!/usr/bin/env node
// M2Draw 에이전트 연결 서버 (Node.js 버전). C# 버전과 같은 도구, 같은 작업 폴더 규칙을 쓴다.
// 작업 폴더: 환경변수 M2DRAW_WORKSPACE, 없으면 %USERPROFILE%\M2Draw_Agent
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const root = process.env.M2DRAW_WORKSPACE || path.join(os.homedir(), "M2Draw_Agent");

// 패키지 이름은 작업 폴더 안의 한 단계 이름만 허용 (경로 이동 방지)
function packageDir(name) {
  if (!name || path.basename(name) !== name || name.includes("..")) {
    throw new Error("패키지 이름이 올바르지 않습니다: " + name);
  }
  return path.join(root, name);
}

const text = (s) => ({ content: [{ type: "text", text: s }] });

const server = new McpServer({ name: "M2DrawMcp", version: "1.0.0" });

server.tool("m2draw_list_packages", "M2Draw 가 내보낸 AI 에이전트 패키지 목록을 보여 준다 (scene.json 이 있는 폴더)", {}, async () => {
  if (!fs.existsSync(root)) return text("작업 폴더가 없습니다: " + root);
  const names = fs.readdirSync(root, { withFileTypes: true })
    .filter((d) => d.isDirectory() && fs.existsSync(path.join(root, d.name, "scene.json")))
    .map((d) => d.name);
  return text(names.length ? names.join("\n") : "패키지가 없습니다. 앱에서 파일 > 내보내기 > AI 에이전트 패키지를 먼저 실행하세요.");
});

server.tool("m2draw_get_guide", "이 앱의 안내(작업 흐름, 컨트롤, 결과 SVG 규칙)를 읽는다. 처음에 한 번 읽으세요", {}, async () => {
  const p = path.join(root, "AGENT_GUIDE.md");
  return text(fs.existsSync(p) ? fs.readFileSync(p, "utf8") : "AGENT_GUIDE.md 가 없습니다. 앱에서 파일 > 에이전트 작업 시작을 먼저 실행하세요.");
});

server.tool("m2draw_get_scene", "패키지의 구조 데이터를 읽는다. 사용자가 고친 최신 상태(current)가 있으면 그것을 읽는다", { package: z.string().describe("패키지 이름") }, async ({ package: pkg }) => {
  const cur = path.join(packageDir(pkg), "current", "scene.json");
  if (fs.existsSync(cur)) return text(fs.readFileSync(cur, "utf8"));
  const p = path.join(packageDir(pkg), "scene.json");
  return text(fs.existsSync(p) ? fs.readFileSync(p, "utf8") : "scene.json 이 없습니다: " + pkg);
});

server.tool("m2draw_get_prompt", "패키지의 지시문(prompt.md)을 읽는다", { package: z.string().describe("패키지 이름") }, async ({ package: pkg }) => {
  const p = path.join(packageDir(pkg), "prompt.md");
  return text(fs.existsSync(p) ? fs.readFileSync(p, "utf8") : "prompt.md 가 없습니다: " + pkg);
});

server.tool("m2draw_get_image_paths", "패키지의 그림 파일 경로(preview.png: 결과 모습, reference.png: 원본 디자인)를 알려 준다. 파일을 직접 열어 볼 수 있다", { package: z.string().describe("패키지 이름") }, async ({ package: pkg }) => {
  const dir = packageDir(pkg);
  const lines = ["preview.png", "reference.png"].map((n) => {
    const p = path.join(dir, n);
    return fs.existsSync(p) ? `${n}: ${p}` : `${n}: 없음`;
  });
  return text(lines.join("\n"));
});

server.tool("m2draw_put_result", "만든 결과 SVG 를 패키지의 result 폴더에 저장한다. 앱에서 파일 > AI 산출물 가져오기로 불러올 수 있다",
  {
    package: z.string().describe("패키지 이름"),
    name: z.string().regex(/^[A-Za-z0-9_-]{1,60}$/).describe("파일 이름 (영문, 숫자, - _ 만)"),
    svg: z.string().describe("SVG 내용 전체"),
  },
  async ({ package: pkg, name, svg }) => {
    const t = svg.trimStart();
    if (!(t.startsWith("<svg") || t.startsWith("<?xml"))) throw new Error("SVG 내용이 아닙니다 (<svg 로 시작해야 함).");
    const dir = path.join(packageDir(pkg), "result");
    fs.mkdirSync(dir, { recursive: true });
    const p = path.join(dir, name + ".svg");
    fs.writeFileSync(p, svg, "utf8");
    return text("저장했습니다: " + p);
  });

await server.connect(new StdioServerTransport());