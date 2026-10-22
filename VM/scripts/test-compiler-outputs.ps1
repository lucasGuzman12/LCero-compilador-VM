[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$ProjectRoot,

    [string]$TestsRoot,

    [string]$Executable,

    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 10,

    [ValidateRange(0, 1000000000)]
    [int]$MaxSteps = 100000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-NativeArgument {
    param([string]$Value)

    # La comilla conserva rutas Windows con espacios y Unicode como un unico
    # argumento de la CLI; no modifica los bytes del lenguaje intermedio.
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Test-ByteEquality {
    param([byte[]]$Left, [byte[]]$Right)

    # La salida de PRINT forma parte de la semantica observable. Se compara
    # como bytes para distinguir LF (0A) de una traduccion accidental a CRLF.
    return [Convert]::ToBase64String($Left) -ceq
        [Convert]::ToBase64String($Right)
}

function Format-Bytes {
    param([byte[]]$Bytes)

    if ($Bytes.Count -eq 0) { return '<vacio>' }
    return [BitConverter]::ToString($Bytes)
}

function Invoke-VmCaptured {
    param([string[]]$Arguments)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:ExecutablePath
    $startInfo.Arguments = [string]::Join(' ', $Arguments)
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $stdout = [IO.MemoryStream]::new()
    $stderr = [IO.MemoryStream]::new()
    $timedOut = $false

    try {
        if (-not $process.Start()) {
            throw 'CreateProcess no inicio la VM.'
        }
        $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)

        # Este timeout protege el runner. No es el presupuesto k de la maquina
        # ni constituye una decision sobre terminacion del programa formal.
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            $process.Kill()
        }
        $process.WaitForExit()
        $stdoutCopy.Wait()
        $stderrCopy.Wait()

        return [PSCustomObject]@{
            ExitCode = if ($timedOut) { $null } else { $process.ExitCode }
            TimedOut = $timedOut
            Stdout = $stdout.ToArray()
            Stderr = [Text.Encoding]::UTF8.GetString($stderr.ToArray())
        }
    }
    finally {
        $stdout.Dispose()
        $stderr.Dispose()
        $process.Dispose()
    }
}

function Get-TerminalFields {
    param([string]$DiagnosticText)

    return [PSCustomObject]@{
        Status = if ($DiagnosticText -match 'LCERO_STATUS=([A-Z_]+)') {
            $Matches[1]
        } else { 'AUSENTE' }
        Steps = if ($DiagnosticText -match 'LCERO_STEPS=([0-9]+)') {
            $Matches[1]
        } else { '?' }
        Pc = if ($DiagnosticText -match '(?:^|\s)PC=([0-9]+)') {
            $Matches[1]
        } else { '?' }
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectRoot "build\bin\$Configuration\lcero-vm.exe"
}
$script:ExecutablePath = (Resolve-Path -LiteralPath $Executable).Path

if ([string]::IsNullOrWhiteSpace($TestsRoot)) {
    $workspaceRoot = Split-Path -Parent $ProjectRoot
    $integrated = Join-Path $ProjectRoot 'TestsParaVM'
    $integratedNested = Join-Path $integrated 'TestsParaVM'
    $outer = Join-Path $workspaceRoot 'TestsParaVM'
    $nested = Join-Path $outer 'TestsParaVM'
    if (Test-Path -LiteralPath (Join-Path $integrated 'prueba_1.lci')) {
        $TestsRoot = $integrated
    } elseif (Test-Path -LiteralPath (Join-Path $integratedNested 'prueba_1.lci')) {
        $TestsRoot = $integratedNested
    } elseif (Test-Path -LiteralPath (Join-Path $outer 'prueba_1.lci')) {
        $TestsRoot = $outer
    } elseif (Test-Path -LiteralPath (Join-Path $nested 'prueba_1.lci')) {
        $TestsRoot = $nested
    } else {
        throw "No se localizaron los seis .lci dentro del proyecto ni bajo $outer"
    }
}
$TestsRoot = (Resolve-Path -LiteralPath $TestsRoot).Path

# Las expectativas provienen de Resultados Esperados.png. Normal y optimizado
# deben ser observationalmente equivalentes aunque P, MEM y k sean distintos.
$cases = @(
    [PSCustomObject]@{ Name = 'prueba_1.lci'; Lines = @('38', '3', '-3', '26') },
    [PSCustomObject]@{ Name = 'prueba_1_optimizada.lci'; Lines = @('38', '3', '-3', '26') },
    [PSCustomObject]@{ Name = 'prueba_2.lci'; Lines = @('100', '400', '600', '700') },
    [PSCustomObject]@{ Name = 'prueba_2_optimizada.lci'; Lines = @('100', '400', '600', '700') },
    [PSCustomObject]@{ Name = 'prueba_3.lci'; Lines = @('60', '40', '5', '4') },
    [PSCustomObject]@{ Name = 'prueba_3_optimizada.lci'; Lines = @('60', '40', '5', '4') }
)

$artifactRoot = Join-Path $ProjectRoot "build\tests-para-vm\$Configuration"
$null = New-Item -ItemType Directory -Path $artifactRoot -Force
$failed = 0

for ($index = 0; $index -lt $cases.Count; $index++) {
    $case = $cases[$index]
    $programPath = Join-Path $TestsRoot $case.Name
    if (-not (Test-Path -LiteralPath $programPath -PathType Leaf)) {
        Write-Host ("[{0}/6] FAIL {1}: archivo ausente" -f ($index + 1), $case.Name)
        $failed++
        continue
    }

    # --check decide pertenencia al lenguaje RI sin ejecutar transiciones.
    $quotedProgram = Quote-NativeArgument $programPath
    $check = Invoke-VmCaptured -Arguments @('--check', $quotedProgram)
    $checkOk = -not $check.TimedOut -and $check.ExitCode -eq 0 -and
        $check.Stderr -match 'LCERO_CHECK=VALID'

    # La ejecucion usa un presupuesto explicito. La comparacion exige HALT y
    # la secuencia O exacta; no exige un k fijo porque el compilador puede
    # producir otra RI semanticamente equivalente con diferente cantidad de pasos.
    $run = Invoke-VmCaptured -Arguments @(
        '--max-steps', $MaxSteps.ToString(), $quotedProgram)
    $fields = Get-TerminalFields -DiagnosticText $run.Stderr
    $expectedBytes = [Text.Encoding]::UTF8.GetBytes(
        ([string]::Join("`n", $case.Lines) + "`n"))
    $bytesOk = Test-ByteEquality -Left $run.Stdout -Right $expectedBytes
    $runOk = -not $run.TimedOut -and $run.ExitCode -eq 0 -and
        $fields.Status -ceq 'HALT' -and $bytesOk -and
        -not ($run.Stdout -contains 13)

    [IO.File]::WriteAllBytes(
        (Join-Path $artifactRoot ($case.Name + '.stdout')), $run.Stdout)
    [IO.File]::WriteAllText(
        (Join-Path $artifactRoot ($case.Name + '.stderr')),
        $run.Stderr, [Text.UTF8Encoding]::new($false))

    if ($checkOk -and $runOk) {
        Write-Host ("[{0}/6] PASS {1} status={2} k={3} pc={4} stdout={5}" -f `
            ($index + 1), $case.Name, $fields.Status, $fields.Steps,
            $fields.Pc, (Format-Bytes $run.Stdout))
    } else {
        $failed++
        Write-Host ("[{0}/6] FAIL {1} check={2} exit={3} status={4} stdout_esperado={5} stdout_real={6}" -f `
            ($index + 1), $case.Name, $checkOk, $run.ExitCode,
            $fields.Status, (Format-Bytes $expectedBytes),
            (Format-Bytes $run.Stdout))
        if (-not [string]::IsNullOrWhiteSpace($check.Stderr)) {
            Write-Host ('  CHECK_STDERR=' + $check.Stderr.Trim())
        }
        if (-not [string]::IsNullOrWhiteSpace($run.Stderr)) {
            Write-Host ('  RUN_STDERR=' + $run.Stderr.Trim())
        }
    }
}

Write-Host ("COMPILER_OUTPUT_TOTAL=6 PASS={0} FAIL={1} CONFIG={2}" -f `
    (6 - $failed), $failed, $Configuration)
Write-Host ("ARTIFACTS={0}" -f $artifactRoot)
if ($failed -ne 0) { exit 1 }
exit 0
