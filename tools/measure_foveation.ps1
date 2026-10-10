param(
    [ValidateSet('off', 'low', 'high')]
    [string]$Foveation,
    [string]$OutputPath = '',
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    $OutputPath = "docs/foveation-$Foveation-$(Get-Date -Format 'yyyyMMdd-HHmmss').txt"
}
& (Join-Path $PSScriptRoot 'measure_dual_pass.ps1') -Mode multiview `
    -Foveation $Foveation -OutputPath $OutputPath -TimeoutSeconds $TimeoutSeconds
