param(
    [Parameter(Mandatory = $true)][string]$LayerPath
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $LayerPath).Path
if ((Split-Path -Leaf $source) -ne 'libVkLayer_khronos_validation.so') {
    throw 'Select the Android arm64-v8a libVkLayer_khronos_validation.so file.'
}
$stream = [IO.File]::OpenRead($source)
try {
    $header = New-Object byte[] 20
    if ($stream.Read($header, 0, $header.Length) -ne $header.Length -or
        $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
        $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or
        $header[4] -ne 2 -or $header[5] -ne 1 -or
        $header[18] -ne 0xb7 -or $header[19] -ne 0) {
        throw 'The layer must be a 64-bit little-endian Android arm64 ELF binary.'
    }
} finally {
    $stream.Dispose()
}
$destination = Join-Path $PSScriptRoot '../app/src/debug/jniLibs/arm64-v8a'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'libVkLayer_khronos_validation.so') -Force
Write-Host "Staged debug-only validation layer: $destination"
