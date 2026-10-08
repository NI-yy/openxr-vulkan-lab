param(
    [string]$OutputPath = '',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'measure_dual_pass.ps1') -Mode multiview `
    -OutputPath $OutputPath -TimeoutSeconds $TimeoutSeconds
