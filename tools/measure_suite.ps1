param(
    [string[]]$Conditions = @('dual-pass:off', 'multiview:off', 'multiview:low', 'multiview:high'),
    [string]$OutputDirectory = '',
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 180,
    [string]$Serial = '',
    [string]$AdbPath = '',
    [string]$ApkPath = ''
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
if (-not $AdbPath -and -not $sdk) { throw 'Set ANDROID_HOME or ANDROID_SDK_ROOT, or pass -AdbPath.' }
$adb = if ($AdbPath) { $AdbPath } else { Join-Path $sdk 'platform-tools/adb.exe' }
$apk = if ($ApkPath) { $ApkPath } else { Join-Path $repo 'app/build/outputs/apk/debug/app-debug.apk' }
if (-not (Test-Path -LiteralPath $adb)) { throw "adb not found: $adb" }
if (-not (Test-Path -LiteralPath $apk)) { throw "Build the debug APK first: $apk" }
if ($Conditions.Count -eq 0) { throw 'Specify at least one condition.' }

$planned = [Collections.Generic.List[object]]::new()
foreach ($condition in $Conditions) {
    if ($condition -notmatch '^(dual-pass|multiview):(off|low|high)$') {
        throw "Invalid condition '$condition'; use mode:foveation (dual-pass or multiview; off, low, or high)."
    }
    if ($Matches[1] -eq 'dual-pass' -and $Matches[2] -ne 'off') {
        throw "FFR is unavailable in dual-pass: $condition"
    }
    $planned.Add([pscustomobject]@{ mode = $Matches[1]; foveation = $Matches[2] })
}

$devices = @(& $adb devices | Select-String '^\S+\s+device$')
if ($LASTEXITCODE -ne 0) { throw 'adb devices failed.' }
if ($Serial) {
    if (-not ($devices | Where-Object { ($_.Line -split '\s+')[0] -eq $Serial })) {
        throw "Device $Serial is not authorized; check adb devices -l."
    }
} elseif ($devices.Count -eq 1) {
    $Serial = ($devices[0].Line -split '\s+')[0]
} else {
    throw 'Connect and authorize exactly one device, or specify -Serial; check adb devices -l.'
}

function Device([string[]]$arguments) {
    $result = @(& $script:adb -s $Serial @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "adb $($arguments -join ' ') failed: $($result -join ' ')" }
    return ($result -join "`n").Trim()
}

if (-not $OutputDirectory) {
    $OutputDirectory = "docs/measurements/$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$directory = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
}
if (Test-Path -LiteralPath $directory) { throw "Output directory already exists: $directory" }
$gitCommit = ((git -C $repo rev-parse HEAD) -join '').Trim()
$gitStatus = @((git -C $repo status --short) | ForEach-Object { [string]$_ })
New-Item -ItemType Directory -Path $directory -Force | Out-Null

$manifest = [ordered]@{
    started = (Get-Date -Format o)
    finished = $null
    status = 'running'
    git_commit = $gitCommit
    git_status = $gitStatus
    apk_path = [IO.Path]::GetFullPath($apk)
    apk_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $apk).Hash
    timeout_seconds = $TimeoutSeconds
    scene = 'grid-100'
    validation_requested = 'off'
    device = [ordered]@{
        serial = $Serial
        model = Device @('shell', 'getprop', 'ro.product.model')
        android = Device @('shell', 'getprop', 'ro.build.version.release')
        build_fingerprint = Device @('shell', 'getprop', 'ro.build.fingerprint')
        security_patch = Device @('shell', 'getprop', 'ro.build.version.security_patch')
        foveation_system_level = Device @('shell', 'getprop', 'debug.oculus.foveation.level')
        foveation_system_dynamic = Device @('shell', 'getprop', 'debug.oculus.foveation.dynamic')
    }
    conditions = @($planned.ToArray())
    results = [Collections.Generic.List[object]]::new()
}
$manifestPath = Join-Path $directory 'manifest.json'
function Save-Manifest {
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}

Save-Manifest
try {
    for ($index = 0; $index -lt $planned.Count; ++$index) {
        $condition = $planned[$index]
        $name = '{0:D2}-{1}-{2}.txt' -f ($index + 1), $condition.mode, $condition.foveation
        $logPath = Join-Path $directory $name
        $result = [ordered]@{
            mode = $condition.mode
            foveation = $condition.foveation
            validation_requested = 'off'
            log = $name
            started = (Get-Date -Format o)
            finished = $null
            status = 'running'
            error = $null
        }
        $manifest.results.Add($result)
        Save-Manifest
        Write-Host "Measuring $($index + 1)/$($planned.Count): $($condition.mode):$($condition.foveation)"
        try {
            $arguments = @{
                Mode = $condition.mode
                Foveation = $condition.foveation
                OutputPath = $logPath
                TimeoutSeconds = $TimeoutSeconds
                Serial = $Serial
                AdbPath = $adb
                ApkPath = $apk
            }
            if ($index -gt 0) { $arguments.SkipInstall = $true }
            & (Join-Path $PSScriptRoot 'measure_dual_pass.ps1') @arguments
            $result.status = 'passed'
        } catch {
            $result.status = 'failed'
            $result.error = $_.Exception.Message
            throw
        } finally {
            $result.finished = Get-Date -Format o
            Save-Manifest
        }
    }
    $manifest.status = 'passed'
} catch {
    $manifest.status = 'failed'
    throw
} finally {
    $manifest.finished = Get-Date -Format o
    Save-Manifest
    Write-Host "Saved $manifestPath"
}
