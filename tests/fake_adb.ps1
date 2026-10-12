$ErrorActionPreference = 'Stop'
$arguments = @($args)
Add-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'calls.txt') -Value ($arguments -join ' ')
if ($arguments[0] -eq 'devices') {
    'List of devices attached'
    "mock-quest`tdevice"
    exit 0
}
if ($arguments[0] -ne '-s' -or $arguments[1] -ne 'mock-quest') { exit 2 }
$command = @($arguments[2..($arguments.Count - 1)])
if ($command[0] -eq 'shell' -and $command[1] -eq 'getprop') {
    if ($command[2] -eq 'debug.vulkan.layers' -and $env:MOCK_EXTERNAL_LAYER -eq '1') {
        'VK_LAYER_KHRONOS_validation'
    }
    switch ($command[2]) {
        'ro.product.model' { 'Quest 2'; break }
        'ro.build.version.release' { '14'; break }
        'ro.build.fingerprint' { 'mock/build'; break }
        'ro.build.version.security_patch' { '2026-01-01'; break }
    }
} elseif ($command[0] -eq 'shell' -and $command[1] -eq 'setprop') {
    if ($command[2] -eq 'debug.openxrvulkanlab.mode') {
        Set-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'mode.txt') -Value $command[3]
    }
    if ($command[2] -eq 'debug.openxrvulkanlab.foveation') {
        Set-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'foveation.txt') -Value $command[3]
    }
    if ($command[2] -eq 'debug.openxrvulkanlab.validation') {
        Set-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'validation.txt') -Value $command[3]
    }
} elseif ($command[0] -eq 'shell' -and $command[1] -eq 'settings') {
    if ($command[4] -eq 'enable_gpu_debug_layers' -and $env:MOCK_EXTERNAL_LAYER -eq '2') { '1' }
    elseif ($command[4] -eq 'gpu_debug_app' -and $env:MOCK_EXTERNAL_LAYER -eq '2') { 'dev.niyy.openxrvulkanlab' }
    elseif ($command[4] -eq 'gpu_debug_layers' -and $env:MOCK_EXTERNAL_LAYER -eq '2') { 'VK_LAYER_KHRONOS_validation' }
    else { 'null' }
} elseif ($command[0] -eq 'logcat' -and $command[1] -eq '-d') {
    if ($env:MOCK_LOGCAT_FAILURE -eq '1') { exit 3 }
    $mode = (Get-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'mode.txt')).Trim()
    $foveation = (Get-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'foveation.txt')).Trim()
    if ($env:MOCK_ADB_MISMATCH -eq '1') { $foveation = 'off' }
    $scene = if ($env:MOCK_SCENE_MISMATCH -eq '1') { 'single-cube' } else { 'grid-100' }
    $validation = (Get-Content -LiteralPath (Join-Path $env:MOCK_ADB_DIR 'validation.txt')).Trim()
    if ($env:MOCK_VALIDATION_MISMATCH -eq '1') { $validation = 'on' }
    "I OpenXRVulkanLab: Vulkan validation: requested=$validation selected=$validation layer=none"
    "I OpenXRVulkanLab: Benchmark config: $mode cubes=100 foveation=$foveation scene=$scene"
    "I OpenXRVulkanLab: Benchmark complete: rendered=2100 warmup=300 measured=1800 scene=$scene"
} elseif ($command[0] -eq 'shell' -and $command[1] -eq 'dumpsys') {
    'Thermal Status: 0'
}
exit 0
