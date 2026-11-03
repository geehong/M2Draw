using System.ComponentModel;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.DependencyInjection;
using ModelContextProtocol.Server;

// M2Draw 에이전트 연결 서버 (C# 버전). MCP 표준(stdio)으로 동작하며, 작업 폴더의 파일로 앱과 주고받는다.
// 작업 폴더: 환경변수 M2DRAW_WORKSPACE, 없으면 %USERPROFILE%\M2Draw_Agent
var builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders();   // stdout 은 MCP 전용이므로 로그를 쓰지 않는다
builder.Services.AddMcpServer()
    .WithStdioServerTransport()
    .WithToolsFromAssembly();
await builder.Build().RunAsync();

[McpServerToolType]
public static class M2DrawTools
{
    static string Root
    {
        get
        {
            var env = Environment.GetEnvironmentVariable("M2DRAW_WORKSPACE");
            return string.IsNullOrWhiteSpace(env)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "M2Draw_Agent")
                : env;
        }
    }

    // 패키지 이름은 작업 폴더 안의 한 단계 이름만 허용 (경로 이동 방지)
    static string PackageDir(string package)
    {
        if (string.IsNullOrWhiteSpace(package) || Path.GetFileName(package) != package || package.Contains(".."))
            throw new ArgumentException("패키지 이름이 올바르지 않습니다: " + package);
        return Path.Combine(Root, package);
    }

    [McpServerTool(Name = "m2draw_list_packages"), Description("M2Draw 가 내보낸 AI 에이전트 패키지 목록을 보여 준다 (scene.json 이 있는 폴더)")]
    public static string ListPackages()
    {
        if (!Directory.Exists(Root)) return "작업 폴더가 없습니다: " + Root;
        var names = Directory.GetDirectories(Root)
            .Where(d => File.Exists(Path.Combine(d, "scene.json")))
            .Select(Path.GetFileName)
            .ToList();
        return names.Count == 0 ? "패키지가 없습니다. 앱에서 파일 > 내보내기 > AI 에이전트 패키지를 먼저 실행하세요." : string.Join("\n", names);
    }

    [McpServerTool(Name = "m2draw_get_guide"), Description("이 앱의 안내(작업 흐름, 컨트롤, 결과 SVG 규칙)를 읽는다. 처음에 한 번 읽으세요")]
    public static string GetGuide()
    {
        var path = Path.Combine(Root, "AGENT_GUIDE.md");
        return File.Exists(path) ? File.ReadAllText(path) : "AGENT_GUIDE.md 가 없습니다. 앱에서 파일 > 에이전트 작업 시작을 먼저 실행하세요.";
    }

    [McpServerTool(Name = "m2draw_get_scene"), Description("패키지의 구조 데이터를 읽는다. 사용자가 고친 최신 상태(current)가 있으면 그것을 읽는다")]
    public static string GetScene([Description("패키지 이름 (m2draw_list_packages 결과)")] string package)
    {
        var cur = Path.Combine(PackageDir(package), "current", "scene.json");
        if (File.Exists(cur)) return File.ReadAllText(cur);
        var path = Path.Combine(PackageDir(package), "scene.json");
        return File.Exists(path) ? File.ReadAllText(path) : "scene.json 이 없습니다: " + package;
    }

    [McpServerTool(Name = "m2draw_get_prompt"), Description("패키지의 지시문(prompt.md)을 읽는다")]
    public static string GetPrompt([Description("패키지 이름")] string package)
    {
        var path = Path.Combine(PackageDir(package), "prompt.md");
        return File.Exists(path) ? File.ReadAllText(path) : "prompt.md 가 없습니다: " + package;
    }

    [McpServerTool(Name = "m2draw_get_image_paths"), Description("패키지의 그림 파일 경로(preview.png: 결과 모습, reference.png: 원본 디자인)를 알려 준다. 파일을 직접 열어 볼 수 있다")]
    public static string GetImagePaths([Description("패키지 이름")] string package)
    {
        var dir = PackageDir(package);
        var lines = new List<string>();
        foreach (var name in new[] { "preview.png", "reference.png" })
        {
            var p = Path.Combine(dir, name);
            lines.Add(File.Exists(p) ? name + ": " + p : name + ": 없음");
        }
        return string.Join("\n", lines);
    }

    [McpServerTool(Name = "m2draw_put_result"), Description("만든 결과 SVG 를 패키지의 result 폴더에 저장한다. 앱에서 파일 > AI 산출물 가져오기로 불러올 수 있다")]
    public static string PutResult(
        [Description("패키지 이름")] string package,
        [Description("파일 이름 (영문, 숫자, - _ 만)")] string name,
        [Description("SVG 내용 전체")] string svg)
    {
        if (!Regex.IsMatch(name ?? "", @"^[A-Za-z0-9_\-]{1,60}$")) throw new ArgumentException("파일 이름은 영문, 숫자, - _ 만 60자 이내로 쓰세요.");
        if (string.IsNullOrWhiteSpace(svg) || !(svg.TrimStart().StartsWith("<svg") || svg.TrimStart().StartsWith("<?xml")))
            throw new ArgumentException("SVG 내용이 아닙니다 (<svg 로 시작해야 함).");
        var dir = Path.Combine(PackageDir(package), "result");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, name + ".svg");
        File.WriteAllText(path, svg, new System.Text.UTF8Encoding(false));
        return "저장했습니다: " + path;
    }
}