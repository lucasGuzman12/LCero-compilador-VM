[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Entrada de compatibilidad para fases anteriores. La unica implementacion de
# la matriz contractual vive en scripts/test-contracts.ps1, de modo que CTest,
# PowerShell y Visual Studio ejercitan exactamente las mismas comprobaciones.
$runner = Join-Path $ProjectRoot 'scripts\test-contracts.ps1'
& $runner -ProjectRoot $ProjectRoot -Executable $Executable `
    -Configuration Debug
exit $LASTEXITCODE
