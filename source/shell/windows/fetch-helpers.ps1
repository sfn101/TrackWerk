# Puts the three Studio SOURCE helpers in shell\windows\helpers\ (specs/windows-sources.md):
#   yt-dlp.exe  official release, checked against its pinned SHA-256
#   qjs.exe     QuickJS built from external\quickjs-2026-06-04.tar.xz (the Linux build's source)
#   ffmpeg.exe  a minimal FFmpeg 7.1 built from external\ffmpeg-7.1.tar.xz (signed upstream source; remux only)
# The build copies them beside pockettracker-sdl.exe in tools\, and build-windows.ps1 into the zip.
# Needs MSYS2 at C:\msys64 with the UCRT64 gcc, make and diffutils:
#   C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed mingw-w64-ucrt-x86_64-gcc make diffutils"
#
#   powershell -ExecutionPolicy Bypass -File shell/windows/fetch-helpers.ps1
$ErrorActionPreference = 'Stop'
$here   = $PSScriptRoot
$out    = Join-Path $here 'helpers'
$cache  = Join-Path $here '..\build\helpers-cache'   # build scratch, outside the source tree
$source = (Resolve-Path (Join-Path $here '..\..')).Path
New-Item -ItemType Directory -Force $out, $cache | Out-Null

$pins = @{
    YtDlpUrl   = 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.10.26/yt-dlp.exe'
    YtDlpSha   = 'f5389d3698ecde4ef06f4e6b1ce0294fe9ac1396228c8990b8d3416d6d177a7d'
    FfmpegSha  = '40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6'
    QuickJsSha = 'b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'
}

function Get-Sha([string]$path) { (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant() }

function Get-Pinned([string]$url, [string]$path, [string]$sha) {
    if ((Test-Path $path) -and (Get-Sha $path) -eq $sha) { return }
    Write-Host "downloading $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile "$path.part"
    if ((Get-Sha "$path.part") -ne $sha) { Remove-Item "$path.part"; throw "hash mismatch: $url" }
    Move-Item -Force "$path.part" $path
}

Get-Pinned $pins.YtDlpUrl  (Join-Path $out 'yt-dlp.exe')        $pins.YtDlpSha

$qjsTar = Join-Path $source 'external\quickjs-2026-06-04.tar.xz'
if ((Get-Sha $qjsTar) -ne $pins.QuickJsSha) { throw 'QuickJS source hash differs' }
$ffTar = Join-Path $source 'external\ffmpeg-7.1.tar.xz'
if ((Get-Sha $ffTar) -ne $pins.FfmpegSha) { throw 'FFmpeg source hash differs' }

$bash = 'C:\msys64\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw 'MSYS2 not found at C:\msys64 (see the header of this script)' }
$env:MSYSTEM = 'UCRT64'; $env:CHERE_INVOKING = '1'
$toMsys = { param($p) & 'C:\msys64\usr\bin\cygpath.exe' -u $p }
& $bash -lc ("bash '{0}' '{1}' '{2}' '{3}' '{4}'" -f (& $toMsys (Join-Path $here 'build-helpers.sh')),
    (& $toMsys $qjsTar), (& $toMsys $ffTar), (& $toMsys (Join-Path $cache 'work')), (& $toMsys $out))
if ($LASTEXITCODE) { throw "helper build failed ($LASTEXITCODE)" }

foreach ($exe in 'yt-dlp.exe', 'qjs.exe', 'ffmpeg.exe') {
    $p = Join-Path $out $exe
    if (-not (Test-Path $p)) { throw "missing $exe" }
    '{0,-11} {1,10:N0} bytes  {2}' -f $exe, (Get-Item $p).Length, (Get-Sha $p)
}
