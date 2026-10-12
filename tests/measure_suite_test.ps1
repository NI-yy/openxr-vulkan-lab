$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$testDir = Join-Path $repo "build/host-tests/measure-suite/$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null
$env:MOCK_ADB_DIR = $testDir
$env:MOCK_ADB_MISMATCH = ''
$env:MOCK_VALIDATION_MISMATCH = ''
$env:MOCK_SCENE_MISMATCH = ''
$env:MOCK_LOGCAT_FAILURE = ''
$env:MOCK_EXTERNAL_LAYER = ''
$apk = Join-Path $testDir 'app-debug.apk'
Set-Content -LiteralPath $apk -Value 'mock APK'
$fakeAdb = Join-Path $PSScriptRoot 'fake_adb.ps1'
$suite = Join-Path $repo 'tools/measure_suite.ps1'
$run = Join-Path $testDir 'success'
Set-Content -LiteralPath (Join-Path $testDir 'calls.txt') -Value ''
& $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $run
$manifest = Get-Content -LiteralPath (Join-Path $run 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.status -ne 'passed' -or $manifest.results.Count -ne 4) { throw 'Suite result is incomplete.' }
if ($manifest.validation_requested -ne 'off') { throw 'Validation condition missing from manifest.' }
if (@($manifest.results | Where-Object status -ne 'passed').Count -ne 0) { throw 'A condition failed.' }
foreach ($result in $manifest.results) {
    if (-not (Test-Path -LiteralPath (Join-Path $run $result.log))) { throw "Missing log: $($result.log)" }
}
$calls = @(Get-Content -LiteralPath (Join-Path $testDir 'calls.txt'))
if (@($calls | Where-Object { $_ -match '^-s mock-quest install -r ' }).Count -ne 1) {
    throw 'APK must be installed exactly once.'
}
$modeCalls = @($calls | Where-Object { $_ -match 'shell setprop debug.openxrvulkanlab.mode ' })
$foveationCalls = @($calls | Where-Object { $_ -match 'shell setprop debug.openxrvulkanlab.foveation ' })
$validationCalls = @($calls | Where-Object { $_ -match 'shell setprop debug.openxrvulkanlab.validation off$' })
if ($validationCalls.Count -ne 4) { throw 'Each measurement must explicitly disable validation.' }
$expectedModes = @('dual-pass', 'multiview', 'multiview', 'multiview')
$expectedFoveation = @('off', 'off', 'low', 'high')
for ($index = 0; $index -lt 4; ++$index) {
    if ($modeCalls[$index] -notmatch " $($expectedModes[$index])$" -or
        $foveationCalls[$index] -notmatch " $($expectedFoveation[$index])$") {
        throw "Wrong condition order at index $index"
    }
}
$env:MOCK_ADB_MISMATCH = '1'
$failedRun = Join-Path $testDir 'failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $failedRun -Conditions @('multiview:low')
} catch { $failed = $true }
if (-not $failed) { throw 'A mismatched selection must fail.' }
$failure = Get-Content -LiteralPath (Join-Path $failedRun 'manifest.json') -Raw | ConvertFrom-Json
if ($failure.status -ne 'failed' -or $failure.results[0].status -ne 'failed') {
    throw 'Failure was not saved to manifest.'
}
$env:MOCK_ADB_MISMATCH = ''
$env:MOCK_SCENE_MISMATCH = '1'
$sceneRun = Join-Path $testDir 'scene-failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $sceneRun -Conditions @('dual-pass:off')
} catch { $failed = $true }
if (-not $failed) { throw 'A mismatched scene must fail measurement.' }
$sceneFailure = Get-Content -LiteralPath (Join-Path $sceneRun 'manifest.json') -Raw | ConvertFrom-Json
if ($sceneFailure.status -ne 'failed' -or $sceneFailure.results[0].status -ne 'failed') {
    throw 'Scene failure was not saved to manifest.'
}
$env:MOCK_SCENE_MISMATCH = ''
$env:MOCK_LOGCAT_FAILURE = '1'
$logcatRun = Join-Path $testDir 'logcat-failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $logcatRun -Conditions @('dual-pass:off')
} catch { $failed = $true }
if (-not $failed) { throw 'A logcat failure must fail measurement.' }
if ((Get-Content -LiteralPath (Join-Path $testDir 'calls.txt') | Select-Object -Last 1) -notmatch
    'shell am force-stop dev.niyy.openxrvulkanlab$') {
    throw 'The app must be stopped after a mid-run failure.'
}
$env:MOCK_LOGCAT_FAILURE = ''
$env:MOCK_VALIDATION_MISMATCH = '1'
$validationRun = Join-Path $testDir 'validation-failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $validationRun -Conditions @('dual-pass:off')
} catch { $failed = $true }
if (-not $failed) { throw 'Validation selected on must fail measurement.' }
$env:MOCK_VALIDATION_MISMATCH = ''
$env:MOCK_EXTERNAL_LAYER = '1'
$externalRun = Join-Path $testDir 'external-failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $externalRun -Conditions @('dual-pass:off')
} catch { $failed = $true }
if (-not $failed) { throw 'Externally injected validation must fail measurement.' }
$env:MOCK_EXTERNAL_LAYER = '2'
$perAppRun = Join-Path $testDir 'per-app-failure'
$failed = $false
try {
    & $suite -AdbPath $fakeAdb -ApkPath $apk -OutputDirectory $perAppRun -Conditions @('dual-pass:off')
} catch { $failed = $true }
if (-not $failed) { throw 'Per-app GPU debug layers must fail measurement.' }
$env:MOCK_EXTERNAL_LAYER = ''
Write-Host 'Measurement suite host test passed.'
