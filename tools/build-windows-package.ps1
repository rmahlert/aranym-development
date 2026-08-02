[CmdletBinding()]
param(
    [string]$SourceDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$CygwinRoot = "$env:LOCALAPPDATA\Cygwin64",
    [string]$OutputDir = "$PWD\dist",
    [ValidateSet("jit", "normal")]
    [string]$Build = "normal",
    [string]$EasyARAnyMDir = "",
    [int]$Jobs = 2
)

$ErrorActionPreference = "Stop"
$bash = Join-Path $CygwinRoot "bin\bash.exe"
$cygpath = Join-Path $CygwinRoot "bin\cygpath.exe"

if (-not (Test-Path $bash) -or -not (Test-Path $cygpath)) {
    throw "Cygwin was not found at '$CygwinRoot'. Run tools/install-cygwin-aranym.ps1 first."
}

function Convert-ToCygwinPath([string]$Path) {
    return (& $cygpath -u $Path).Trim()
}

$sourceCyg = Convert-ToCygwinPath (Resolve-Path $SourceDir)
$output = [IO.Path]::GetFullPath($OutputDir)
$buildArgs = "--disable-opengl --enable-slirp"
if ($Build -eq "jit") {
    $buildArgs += " --enable-jit-compiler --enable-addressing=fixed"
} else {
    $buildArgs += " --disable-jit-compiler --enable-addressing=normal"
}

Write-Host "Building ARAnyM ($Build) in Cygwin..."
$command = "cd '$sourceCyg' && ./autogen.sh $buildArgs && make -j$Jobs"
& $bash -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "ARAnyM build failed with exit code $LASTEXITCODE."
}

$exe = Join-Path $SourceDir "aranym.exe"
if (-not (Test-Path $exe)) {
    throw "The build completed without producing '$exe'."
}

$version = (& $exe --version 2>$null | Select-Object -First 1).Trim()
$safeVersion = ($version -replace '[^A-Za-z0-9._-]', '_')
$package = Join-Path $output "aranym-windows-$Build"
if (Test-Path $package) {
    Remove-Item -Recurse -Force $package
}
New-Item -ItemType Directory -Force $package | Out-Null

Copy-Item $exe (Join-Path $package "aranym.exe")
Copy-Item (Join-Path $SourceDir "COPYING") (Join-Path $package "COPYING.txt")
Copy-Item (Join-Path $SourceDir "README.md") (Join-Path $package "README.md")
Copy-Item (Join-Path $SourceDir "doc\config") (Join-Path $package "config.example")
New-Item -ItemType Directory -Force (Join-Path $package "data") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $package "network") | Out-Null
Copy-Item (Join-Path $SourceDir "data\wm_icon.bmp") (Join-Path $package "data\wm_icon.bmp")
Copy-Item (Join-Path $SourceDir "data\logo.bmp") (Join-Path $package "data\logo.bmp")
Copy-Item (Join-Path $SourceDir "data\installer_welcome.bmp") (Join-Path $package "data\installer_welcome.bmp")
Copy-Item (Join-Path $SourceDir "data\config") (Join-Path $package "data\config.example")
Copy-Item (Join-Path $SourceDir "data\emutos-aranym.img") (Join-Path $package "data\emutos-aranym.img")
Copy-Item (Join-Path $SourceDir "data\network\nfeth.xif") (Join-Path $package "network\nfeth.xif")

if ([string]::IsNullOrWhiteSpace($EasyARAnyMDir)) {
    $EasyARAnyMDir = Join-Path $SourceDir "dist\easyaramint"
}
if (Test-Path (Join-Path $EasyARAnyMDir "config")) {
    $atariPackage = Join-Path $package "atari"
    New-Item -ItemType Directory -Force $atariPackage | Out-Null
    foreach ($name in @("config", "emutos-aranym.img", "EasyMint", "host_fs", "nfeth.xif")) {
        $source = Join-Path $EasyARAnyMDir $name
        if (Test-Path $source) {
            Copy-Item $source $atariPackage -Recurse -Force
        }
    }
    Write-Host "Included optional EasyARAnyM Atari drives from $EasyARAnyMDir"
} else {
    Write-Warning "EasyARAnyM package not found at '$EasyARAnyMDir'; installer will omit optional Atari drives."
}

$runtimeNames = @(
    "cygwin1.dll", "cyggcc_s-1.dll", "cygstdc++-6.dll", "cygz.dll",
    "SDL2.dll", "SDL2_image.dll", "libjpeg-8.dll", "libslirp-0.dll",
    "libglib-2.0-0.dll", "libintl-8.dll", "libiconv-2.dll", "libpcre2-8-0.dll",
    "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "zlib1.dll",
    "libpng16-16.dll"
)
$runtimeRoots = @(
    (Join-Path $CygwinRoot "bin"),
    (Join-Path $CygwinRoot "usr\i686-w64-mingw32\sys-root\mingw\bin"),
    (Join-Path $CygwinRoot "usr\x86_64-w64-mingw32\sys-root\mingw\bin")
)
foreach ($name in $runtimeNames) {
    $match = $null
    foreach ($root in $runtimeRoots) {
        if (Test-Path $root) {
            $match = Get-ChildItem -Path $root -Filter $name -File -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($match) { break }
        }
    }
    if ($match) {
        Copy-Item $match.FullName (Join-Path $package $match.Name) -Force
    }
}

$readme = @"
ARAnyM Windows portable build

$version

Run aranym.exe from this directory. Copy config.example to an ARAnyM
configuration file and provide your own TOS or EmuTOS ROM image.

This package uses the conservative JITSafe profile when built with JIT.
"@
Set-Content -Path (Join-Path $package "README-Windows.txt") -Value $readme -Encoding ASCII

New-Item -ItemType Directory -Force $output | Out-Null
$archive = Join-Path $output "aranym-windows-$Build-$safeVersion.zip"
if (Test-Path $archive) {
    Remove-Item -Force $archive
}
Compress-Archive -Path (Join-Path $package "*") -DestinationPath $archive
Write-Host "Created $archive"
