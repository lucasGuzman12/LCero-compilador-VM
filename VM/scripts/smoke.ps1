[CmdletBinding()]
param(
    [string]$Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bin\Debug\lcero-vm.exe'),
    [string]$Contracts = (Join-Path (Split-Path -Parent $PSScriptRoot) 'test\contracts')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "No se encontro el ejecutable: $Executable"
}

function Invoke-SmokeCase {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string[]]$RequiredPatterns = @()
    )

    # Cada invocacion comprueba la frontera de CLI. --check reconoce la RI sin
    # transiciones; los casos posicionales si ejercitan la maquina abstracta.
    # Windows PowerShell representa stderr nativo como ErrorRecord. Durante la
    # captura se usa Continue porque el caso negativo DEBE escribir allí; luego
    # se valida el codigo del proceso, que es la semantica observable de la CLI.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $combinedOutput = & $Executable @Arguments 2>&1 | Out-String
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Name devolvio $actualExitCode; se esperaba $ExpectedExitCode. Salida: $combinedOutput"
    }
    foreach ($pattern in $RequiredPatterns) {
        if ($combinedOutput -notmatch $pattern) {
            throw "$Name no contiene el patron '$pattern'. Salida: $combinedOutput"
        }
    }

    Write-Host "[OK] $Name -> exit $actualExitCode"
}

Invoke-SmokeCase -Name 'ayuda' -Arguments @('--help') -ExpectedExitCode 0
Invoke-SmokeCase -Name 'version' -Arguments @('--version') -ExpectedExitCode 0
Invoke-SmokeCase -Name 'opcion invalida' -Arguments @('--opcion-inexistente') -ExpectedExitCode 5
Invoke-SmokeCase -Name 'limite negativo invalido' -Arguments @('--max-steps', '-1', (Join-Path $Contracts 'valid\14_halt_solo.lci')) -ExpectedExitCode 5
Invoke-SmokeCase -Name 'limite no numerico' -Arguments @('--max-steps', 'abc', (Join-Path $Contracts 'valid\14_halt_solo.lci')) -ExpectedExitCode 5
Invoke-SmokeCase -Name 'limites incompatibles' -Arguments @('--max-steps', '1', '--no-step-limit', (Join-Path $Contracts 'valid\14_halt_solo.lci')) -ExpectedExitCode 5
Invoke-SmokeCase -Name 'ruta inexistente' -Arguments @((Join-Path $Contracts 'no_existe.lci')) -ExpectedExitCode 5
Invoke-SmokeCase -Name 'check valido' -Arguments @('--check', (Join-Path $Contracts 'valid\01_hola_registros.lci')) -ExpectedExitCode 0
Invoke-SmokeCase -Name 'etiqueta inexistente' -Arguments @((Join-Path $Contracts 'load_error\30_etiqueta_inexistente.lci')) -ExpectedExitCode 3 -RequiredPatterns @('LCERO_STATUS=ERROR_DE_CARGA', 'LCERO_STEPS=0', 'PC=0', 'CATEGORY=CARGA')
Invoke-SmokeCase -Name 'HALT valido' -Arguments @((Join-Path $Contracts 'valid\01_hola_registros.lci')) -ExpectedExitCode 0 -RequiredPatterns @('LCERO_STATUS=HALT', 'LCERO_STEPS=3', 'CATEGORY=TERMINACION')
Invoke-SmokeCase -Name 'HALT exacto al limite' -Arguments @('--max-steps', '3', (Join-Path $Contracts 'limit\15_halt_exacto_limite.lci')) -ExpectedExitCode 0 -RequiredPatterns @('LCERO_STATUS=HALT', 'LCERO_STEPS=3')
Invoke-SmokeCase -Name 'limite antes de HALT' -Arguments @('--max-steps', '2', (Join-Path $Contracts 'limit\16_limite_antes_halt.lci')) -ExpectedExitCode 4 -RequiredPatterns @('LCERO_STATUS=LIMIT', 'LCERO_STEPS=2', 'CATEGORY=PRESUPUESTO')
Invoke-SmokeCase -Name 'bucle bajo limite' -Arguments @('--max-steps', '4', (Join-Path $Contracts 'limit\17_bucle_bajo_limite.lci')) -ExpectedExitCode 4 -RequiredPatterns @('LCERO_STATUS=LIMIT', 'LCERO_STEPS=4')
Invoke-SmokeCase -Name 'presupuesto cero' -Arguments @('--max-steps', '0', (Join-Path $Contracts 'limit\18_presupuesto_cero.lci')) -ExpectedExitCode 4 -RequiredPatterns @('LCERO_STATUS=LIMIT', 'LCERO_STEPS=0')
Invoke-SmokeCase -Name 'division por cero' -Arguments @((Join-Path $Contracts 'runtime_error\19_division_por_cero.lci')) -ExpectedExitCode 2 -RequiredPatterns @('LCERO_STATUS=ERROR', 'CATEGORY=EJECUCION', 'DIVISION_POR_CERO')
Invoke-SmokeCase -Name 'slot no inicializado' -Arguments @((Join-Path $Contracts 'runtime_error\20_slot_sin_inicializar.lci')) -ExpectedExitCode 2 -RequiredPatterns @('SLOT_NO_INICIALIZADO')
Invoke-SmokeCase -Name 'PRINT no inicializado' -Arguments @((Join-Path $Contracts 'runtime_error\21_print_registro_no_inicializado.lci')) -ExpectedExitCode 2 -RequiredPatterns @('REGISTRO_NO_INICIALIZADO')
Invoke-SmokeCase -Name 'STORE no inicializado' -Arguments @((Join-Path $Contracts 'runtime_error\22_store_registro_no_inicializado.lci')) -ExpectedExitCode 2 -RequiredPatterns @('REGISTRO_NO_INICIALIZADO')
Invoke-SmokeCase -Name 'caida sin HALT' -Arguments @((Join-Path $Contracts 'runtime_error\23_caida_sin_halt.lci')) -ExpectedExitCode 2 -RequiredPatterns @('CAIDA_SIN_HALT')
Invoke-SmokeCase -Name 'salida parcial antes de error' -Arguments @((Join-Path $Contracts 'runtime_error\24_salida_parcial_antes_error.lci')) -ExpectedExitCode 2 -RequiredPatterns @('DIVISION_POR_CERO')

$traceDirectory = Join-Path ([IO.Path]::GetTempPath()) ('lcero smoke trace ' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $traceDirectory
$tracePath = Join-Path $traceDirectory 'bucle observado.log'
try {
    # La traza observa transiciones de C, pero no forma parte de la secuencia O
    # creada por PRINT. El smoke verifica además que el formato físico use LF.
    Invoke-SmokeCase -Name 'traza de bucle' -Arguments @('--trace', $tracePath, (Join-Path $Contracts 'valid\08_bucle_descendente.lci')) -ExpectedExitCode 0 -RequiredPatterns @('LCERO_STATUS=HALT', 'LCERO_STEPS=40')
    if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
        throw 'La ejecucion con --trace no creo el log esperado.'
    }
    $traceBytes = [IO.File]::ReadAllBytes($tracePath)
    $traceText = [Text.Encoding]::UTF8.GetString($traceBytes)
    if ($traceText -notmatch 'LCERO_TRACE 1' -or
        $traceText -notmatch 'TERMINAL status=HALT k=40' -or
        $traceBytes -contains 13) {
        throw 'La traza no contiene el encabezado/terminal esperado o incluye CR.'
    }
    Write-Host '[OK] traza binaria usa UTF-8/LF y conserva HALT k=40'

    $inaccessibleTrace = Join-Path (Join-Path $traceDirectory 'padre inexistente') 'traza.log'
    Invoke-SmokeCase -Name 'ruta de traza inaccesible' -Arguments @('--trace', $inaccessibleTrace, (Join-Path $Contracts 'valid\01_hola_registros.lci')) -ExpectedExitCode 5 -RequiredPatterns @('LCERO_CLI_ERROR=TRAZA', 'TRAZA_INACCESIBLE')
}
finally {
    if (Test-Path -LiteralPath $tracePath -PathType Leaf) {
        Remove-Item -LiteralPath $tracePath
    }
    if (Test-Path -LiteralPath $traceDirectory -PathType Container) {
        # El directorio propio debe estar vacio; no se usa borrado recursivo.
        Remove-Item -LiteralPath $traceDirectory
    }
}

Write-Host 'Smoke test de robustez y trazas conservado en la entrega de etapa 07.'
exit 0
