[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$ProjectRoot,

    [string]$Executable,

    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 10,

    [string]$CaseId,

    [switch]$SkipAdditionalRegressions
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-NativePath {
    param([string]$Path)

    # Las rutas de Windows no admiten comillas como parte del nombre. Rodear
    # el argumento conserva espacios y Unicode sin alterar los bytes ASCII RI.
    return '"' + $Path + '"'
}

function Test-ByteEquality {
    param(
        [AllowNull()]
        [byte[]]$Left,
        [AllowNull()]
        [byte[]]$Right
    )

    # Base64 es solo una representacion reversible para comparar arreglos en
    # Windows PowerShell 5; no transforma el stdout contractual.
    if ($null -eq $Left) { $Left = [byte[]]@() }
    if ($null -eq $Right) { $Right = [byte[]]@() }
    return [Convert]::ToBase64String($Left) -ceq
        [Convert]::ToBase64String($Right)
}

function Format-Bytes {
    param([AllowNull()][byte[]]$Bytes)

    if ($null -eq $Bytes -or $Bytes.Count -eq 0) { return '<vacio>' }
    $visible = if ($Bytes.Count -gt 32) { $Bytes[0..31] } else { $Bytes }
    $hex = [BitConverter]::ToString($visible)
    if ($Bytes.Count -gt 32) { return $hex + '-...'
    }
    return $hex
}

function Invoke-VmCaptured {
    param(
        [string]$Id,
        [string[]]$Arguments,
        [string]$TemporaryDirectory
    )

    $stdoutPath = Join-Path $TemporaryDirectory ($Id + '.stdout')
    $stderrPath = Join-Path $TemporaryDirectory ($Id + '.stderr')
    $argumentLine = [string]::Join(' ', $Arguments)

    $process = $null
    $stdoutFile = $null
    $stderrFile = $null
    $timedOut = $false
    $startError = $null
    $exitCode = $null
    try {
        # Ambos streams se consumen en paralelo hacia archivos binarios. Esto
        # evita deadlocks sin pasar PRINT por una conversion de texto.
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $script:ExecutablePath
        $startInfo.Arguments = $argumentLine
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true

        $stdoutFile = [IO.File]::Open(
            $stdoutPath, [IO.FileMode]::Create, [IO.FileAccess]::Write,
            [IO.FileShare]::Read)
        $stderrFile = [IO.File]::Open(
            $stderrPath, [IO.FileMode]::Create, [IO.FileAccess]::Write,
            [IO.FileShare]::Read)
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            throw 'CreateProcess no inicio la VM.'
        }
        $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
        $stderrCopy = $process.StandardError.BaseStream.CopyToAsync($stderrFile)

        # Este timeout fisico es distinto del presupuesto semantico N: evita que
        # una regresion del proceso deje colgada toda la suite.
        if (-not $process.WaitForExit($script:TimeoutMilliseconds)) {
            $timedOut = $true
            $process.Kill()
        }
        $process.WaitForExit()
        $stdoutCopy.Wait()
        $stderrCopy.Wait()
        $stdoutFile.Flush()
        $stderrFile.Flush()
        if (-not $timedOut) {
            $exitCode = $process.ExitCode
        }
    }
    catch {
        $startError = $_.Exception.Message
    }
    finally {
        if ($null -ne $stdoutFile) { $stdoutFile.Dispose() }
        if ($null -ne $stderrFile) { $stderrFile.Dispose() }
        if ($null -ne $process) { $process.Dispose() }
    }

    return [PSCustomObject]@{
        TimedOut = $timedOut
        StartError = $startError
        ExitCode = $exitCode
        Stdout = if (Test-Path -LiteralPath $stdoutPath) {
            [IO.File]::ReadAllBytes($stdoutPath)
        } else { [byte[]]@() }
        Stderr = if (Test-Path -LiteralPath $stderrPath) {
            [IO.File]::ReadAllText($stderrPath)
        } else { '' }
    }
}

function Get-DiagnosticFields {
    param([string]$Text)

    $status = if ($Text -match 'LCERO_STATUS=([A-Z_]+)') {
        $Matches[1]
    } else { 'AUSENTE' }
    $steps = if ($Text -match 'LCERO_STEPS=([0-9]+)') {
        $Matches[1]
    } else { $null }
    $pc = if ($Text -match '(?:^|\s)PC=([0-9]+)') {
        $Matches[1]
    } else { $null }
    $category = if ($Text -match 'CATEGORY=([A-Z_]+)') {
        $Matches[1]
    } else { $null }
    $code = if ($Text -match 'CODE=([A-Z_]+)') {
        $Matches[1]
    } else { $null }

    return [PSCustomObject]@{
        Status = $status
        Steps = $steps
        Pc = $pc
        Category = $category
        Code = $code
    }
}

function Test-DescriptiveErrorClass {
    param(
        [string]$ExpectedClass,
        [string]$ActualCode
    )

    if ([string]::IsNullOrWhiteSpace($ExpectedClass)) {
        return $true
    }

    # error_class_expected es descriptivo, no normativo. Estos tres mapeos
    # documentan donde una clase teorica agrupa diagnosticos mas precisos de la
    # implementacion sin cambiar el contrato ni el manifiesto entregado.
    $patterns = @{
        LECTURA_NO_INICIALIZADA = '^(REGISTRO_NO_INICIALIZADO|SLOT_NO_INICIALIZADO)$'
        FORMATO_INVALIDO = '^(FORMATO_INVALIDO|OPERANDO_INVALIDO)$'
        ETIQUETA_INVALIDA = '^(ETIQUETA_INVALIDA|ETIQUETA_RESERVADA)$'
    }
    if ($patterns.ContainsKey($ExpectedClass)) {
        return $ActualCode -match $patterns[$ExpectedClass]
    }
    return $ActualCode -ceq $ExpectedClass
}

function Test-ExpectedExecution {
    param(
        [object]$Run,
        [string]$ExpectedStatus,
        [int]$ExpectedExit,
        [AllowNull()][string]$ExpectedSteps,
        [byte[]]$ExpectedBytes,
        [AllowNull()][string]$ExpectedErrorClass
    )

    $reasons = [Collections.Generic.List[string]]::new()
    $fields = Get-DiagnosticFields -Text $Run.Stderr

    if ($Run.TimedOut) {
        $reasons.Add("timeout externo de $TimeoutSeconds s")
    }
    if ($null -ne $Run.StartError) {
        $reasons.Add('no se inicio el proceso: ' + $Run.StartError)
    }
    if ($Run.ExitCode -ne $ExpectedExit) {
        $reasons.Add("exit esperado=$ExpectedExit real=$($Run.ExitCode)")
    }
    if ($fields.Status -cne $ExpectedStatus) {
        $reasons.Add("estado esperado=$ExpectedStatus real=$($fields.Status)")
    }
    if (-not (Test-ByteEquality -Left $Run.Stdout -Right $ExpectedBytes)) {
        $reasons.Add(('stdout difiere: esperado={0} real={1}' -f `
            (Format-Bytes $ExpectedBytes), (Format-Bytes $Run.Stdout)))
    }
    if ($Run.Stdout -contains 13) {
        $reasons.Add('stdout contiene CR 0D; se exige LF')
    }
    if (-not [string]::IsNullOrEmpty($ExpectedSteps) -and
        $fields.Steps -cne $ExpectedSteps) {
        $reasons.Add("k esperado=$ExpectedSteps real=$($fields.Steps)")
    }
    if ($null -eq $fields.Steps) {
        $reasons.Add('LCERO_STEPS ausente')
    }
    if ($null -eq $fields.Pc) {
        $reasons.Add('PC ausente')
    }
    if ($null -eq $fields.Category) {
        $reasons.Add('CATEGORY ausente')
    }
    if ($ExpectedStatus -ceq 'ERROR_DE_CARGA') {
        if ($fields.Steps -cne '0' -or $fields.Pc -cne '0' -or
            $fields.Category -cne 'CARGA') {
            $reasons.Add('ERROR_DE_CARGA no certifica k=0, pc=0 y CARGA')
        }
        if (@($Run.Stdout).Count -ne 0) {
            $reasons.Add('ERROR_DE_CARGA produjo stdout')
        }
    }
    if (-not (Test-DescriptiveErrorClass `
            -ExpectedClass $ExpectedErrorClass -ActualCode $fields.Code)) {
        $reasons.Add(
            "clase descriptiva=$ExpectedErrorClass codigo=$($fields.Code)")
    }

    return [PSCustomObject]@{
        Passed = $reasons.Count -eq 0
        Fields = $fields
        OutputMatches = Test-ByteEquality -Left $Run.Stdout -Right $ExpectedBytes
        Reasons = $reasons
    }
}

function Write-CaseReport {
    param(
        [string]$Prefix,
        [string]$Id,
        [string]$ExpectedStatus,
        [object]$Evaluation
    )

    $result = if ($Evaluation.Passed) { 'PASS' } else { 'FAIL' }
    $steps = if ($null -eq $Evaluation.Fields.Steps) {
        '-'
    } else { $Evaluation.Fields.Steps }
    $output = if ($Evaluation.OutputMatches) { 'OK' } else { 'DIFIERE' }
    $reason = if ($Evaluation.Passed) {
        '-'
    } else { [string]::Join('; ', $Evaluation.Reasons) }

    # Una unica linea por fixture facilita localizar regresiones en CTest y VS.
    Write-Host ("{0} {1} {2} esperado={3} real={4} k={5} stdout={6} motivo={7}" -f `
        $Prefix, $result, $Id, $ExpectedStatus,
        $Evaluation.Fields.Status, $steps, $output, $reason)
}

function Invoke-Regression {
    param(
        [string]$Id,
        [string[]]$Arguments,
        [string]$ExpectedStatus,
        [int]$ExpectedExit,
        [string]$ExpectedSteps,
        [byte[]]$ExpectedBytes,
        [string]$ExpectedErrorClass,
        [string]$TemporaryDirectory
    )

    $run = Invoke-VmCaptured -Id ('reg_' + $Id) `
        -Arguments $Arguments -TemporaryDirectory $TemporaryDirectory
    $evaluation = Test-ExpectedExecution -Run $run `
        -ExpectedStatus $ExpectedStatus -ExpectedExit $ExpectedExit `
        -ExpectedSteps $ExpectedSteps -ExpectedBytes $ExpectedBytes `
        -ExpectedErrorClass $ExpectedErrorClass
    Write-CaseReport -Prefix '[REGRESION]' -Id $Id `
        -ExpectedStatus $ExpectedStatus -Evaluation $evaluation
    if (-not $evaluation.Passed) {
        $script:FailureCount++
    }
    return $evaluation.Passed
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectRoot (
        "build\bin\$Configuration\lcero-vm.exe")
}
$script:ExecutablePath = [IO.Path]::GetFullPath($Executable)
$script:TimeoutMilliseconds = $TimeoutSeconds * 1000
$script:FailureCount = 0
$script:SuiteCompleted = $false

if (-not (Test-Path -LiteralPath $ProjectRoot -PathType Container)) {
    throw "No existe la raiz del proyecto: $ProjectRoot"
}
if (-not (Test-Path -LiteralPath $script:ExecutablePath -PathType Leaf)) {
    throw "No existe el ejecutable $Configuration`: $script:ExecutablePath"
}

$manifestPath = Join-Path $ProjectRoot 'test\contracts\expected_cases.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "No existe el manifiesto contractual: $manifestPath"
}

# ConvertFrom-Json lee el manifiesto entregado. No se agrega una dependencia
# JSON a C++ porque esta automatizacion pertenece a la infraestructura de test.
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
$selectedCases = @($manifest.cases)
if (-not [string]::IsNullOrWhiteSpace($CaseId)) {
    $selectedCases = @($selectedCases | Where-Object { $_.id -ceq $CaseId })
    if ($selectedCases.Count -ne 1) {
        throw "No existe un caso contractual con id '$CaseId'."
    }
}

$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporary = [IO.Path]::GetFullPath((Join-Path $temporaryBase (
    'lcero_contracts_' + [Guid]::NewGuid().ToString('N'))))
if (-not $temporary.StartsWith(
        $temporaryBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'La carpeta temporal calculada salio de la raiz temporal.'
}
[void](New-Item -ItemType Directory -Path $temporary)

$expectedExitCodes = @{
    HALT = 0
    ERROR = 2
    ERROR_DE_CARGA = 3
    LIMIT = 4
}
$statusCounts = @{
    HALT = 0
    LIMIT = 0
    ERROR = 0
    ERROR_DE_CARGA = 0
}
$passedCases = 0
$caseNumber = 0

try {
    if ([string]::IsNullOrWhiteSpace($CaseId)) {
        $expectedFolders = @{
            valid = 14
            limit = 4
            runtime_error = 6
            load_error = 16
        }
        foreach ($folder in $expectedFolders.Keys) {
            $folderPath = Join-Path $ProjectRoot ('test\contracts\' + $folder)
            $actualCount = @(Get-ChildItem -LiteralPath $folderPath `
                -File -Filter '*.lci').Count
            if ($actualCount -ne $expectedFolders[$folder]) {
                Write-Host ("[FALLO] carpeta {0}: se esperaban {1} .lci y hay {2}" -f `
                    $folder, $expectedFolders[$folder], $actualCount)
                $script:FailureCount++
            }
        }
    }

    foreach ($case in $selectedCases) {
        $caseNumber++
        $inputPath = [IO.Path]::GetFullPath((Join-Path $ProjectRoot (
            [string]$case.path)))
        $expectedOutputPath = [IO.Path]::GetFullPath((Join-Path $ProjectRoot (
            [string]$case.stdout_expected_file)))
        if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
            throw "Falta el fixture entregado: $inputPath"
        }
        if (-not (Test-Path -LiteralPath $expectedOutputPath -PathType Leaf)) {
            throw "Falta la salida esperada entregada: $expectedOutputPath"
        }

        $arguments = [Collections.Generic.List[string]]::new()
        if ($null -ne $case.max_steps) {
            # El valor se pasa literalmente desde JSON. En particular, el caso
            # 17 siempre recibe N=4 y nunca se ejecuta sin proteccion.
            $arguments.Add('--max-steps')
            $arguments.Add([string]$case.max_steps)
        }
        # Para los casos finitos con null basta el presupuesto predeterminado;
        # no se desactiva innecesariamente la proteccion de la CLI.
        $arguments.Add((Quote-NativePath -Path $inputPath))

        $run = Invoke-VmCaptured -Id ([string]$case.id) `
            -Arguments $arguments.ToArray() -TemporaryDirectory $temporary
        $expectedBytes = [IO.File]::ReadAllBytes($expectedOutputPath)
        $expectedStatus = [string]$case.status
        $expectedSteps = if ($null -eq $case.steps_expected) {
            $null
        } else { [string]$case.steps_expected }
        $expectedClass = if ($null -eq $case.error_class_expected) {
            $null
        } else { [string]$case.error_class_expected }
        $evaluation = Test-ExpectedExecution -Run $run `
            -ExpectedStatus $expectedStatus `
            -ExpectedExit ([int]$expectedExitCodes[$expectedStatus]) `
            -ExpectedSteps $expectedSteps -ExpectedBytes $expectedBytes `
            -ExpectedErrorClass $expectedClass

        $prefix = '[{0:D2}/{1:D2}]' -f $caseNumber,$selectedCases.Count
        Write-CaseReport -Prefix $prefix -Id ([string]$case.id) `
            -ExpectedStatus $expectedStatus -Evaluation $evaluation
        if ($evaluation.Passed) {
            $passedCases++
        } else {
            $script:FailureCount++
        }
        $statusCounts[$expectedStatus]++
    }

    if ([string]::IsNullOrWhiteSpace($CaseId)) {
        if ($selectedCases.Count -ne 40 -or
            $statusCounts.HALT -ne 15 -or
            $statusCounts.LIMIT -ne 3 -or
            $statusCounts.ERROR -ne 6 -or
            $statusCounts.ERROR_DE_CARGA -ne 16) {
            Write-Host '[FALLO] La distribucion del manifiesto no es 15/3/6/16.'
            $script:FailureCount++
        }
    }

    if (-not $SkipAdditionalRegressions -and
        [string]::IsNullOrWhiteSpace($CaseId)) {
        $unicodeDirectory = Join-Path $temporary 'rutas con espacios y á'
        [void](New-Item -ItemType Directory -Path $unicodeDirectory)
        $unicodeProgram = Join-Path $unicodeDirectory 'programa válido 01.lci'
        Copy-Item -LiteralPath (Join-Path $ProjectRoot (
            'test\contracts\valid\01_hola_registros.lci')) `
            -Destination $unicodeProgram
        $fiveLf = [IO.File]::ReadAllBytes((Join-Path $ProjectRoot (
            'test\contracts\expected\01_hola_registros.out')))
        [void](Invoke-Regression -Id 'ruta_unicode_y_espacios' `
            -Arguments @((Quote-NativePath $unicodeProgram)) `
            -ExpectedStatus 'HALT' -ExpectedExit 0 -ExpectedSteps '3' `
            -ExpectedBytes $fiveLf -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary)

        $bigInteger = ('1234567890' * 10) -join ''
        $bigProgram = Join-Path $temporary 'inmediato_100_digitos.lci'
        $bigText = "LCERO_IR 1`nMEM 0`nCONST R0, $bigInteger`nPRINT R0`nHALT`n"
        [IO.File]::WriteAllText(
            $bigProgram, $bigText, [Text.UTF8Encoding]::new($false))
        $bigExpected = [Text.Encoding]::ASCII.GetBytes($bigInteger + "`n")
        [void](Invoke-Regression -Id 'inmediato_100_digitos' `
            -Arguments @((Quote-NativePath $bigProgram)) `
            -ExpectedStatus 'HALT' -ExpectedExit 0 -ExpectedSteps '3' `
            -ExpectedBytes $bigExpected -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary)

        $limitProgram = [IO.Path]::GetFullPath((Join-Path $ProjectRoot (
            'test\contracts\limit\15_halt_exacto_limite.lci')))
        [void](Invoke-Regression -Id 'limite_0' `
            -Arguments @('--max-steps', '0', (Quote-NativePath $limitProgram)) `
            -ExpectedStatus 'LIMIT' -ExpectedExit 4 -ExpectedSteps '0' `
            -ExpectedBytes ([byte[]]@()) -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary)
        [void](Invoke-Regression -Id 'limite_1' `
            -Arguments @('--max-steps', '1', (Quote-NativePath $limitProgram)) `
            -ExpectedStatus 'LIMIT' -ExpectedExit 4 -ExpectedSteps '1' `
            -ExpectedBytes ([byte[]]@()) -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary)
        $sevenLf = [Text.Encoding]::ASCII.GetBytes("7`n")
        [void](Invoke-Regression -Id 'limite_3_halt' `
            -Arguments @('--max-steps', '3', (Quote-NativePath $limitProgram)) `
            -ExpectedStatus 'HALT' -ExpectedExit 0 -ExpectedSteps '3' `
            -ExpectedBytes $sevenLf -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary)

        $setProgram = Join-Path $temporary 'reinicio_escribe.lci'
        $readProgram = Join-Path $temporary 'reinicio_lee.lci'
        [IO.File]::WriteAllText($setProgram,
            "LCERO_IR 1`nMEM 0`nCONST R0, 99`nHALT`n",
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($readProgram,
            "LCERO_IR 1`nMEM 0`nPRINT R0`nHALT`n",
            [Text.UTF8Encoding]::new($false))
        $firstFresh = Invoke-Regression -Id 'reinicio_primera_vm' `
            -Arguments @((Quote-NativePath $setProgram)) `
            -ExpectedStatus 'HALT' -ExpectedExit 0 -ExpectedSteps '2' `
            -ExpectedBytes ([byte[]]@()) -ExpectedErrorClass $null `
            -TemporaryDirectory $temporary
        $secondFresh = Invoke-Regression -Id 'reinicio_segunda_vm' `
            -Arguments @((Quote-NativePath $readProgram)) `
            -ExpectedStatus 'ERROR' -ExpectedExit 2 -ExpectedSteps '0' `
            -ExpectedBytes ([byte[]]@()) `
            -ExpectedErrorClass 'REGISTRO_NO_INICIALIZADO' `
            -TemporaryDirectory $temporary
        if ($firstFresh -and $secondFresh) {
            Write-Host '[REGRESION] PASS reinicializacion: R0 no se comparte entre archivos'
        }
    }

    Write-Host ("CONTRACT_TOTAL={0} PASS={1} FAIL={2} HALT={3} LIMIT={4} ERROR={5} ERROR_DE_CARGA={6} CONFIG={7}" -f `
        $selectedCases.Count, $passedCases,
        ($selectedCases.Count - $passedCases), $statusCounts.HALT,
        $statusCounts.LIMIT, $statusCounts.ERROR,
        $statusCounts.ERROR_DE_CARGA, $Configuration)
    $script:SuiteCompleted = $true
}
finally {
    # La ruta fue normalizada y validada dentro de la raiz temporal antes de
    # crearla. Solo esa carpeta unica se elimina recursivamente.
    if ($script:SuiteCompleted -and $script:FailureCount -eq 0 -and
        (Test-Path -LiteralPath $temporary -PathType Container)) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    } elseif (Test-Path -LiteralPath $temporary -PathType Container) {
        Write-Host "FAILED_ARTIFACTS=$temporary"
    }
}

if ($script:FailureCount -ne 0) {
    Write-Host "SUITE_RESULT=FAIL comprobaciones_fallidas=$script:FailureCount"
    exit 1
}

Write-Host 'SUITE_RESULT=PASS'
exit 0
