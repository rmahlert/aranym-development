param(
    [string]$CygwinRoot = (Join-Path $env:LOCALAPPDATA "Cygwin64"),
    [string]$Mirror = "https://mirrors.kernel.org/sourceware/cygwin/"
)

$ErrorActionPreference = "Stop"
$setupPath = Join-Path $env:TEMP "cygwin-setup-x86_64.exe"
$packageCache = Join-Path $env:TEMP "cygwin-package-cache"
$packages = @(
    "gcc-core",
    "gcc-g++",
    "make",
    "makedepend",
    "git",
    "autoconf",
    "automake",
    "libtool",
    "cygutils",
    "patch",
    "mingw64-i686-gcc-core",
    "mingw64-i686-gcc-g++",
    "mingw64-i686-SDL2",
    "mingw64-i686-SDL2_image",
    "libjpeg-devel",
    "libslirp-devel"
)

if (-not (Test-Path $setupPath)) {
    Invoke-WebRequest -Uri "https://www.cygwin.com/setup-x86_64.exe" -OutFile $setupPath
}

New-Item -ItemType Directory -Force -Path $packageCache | Out-Null
$packageList = $packages -join ","
$arguments = @(
    "--quiet-mode",
    "--no-admin",
    "--root", $CygwinRoot,
    "--local-package-dir", $packageCache,
    "--site", $Mirror,
    "--packages", $packageList,
    "--no-shortcuts",
    "--no-startmenu",
    "--no-desktop",
    "--upgrade-also"
)

& $setupPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Cygwin setup failed with exit code $LASTEXITCODE"
}

$bashPath = Join-Path $CygwinRoot "bin\bash.exe"
if (-not (Test-Path $bashPath)) {
    throw "Cygwin was not installed at $CygwinRoot"
}

Write-Host "Cygwin build tools installed at $CygwinRoot"
Write-Host "Open Cygwin Terminal and run:"
Write-Host "  git clone https://github.com/aranym/aranym.git"
Write-Host "  cd aranym"
Write-Host "  ./autogen.sh"
Write-Host "  make -j2"
