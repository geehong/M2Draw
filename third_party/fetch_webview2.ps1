# Microsoft.Web.WebView2 NuGet 패키지에서 헤더와 정적 로더 라이브러리만 third_party/WebView2 로 꺼낸다 (저장소에는 포함하지 않음).
# 사용: powershell -ExecutionPolicy Bypass -File third_party\fetch_webview2.ps1
$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'WebView2'
if (Test-Path (Join-Path $root 'x64\WebView2LoaderStatic.lib')) { 'already present: ' + $root; exit 0 }
$tmp = Join-Path $env:TEMP ('wv2_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
$pkg = Join-Path $tmp 'wv2.zip'
Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2' -OutFile $pkg
Expand-Archive -Path $pkg -DestinationPath (Join-Path $tmp 'x')
$n = Join-Path $tmp 'x\build\native'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'include'), (Join-Path $root 'x64'), (Join-Path $root 'x86') | Out-Null
Copy-Item (Join-Path $n 'include\*.h') (Join-Path $root 'include')
Copy-Item (Join-Path $n 'x64\WebView2LoaderStatic.lib') (Join-Path $root 'x64')
Copy-Item (Join-Path $n 'x86\WebView2LoaderStatic.lib') (Join-Path $root 'x86')
Remove-Item -Recurse -Force $tmp
'extracted to ' + $root
