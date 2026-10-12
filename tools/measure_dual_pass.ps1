param(
    [ValidateSet('dual-pass', 'multiview')]
    [string]$Mode = 'dual-pass',
    [ValidateSet('off', 'low', 'high')]
    [string]$Foveation = 'off',
    [string]$OutputPath = '',
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 180,
    [switch]$SkipInstall,
    [string]$Serial = '',
    [string]$AdbPath = '',
    [string]$ApkPath = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = "docs/$Mode-$(Get-Date -Format 'yyyyMMdd-HHmmss').txt"
}
$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
if (-not $AdbPath -and -not $sdk) { throw 'Set ANDROID_HOME or ANDROID_SDK_ROOT, or pass -AdbPath.' }
$adb = if ($AdbPath) { $AdbPath } else { Join-Path $sdk 'platform-tools/adb.exe' }
$apk = if ($ApkPath) { $ApkPath } else { Join-Path $repo 'app/build/outputs/apk/debug/app-debug.apk' }
if (-not (Test-Path $adb)) { throw "adb not found: $adb" }
if (-not (Test-Path $apk)) { throw "Build the debug APK first: $apk" }

$devices = @(& $adb devices | Select-String '^\S+\s+device$')
if ($Serial) {
    if (-not ($devices | Where-Object { ($_.Line -split '\s+')[0] -eq $Serial })) {
        throw "Device $Serial is not authorized; check adb devices -l."
    }
} elseif ($devices.Count -ne 1) {
    throw 'Connect and authorize exactly one device, or specify -Serial; check adb devices -l.'
} else {
    $Serial = ($devices[0].Line -split '\s+')[0]
}
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
    $result = @(& $script:adb -s $Serial @arguments 2>&1)
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
    "adb_device=$Serial",
    "model=$(Device @('shell','getprop','ro.product.model'))",
    "android=$(Device @('shell','getprop','ro.build.version.release'))",
    "build=$(Device @('shell','getprop','ro.build.fingerprint'))",
    "security_patch=$(Device @('shell','getprop','ro.build.version.security_patch'))"
)
Add-Section 'Foveation overrides' @(
    "system_level=$(Device @('shell','getprop','debug.oculus.foveation.level'))",
    "system_dynamic=$(Device @('shell','getprop','debug.oculus.foveation.dynamic'))"
)
Add-Section 'Thermal before' (Device @('shell','dumpsys','thermalservice'))

function Check-ExternalValidation {
    $globalLayers = ((Device @('shell','getprop','debug.vulkan.layers')) -join '').Trim()
    $gpuEnabled = ((Device @('shell','settings','get','global','enable_gpu_debug_layers')) -join '').Trim()
    $gpuApp = ((Device @('shell','settings','get','global','gpu_debug_app')) -join '').Trim()
    $gpuLayers = ((Device @('shell','settings','get','global','gpu_debug_layers')) -join '').Trim()
    Add-Section 'Vulkan validation environment' @(
        "debug_vulkan_layers=$globalLayers",
        "enable_gpu_debug_layers=$gpuEnabled",
        "gpu_debug_app=$gpuApp",
        "gpu_debug_layers=$gpuLayers"
    )
    if ($globalLayers -and $globalLayers -ne 'null') {
        throw 'Global Vulkan layers are active. Clear debug.vulkan.layers before measuring.'
    }
    if ($gpuEnabled -eq '1' -and $gpuApp -eq 'dev.niyy.openxrvulkanlab' -and
        $gpuLayers -and $gpuLayers -ne 'null') {
        throw 'GPU debug layers are enabled for this app. Clear per-app GPU debug settings before measuring.'
    }
}
Check-ExternalValidation

if (-not $SkipInstall) { Device @('install','-r',$apk) | Out-Null }
Device @('shell','setprop','debug.openxrvulkanlab.validation','off') | Out-Null
Device @('shell','setprop','debug.openxrvulkanlab.mode',$Mode) | Out-Null
Device @('shell','setprop','debug.openxrvulkanlab.foveation',$Foveation) | Out-Null
Device @('shell','setprop','debug.openxrvulkanlab.scene','grid-100') | Out-Null
Device @('logcat','-c') | Out-Null
Device @('shell','am','force-stop','dev.niyy.openxrvulkanlab') | Out-Null
try {
    Device @('shell','am','start','-n','dev.niyy.openxrvulkanlab/android.app.NativeActivity') | Out-Null
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $complete = $false
    $wrongMode = $false
    $wrongFoveation = $false
    $wrongScene = $false
    $wrongValidation = $false
    do {
        Start-Sleep -Seconds 3
        $log = Device @('logcat','-d','-s','OpenXRVulkanLab:I','*:S')
        $complete = [bool]($log | Select-String 'Benchmark complete:')
        $wrongMode = [bool]($log | Select-String 'Benchmark config: ') -and
            -not [bool]($log | Select-String "Benchmark config: $Mode ")
        $wrongFoveation = [bool]($log | Select-String 'Benchmark config: ') -and
            -not [bool]($log | Select-String "foveation=$Foveation")
        $wrongScene = [bool]($log | Select-String 'Benchmark config: ') -and
            -not [bool]($log | Select-String 'Benchmark config: .* scene=grid-100')
        $wrongValidation = [bool]($log | Select-String 'Vulkan validation: requested=') -and
            -not [bool]($log | Select-String 'Vulkan validation: requested=off selected=off layer=none')
    } until ($complete -or $wrongMode -or $wrongFoveation -or $wrongScene -or $wrongValidation -or (Get-Date) -ge $deadline)

    $selected = [bool]($log | Select-String "Benchmark config: $Mode ")
    $foveationSelected = [bool]($log | Select-String "Benchmark config: .* foveation=$Foveation")
    $sceneSelected = [bool]($log | Select-String 'Benchmark config: .* scene=grid-100') -and
        [bool]($log | Select-String 'Benchmark complete: .* scene=grid-100')
    $validationOff = [bool]($log | Select-String 'Vulkan validation: requested=off selected=off layer=none')
    Check-ExternalValidation
    Add-Section 'Measurement' @("finished=$(Get-Date -Format o)", "requested_mode=$Mode", "selected_mode_matched=$selected", "requested_foveation=$Foveation", "selected_foveation_matched=$foveationSelected", 'requested_scene=grid-100', "selected_scene_matched=$sceneSelected", 'validation_requested=off', "validation_off_confirmed=$validationOff", "complete=$complete")
    Add-Section 'Application log' $log
    Add-Section 'Thermal after' (Device @('shell','dumpsys','thermalservice'))
    $lines | Set-Content -LiteralPath $destination -Encoding utf8
    Write-Host "Saved $destination"
    if (-not $selected) { throw "Requested render mode was not selected; inspect $destination" }
    if (-not $foveationSelected) { throw "Requested foveation level was not selected; inspect $destination" }
    if (-not $sceneSelected) { throw "Requested scene was not selected; inspect $destination" }
    if (-not $validationOff -or $wrongValidation) { throw "Vulkan validation was not confirmed off; inspect $destination" }
    if (-not $complete) { throw "Benchmark did not finish within $TimeoutSeconds seconds; inspect $destination" }
} finally {
    try { Device @('shell','am','force-stop','dev.niyy.openxrvulkanlab') | Out-Null }
    catch { Write-Warning "Could not stop app after measurement: $($_.Exception.Message)" }
}
