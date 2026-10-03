# Regenerate the checked-in SPIR-V header after editing the GLSL sources.
$ErrorActionPreference = 'Stop'
$shaderDir = Join-Path $PSScriptRoot '..\app\src\main\cpp'
$compiler = (Get-Command glslc -ErrorAction Stop).Source
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('// Generated from cube.vert and cube.frag with glslc.')
$lines.Add('#pragma once')
$lines.Add('#include <cstdint>')
foreach ($entry in @(@('cube.vert', 'kCubeVertexShader'), @('cube.frag', 'kCubeFragmentShader'))) {
    $source = Join-Path $shaderDir $entry[0]
    $output = Join-Path $shaderDir ($entry[0] + '.spv')
    try {
        & $compiler $source -o $output
        if ($LASTEXITCODE -ne 0) { throw "glslc failed for $source" }
        $bytes = [System.IO.File]::ReadAllBytes($output)
        if ($bytes.Length % 4 -ne 0) { throw "Invalid SPIR-V length: $source" }
        $lines.Add("constexpr uint32_t $($entry[1])[] = {")
        for ($i = 0; $i -lt $bytes.Length; $i += 32) {
            $words = [System.Collections.Generic.List[string]]::new()
            for ($j = $i; $j -lt [Math]::Min($i + 32, $bytes.Length); $j += 4) {
                $words.Add(('0x{0:x8}' -f [BitConverter]::ToUInt32($bytes, $j)))
            }
            $lines.Add('    ' + ($words -join ', ') + ',')
        }
        $lines.Add('};')
    } finally {
        Remove-Item -LiteralPath $output -ErrorAction SilentlyContinue
    }
}
[System.IO.File]::WriteAllLines((Join-Path $shaderDir 'cube_shaders.h'), $lines)
