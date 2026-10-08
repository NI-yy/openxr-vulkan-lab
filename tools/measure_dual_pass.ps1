param(
    [string]$OutputPath = "docs/dual-pass-$(Get-Date -Format 'yyyyMMdd-HHmmss').txt",
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $env:ANDROID_HOME 'platform-tools/adb.exe'
$apk = Join-Path $repo 'app/build/outputs/apk/debug/app-debug.apk'
if (-not (Test-Path $adb)) { throw "adb not found: $adb" }
if (-not (Test-Path $apk)) { throw "Build the debug APK first: $apk" }

$devices = @(& $adb devices | Select-String '^\S+\s+device$')
if ($devices.Count -ne 1) {
    throw 'Connect and authorize exactly one Quest 2; check adb devices -l.'
}
$serial = ($devices[0].Line -split '\s+')[0]
$destination = if ([IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath
} else {
    Join-Path $repo $OutputPath
}
$parent = Split-Path -Parent $destination
New-Item -ItemType Directory -Force -Path $parent | Out-Null

$lines = [Collections.Generic.List[string]]::new()
function Add-Section([string]$name, [string[]]$content) {
    $script:lines.Add("=== $name ===")
    foreach ($line in $content) { $script:lines.Add($line) }
    $script:lines.Add('')
}
function Device([string[]]$arguments) {
    $result = @(& $script:adb -s $script:serial @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($arguments -join ' ') failed: $($result -join ' ')"
    }
    $result
}

Add-Section 'Host' @(
    "started=$(Get-Date -Format o)",
    "git_commit=$(git -C $repo rev-parse HEAD)",
    "git_status=$(git -C $repo status --short | Out-String)",
    "apk_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $apk).Hash)"
)
Add-Section 'Device' @(
    'adb_device=authorized Quest 2',
    "model=$(Device @('shell','getprop','ro.product.model'))",
    "android=$(Device @('shell','getprop','ro.build.version.release'))",
    "build=$(Device @('shell','getprop','ro.build.fingerprint'))",
    "security_patch=$(Device @('shell','getprop','ro.build.version.security_patch'))"
)
Add-Section 'Thermal before' (Device @('shell','dumpsys','thermalservice'))

Device @('install','-r',$apk) | Out-Null
Device @('logcat','-c') | Out-Null
Device @('shell','am','force-stop','dev.niyy.openxrvulkanlab') | Out-Null
Device @('shell','am','start','-n','dev.niyy.openxrvulkanlab/android.app.NativeActivity') | Out-Null
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$complete = $false
do {
    Start-Sleep -Seconds 3
    $log = Device @('logcat','-d','-s','OpenXRVulkanLab:I','*:S')
    $complete = [bool]($log | Select-String 'Benchmark complete:')
} until ($complete -or (Get-Date) -ge $deadline)

Add-Section 'Measurement' @("finished=$(Get-Date -Format o)", "complete=$complete")
Add-Section 'Application log' $log
Add-Section 'Thermal after' (Device @('shell','dumpsys','thermalservice'))
$lines | Set-Content -LiteralPath $destination -Encoding utf8
Device @('shell','am','force-stop','dev.niyy.openxrvulkanlab') | Out-Null
Write-Host "Saved $destination"
if (-not $complete) { throw "Benchmark did not finish within $TimeoutSeconds seconds; inspect $destination" }
