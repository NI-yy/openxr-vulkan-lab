param()

$ErrorActionPreference = 'Stop'

function Require-Path($Path, $Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description が見つかりません: $Path"
    }
}

function Run-Checked($Executable, $Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable が終了コード $LASTEXITCODE で失敗しました"
    }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdk = $env:ANDROID_HOME
if (-not $sdk) { $sdk = $env:ANDROID_SDK_ROOT }
if (-not $sdk) { throw 'ANDROID_HOME または ANDROID_SDK_ROOT を設定してください' }
Require-Path $sdk 'Android SDK'
foreach ($package in @('platforms/android-34', 'build-tools/34.0.0', 'ndk/23.2.8568313', 'cmake/3.22.1')) {
    Require-Path (Join-Path $sdk $package) "Android SDK パッケージ $package"
}

$java = 'java'
if ($env:JAVA_HOME) {
    $java = Join-Path $env:JAVA_HOME 'bin/java'
    if ($IsWindows -or $env:OS -eq 'Windows_NT') { $java += '.exe' }
    Require-Path $java 'JAVA_HOME の Java'
} elseif (-not (Get-Command java -ErrorAction SilentlyContinue)) {
    throw 'Java 21 が見つかりません。JAVA_HOME を設定してください'
}
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue' # Windows PowerShell 5.1 treats java -version stderr as an error.
try {
    $javaOutput = @(& $java -version 2>&1)
    $javaExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
}
$javaVersion = [string]$javaOutput[0]
if ($javaExitCode -ne 0 -or $javaVersion -notmatch 'version "21(\.|\")') {
    throw "Java 21 が必要です: $javaVersion"
}

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command clang++ -ErrorAction SilentlyContinue }
if (-not $compiler) { throw 'ホスト C++ コンパイラ (g++ または clang++) が見つかりません' }

$windowsHost = $IsWindows -or $env:OS -eq 'Windows_NT'
$wrapper = Join-Path $repository 'gradlew'
if ($windowsHost) { $wrapper += '.bat' }
Require-Path $wrapper 'Gradle Wrapper'

Push-Location $repository
try {
    Write-Host "Environment OK: $javaVersion; SDK=$sdk; C++=$($compiler.Source)"
    $outputDir = Join-Path $repository 'build/host-tests'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $testExecutable = Join-Path $outputDir 'scene_test'
    if ($IsWindows -or $env:OS -eq 'Windows_NT') { $testExecutable += '.exe' }
    Run-Checked $compiler.Source @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iapp/src/main/cpp', 'tests/scene_test.cpp', 'app/src/main/cpp/scene.cpp', '-o', $testExecutable)
    Run-Checked $testExecutable @()
    Write-Host 'Host scene test passed.'
    if ($windowsHost) {
        Run-Checked $wrapper @(':app:assembleDebug', '--no-daemon', '--console=plain')
    } else {
        Run-Checked 'bash' @($wrapper, ':app:assembleDebug', '--no-daemon', '--console=plain')
    }
    $apk = Join-Path $repository 'app/build/outputs/apk/debug/app-debug.apk'
    Require-Path $apk 'Debug APK'
    Write-Host "Verification passed: $apk"
} finally {
    Pop-Location
}
