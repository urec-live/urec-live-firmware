# Takes a headless-browser screenshot of the locally served viewer page.
# Usage: scripts\shot.ps1 -Out <png> [-Hash capture-iso] [-Width 1400] [-Height 1000] [-Dark]
# Needs scripts/serve-viewer.ts running (http://127.0.0.1:4173) and Microsoft Edge installed.
param(
  [string]$Out = "",
  [string]$Hash = "",
  [int]$Width = 1400,
  [int]$Height = 1000,
  [string]$Theme = "",          # light | dark: sets data-theme on the local page
  [switch]$DumpDom,             # print the final DOM instead of taking a screenshot
  [string]$Page = "_local.html"
)
$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edge)) { $edge = "C:\Program Files\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $edge)) { throw "Microsoft Edge not found" }

$profileDir = Join-Path $env:TEMP "beacon-viewer-shot-profile"
$url = "http://127.0.0.1:4173/$Page"
if ($Theme) { $url = "$url`?theme=$Theme" }
if ($Hash) { $url = "$url#$Hash" }
$browserArgs = @(
  "--headless=new", "--no-first-run", "--hide-scrollbars", "--mute-audio",
  "--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--ignore-gpu-blocklist",
  "--user-data-dir=$profileDir", "--window-size=$Width,$Height",
  "--virtual-time-budget=30000"
)
if ($DumpDom) {
  $domFile = Join-Path $env:TEMP "beacon-viewer-dom.html"
  $p = Start-Process -FilePath $edge -ArgumentList ($browserArgs + "--dump-dom" + $url) -PassThru -WindowStyle Hidden -RedirectStandardOutput $domFile
  if (-not $p.WaitForExit(120000)) { $p.Kill(); throw "browser timed out" }
  Get-Content $domFile -Raw
  return
}
if (-not $Out) { throw "give -Out <png> or -DumpDom" }
$outPath = [System.IO.Path]::GetFullPath($Out)
$p = Start-Process -FilePath $edge -ArgumentList ($browserArgs + "--screenshot=$outPath" + $url) -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(120000)) { $p.Kill(); throw "browser timed out" }
if (Test-Path $outPath) { "wrote $outPath ($((Get-Item $outPath).Length) bytes)" } else { throw "no screenshot written" }
