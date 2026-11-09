#requires -Version 5.1
<# Private, one-shot c1 qualification. Prepare never launches the emulator.
   SPDX-License-Identifier: GPL-2.0-or-later #>
[CmdletBinding()]
param(
    [ValidateSet('Prepare','Run')][string]$Mode = 'Prepare',
    [string]$SeedRoot,
    [string]$Plan,
    [ValidateRange(30,300)][int]$TimeoutSeconds = 120
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Packet = [IO.Path]::GetFullPath($PSScriptRoot)
$Utf8 = New-Object Text.UTF8Encoding($false)
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function NewText([string]$Path,[string]$Text) {
    $f = New-Object IO.FileStream($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $b=$Utf8.GetBytes($Text); $f.Write($b,0,$b.Length) } finally { $f.Dispose() }
}
function NewJson([string]$Path,$Value) { NewText $Path (($Value | ConvertTo-Json -Depth 20)+"`r`n") }
function NoLinks([string]$Path) {
    $p=[IO.Path]::GetFullPath($Path)
    while ($p) {
        if (Test-Path -LiteralPath $p) {
            if ((Get-Item -Force -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path refused: $p" }
        }
        $parent=[IO.Path]::GetDirectoryName($p)
        if ($parent -eq $p) { break }; $p=$parent
    }
}
function ClosedCopy([string]$Source,[string]$Target,[string]$Expected) {
    NoLinks $Source; NoLinks $Target
    # Refuse an image held by any other reader/writer. Never write the seed.
    $seedStream = New-Object IO.FileStream($Source,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try {
        $digest=[Security.Cryptography.SHA256]::Create()
        try { $actual=([BitConverter]::ToString($digest.ComputeHash($seedStream))).Replace('-','').ToLowerInvariant() } finally { $digest.Dispose() }
        if ($actual -ne $Expected) { throw "Seed hash mismatch: $Source" }
        $seedStream.Position=0
        $targetStream=New-Object IO.FileStream($Target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $seedStream.CopyTo($targetStream); $targetStream.Flush() } finally { $targetStream.Dispose() }
    } finally { $seedStream.Dispose() }
    if ((Hash $Target) -ne $Expected) { throw "Copy hash mismatch: $Target" }
}
function ExpectedConfig([string]$Dir,[string]$Arm) {
$jit=if ($arm -eq 'jit-on') { 'Yes' } else { 'No' }
$rom=(Join-Path $dir 'emutos-aranym.img').Replace('\','/')
$disk=(Join-Path $dir 'prepared.img').Replace('\','/')
$hostDir=(Join-Path $dir 'host').Replace('\','/')
return @"
[GLOBAL]
FastRAM = 64
FixedMemoryOffset = 60000000
EmuTOS = $rom
[JIT]
JIT = $jit
JITFPU = No
JITCacheSize = 8192
[HOSTFS]
H = $hostDir
[IDE0]
Present = Yes
Path = $disk
ByteSwap = No
ReadOnly = No
ModelName = Master
[ETH0]
Type =
Tunnel =
[STARTUP]
GrabMouse = No
[AUDIO]
Enabled = No
[TOS]
RedirConsole = Yes
[NFVDI]
UseHostMouseCursor = No
"@
}
NoLinks $Packet
$manifestPath=Join-Path $Packet 'SHA256-MANIFEST.json'
$manifestHash=Hash $manifestPath
$manifest=Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
foreach ($item in $manifest.files.PSObject.Properties) {
    if ([IO.Path]::IsPathRooted($item.Name) -or ([regex]::Split($item.Name,'[\\/]') -contains '..')) { throw 'Unsafe manifest path' }
    $p=Join-Path $Packet $item.Name
    NoLinks $p
    if ((Hash $p) -ne $item.Value) { throw "Packet changed: $($item.Name)" }
}
$listed=@($manifest.files.PSObject.Properties.Name)+@('SHA256-MANIFEST.json')
foreach ($file in Get-ChildItem -Force -Recurse -File -LiteralPath $Packet) {
    $relative=$file.FullName.Substring($Packet.Length+1).Replace('\','/')
    if ($listed -notcontains $relative) { throw "Unexpected packet file: $relative" }
}
$exe=Join-Path $Packet $manifest.binary
if ((Hash $exe) -ne 'f3eac0a6e7abafe42eb7f7f12f8e1f20b7b328cef1ecd7bfe54449fe001bbae7') { throw 'Unexpected candidate' }
$homePath=[Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
if (-not $homePath) { throw 'Cannot resolve native Windows user profile' }
NoLinks $homePath
$ownedProfile=Join-Path $homePath 'aranym-upstream-jit-experimental'
$seeds=Get-Content -Raw -LiteralPath (Join-Path $Packet 'SEEDS.json') | ConvertFrom-Json
if ($Mode -eq 'Prepare') {
    if (-not $SeedRoot -or $Plan) { throw 'Prepare requires -SeedRoot and no -Plan' }
    $SeedRoot=[IO.Path]::GetFullPath($SeedRoot); NoLinks $SeedRoot
    foreach ($item in $seeds.files.PSObject.Properties) {
        if ((Hash (Join-Path $SeedRoot $item.Name)) -ne $item.Value) { throw "Seed differs: $($item.Name)" }
    }
    if (Test-Path -LiteralPath $ownedProfile) { throw "Experimental profile exists; inspect/archive it before preparing a fresh test: $ownedProfile" }
    $token=[Guid]::NewGuid().ToString('N')
    $work=Join-Path $homePath ('aranym-modern-jit-smoke-'+$token)
    New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
    $configs=@{}
    foreach ($arm in @('jit-off','jit-on')) {
        $dir=Join-Path $work $arm; New-Item -ItemType Directory -Path $dir | Out-Null
        $config=ExpectedConfig $dir $arm
        $configPath=Join-Path $dir 'config'; NewText $configPath $config
        $configs[$arm]=Hash $configPath
    }
    $planPath=Join-Path $work 'plan.json'
    NewJson $planPath ([ordered]@{token=$token;work=$work;seed_root=$SeedRoot;packet=$Packet;packet_manifest_sha256=$manifestHash;profile=$ownedProfile;config_sha256=$configs;timeout_seconds=$TimeoutSeconds;legacy_fpu=$true;memory_pilot=$false;prepared_only=$true})
    Write-Output "Prepared configs and one-shot plan. No emulator launched. Review $planPath"
    Write-Output "Later separate smoke command: powershell.exe -NoProfile -File `"$PSCommandPath`" -Mode Run -Plan `"$planPath`""
    exit 0
}
if (-not $Plan -or $SeedRoot) { throw 'Run requires -Plan and no -SeedRoot' }
$Plan=[IO.Path]::GetFullPath($Plan); NoLinks $Plan
$p=Get-Content -Raw -LiteralPath $Plan | ConvertFrom-Json
$expectedWork=Join-Path $homePath ('aranym-modern-jit-smoke-'+$p.token)
if ($p.token -notmatch '^[a-f0-9]{32}$' -or $p.work -ne $expectedWork -or $Plan -ne (Join-Path $expectedWork 'plan.json') -or $p.packet -ne $Packet -or $p.packet_manifest_sha256 -ne $manifestHash -or $p.profile -ne $ownedProfile) { throw 'Plan ownership/packet mismatch' }
if ($p.timeout_seconds -lt 30 -or $p.timeout_seconds -gt 300) { throw 'Invalid plan deadline' }
NoLinks $p.work; NoLinks $p.seed_root
if (Test-Path -LiteralPath $ownedProfile) { throw 'Existing profile refused; no automatic reuse or retry' }
foreach ($arm in @('jit-off','jit-on')) {
    $dir=Join-Path $p.work $arm
    $config=Join-Path $dir 'config'; NoLinks $config
    if ([IO.File]::ReadAllText($config) -ne (ExpectedConfig $dir $arm)) { throw "Config escapes exact owned profile: $arm" }
    if ((Hash $config) -ne $p.config_sha256.$arm) { throw "Config changed: $arm" }
}
# CreateNew is the one-shot launch lock: a failed/interrupted attempt is retained.
NewJson (Join-Path $p.work 'run-started.json') ([ordered]@{utc=[DateTime]::UtcNow.ToString('o');packet_manifest_sha256=$manifestHash;plan_sha256=(Hash $Plan)})
New-Item -ItemType Directory -Path $ownedProfile -ErrorAction Stop | Out-Null
NewJson (Join-Path $ownedProfile 'owned-smoke-profile.json') ([ordered]@{token=$p.token;work=$p.work;packet_manifest_sha256=$manifestHash})
$lock=New-Object IO.FileStream((Join-Path $ownedProfile 'smoke.lock'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$results=@(); $failed=$false
try {
    foreach ($arm in @('jit-off','jit-on')) {
        $dir=Join-Path $p.work $arm
        foreach ($item in $seeds.files.PSObject.Properties) {
            $target=Join-Path $dir $item.Name
            $parent=[IO.Path]::GetDirectoryName($target)
            if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
            ClosedCopy (Join-Path $p.seed_root $item.Name) $target $item.Value
        }
        $nvram=Join-Path $ownedProfile 'nvram'; NoLinks $nvram
        if (Test-Path -LiteralPath $nvram) { Remove-Item -LiteralPath $nvram -Force }
        ClosedCopy (Join-Path $dir 'starting-nvram.bin') $nvram $seeds.files.'starting-nvram.bin'
        $info=New-Object Diagnostics.ProcessStartInfo
        $info.FileName=$exe; $info.Arguments='-c "'+(Join-Path $dir 'config')+'"'
        $info.WorkingDirectory=$dir; $info.UseShellExecute=$false
        $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
        foreach ($name in @($info.EnvironmentVariables.Keys)) {
            if ($name -like 'ARANYM_*' -or $name -like 'SDL_*') { $info.EnvironmentVariables.Remove($name) }
        }
        $info.EnvironmentVariables['ARANYM_MODERN040_LEGACY_FPU']='1'
        $info.EnvironmentVariables['ARANYM_UPSTREAM_JIT_DIAGNOSTICS']='1'
        $process=New-Object Diagnostics.Process; $process.StartInfo=$info
        $clock=[Diagnostics.Stopwatch]::StartNew(); $timedOut=$false
        try {
            if (-not $process.Start()) { throw 'Process did not start' }
            $childPid=$process.Id
            $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
            while (-not $process.WaitForExit(1000)) {
                if ($clock.Elapsed.TotalSeconds -ge $p.timeout_seconds) { $timedOut=$true; $process.Kill(); break }
            }
            $process.WaitForExit(); $clock.Stop()
            $out=$stdout.GetAwaiter().GetResult(); $err=$stderr.GetAwaiter().GetResult(); $exit=$process.ExitCode
        } finally { $process.Dispose() }
        NewText (Join-Path $dir 'stdout.log') $out; NewText (Join-Path $dir 'stderr.log') $err
        $text=$out+"`n"+$err; $checks=[ordered]@{}
        foreach ($marker in $seeds.required_markers) { $checks[$marker]=$text.Contains($marker) }
        $witness=[regex]::Match($text,'UPSTREAM-JIT compiled-insns=(\d+) cached-dispatches=(\d+) executed-translated=(\d+) executed-guest=(\d+)')
        $observed=[ordered]@{}
        if ($witness.Success) {
            $observed.emitted_instructions=[UInt64]$witness.Groups[1].Value
            $observed.cached_dispatches=[UInt64]$witness.Groups[2].Value
            $observed.executed_translated=[UInt64]$witness.Groups[3].Value
            $observed.executed_guest=[UInt64]$witness.Groups[4].Value
        }
        $pass=($exit -eq 0 -and -not $timedOut -and -not ($checks.Values -contains $false))
        if ($arm -eq 'jit-on') { $pass=$pass -and $witness.Success -and $observed.executed_translated -gt 0 -and $observed.executed_guest -gt 0 }
        $post=[ordered]@{}
        foreach ($item in $seeds.files.PSObject.Properties) { $post[$item.Name]=Hash (Join-Path $dir $item.Name) }
        $record=[ordered]@{arm=$arm;passed=[bool]$pass;exit=$exit;timeout=$timedOut;pid=$childPid;elapsed_seconds=$clock.Elapsed.TotalSeconds;checks=$checks;jit_observed=$observed;legacy_fpu=$true;jit_fpu=$false;memory_pilot=$false;starting_seed_sha256=$seeds.files;post_copy_sha256=$post;nvram_post_sha256=(Hash $nvram);config_sha256=(Hash (Join-Path $dir 'config'));stdout_sha256=(Hash (Join-Path $dir 'stdout.log'));stderr_sha256=(Hash (Join-Path $dir 'stderr.log'));binary_sha256=(Hash $exe);packet_manifest_sha256=$manifestHash}
        NewJson (Join-Path $dir 'result.json') $record; $results+=$record
        if (-not $pass) { $failed=$true; break }
    }
    NewJson (Join-Path $p.work 'result.json') ([ordered]@{passed=(-not $failed -and $results.Count -eq 2);arms=$results;no_performance_acceptance=$true;scope='Windows owned64MiB c1 boot/font/shutdown smoke only; other CPU/ABI/HighWire gates remain'})
} finally { $lock.Dispose() }
Write-Output "Results retained under $($p.work). Experimental profile retained at $ownedProfile"
if ($failed) { exit 1 }; exit 0
