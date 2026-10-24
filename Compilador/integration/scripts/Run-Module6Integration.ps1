param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$SkipBuild
)

. (Join-Path $PSScriptRoot 'Common.ps1')

$integrationRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$paths = Resolve-LCeroProjects -RepositoryRoot (Join-Path $integrationRoot '..\..')
$compilerExe = Join-Path $paths.CompilerRoot "bin\x64\$Configuration\LCero.Compiler.exe"
$compilerTests = Join-Path $paths.CompilerRoot "bin\x64\$Configuration\LCero.Compiler.Tests.exe"
$vmExe = Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero-vm.exe"
$resultsRoot = Join-Path $integrationRoot 'results'
$artifactRoot = Join-Path $integrationRoot 'artifacts\lci'
$transferRoot = Join-Path $paths.VmRoot '.module6-transfer'
$casesRoot = Join-Path $integrationRoot 'cases'
$manualRoot = Join-Path $integrationRoot 'manual-lci'
[IO.Directory]::CreateDirectory($resultsRoot) | Out-Null
[IO.Directory]::CreateDirectory($artifactRoot) | Out-Null

$vmHashesBefore = Get-VmProtectedHashes -VmRoot $paths.VmRoot
$allResults = [Collections.Generic.List[object]]::new()
$manualResults = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) {
    $failures.Add($Message)
    Write-Host "[FALLO] $Message" -ForegroundColor Red
}

function Invoke-TransferredVm {
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$SourceLci,
        [Parameter(Mandatory)][int]$MaxSteps
    )
    [IO.Directory]::CreateDirectory($transferRoot) | Out-Null
    $copied = Join-Path $transferRoot "$Id.lci"
    Copy-Item -LiteralPath $SourceLci -Destination $copied -Force
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $SourceLci).Hash
    $copyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $copied).Hash
    if ($sourceHash -ne $copyHash) { throw "La copia fisica de '$Id' no conserva los bytes." }
    $check = Invoke-CapturedProcess -Executable $vmExe -Arguments @('--check', $copied) -WorkingDirectory $paths.VmRoot
    $run = Invoke-CapturedProcess -Executable $vmExe -Arguments @('--max-steps', "$MaxSteps", $copied) -WorkingDirectory $paths.VmRoot
    Remove-Item -LiteralPath $copied -Force
    $match = [regex]::Match($run.Stderr, 'LCERO_STATUS=([A-Z_]+)\s+LCERO_STEPS=([0-9]+)')
    [pscustomobject]@{
        SourceHash = $sourceHash
        CopyHash = $copyHash
        CheckExit = $check.ExitCode
        RunExit = $run.ExitCode
        Status = if ($match.Success) { $match.Groups[1].Value } else { 'NO_DISPONIBLE' }
        Steps = if ($match.Success) { [int64]$match.Groups[2].Value } else { -1 }
        Stdout = $run.Stdout
        Stderr = $run.Stderr
        ElapsedNanoseconds = $run.ElapsedNanoseconds
    }
}

try {
    if (-not $SkipBuild) {
        $msbuild = Resolve-MSBuild
        foreach ($build in @(
            [pscustomobject]@{ Root=$paths.CompilerRoot; Solution='LCero-Compilador.slnx' },
            [pscustomobject]@{ Root=$paths.VmRoot; Solution='LCero-VM.slnx' }
        )) {
            $result = Invoke-CapturedProcess -Executable $msbuild -Arguments @(
                $build.Solution, '/t:Build', '/m', "/p:Configuration=$Configuration", '/p:Platform=x64', '/v:minimal'
            ) -WorkingDirectory $build.Root
            if ($result.ExitCode -ne 0) { throw "Fallo la compilacion de $($build.Solution): $($result.Stderr)" }
        }
    }
    foreach ($required in @($compilerExe,$compilerTests,$vmExe)) {
        if (-not (Test-Path -LiteralPath $required)) { throw "Falta el ejecutable '$required'." }
    }

    $legacy = Invoke-CapturedProcess -Executable $compilerTests -WorkingDirectory $paths.CompilerRoot
    if ($legacy.ExitCode -ne 0) { Add-Failure "Suite heredada del compilador: $($legacy.Stdout) $($legacy.Stderr)" }

    $vmLegacyTests = @(
        [pscustomobject]@{ Name='cli'; Path=(Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero_vm_tests.exe"); Arguments=@() },
        [pscustomobject]@{ Name='modelo'; Path=(Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero_vm_model_tests.exe"); Arguments=@() },
        [pscustomobject]@{ Name='loader'; Path=(Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero_vm_loader_tests.exe"); Arguments=@((Join-Path $paths.VmRoot 'test\contracts')) },
        [pscustomobject]@{ Name='interprete'; Path=(Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero_vm_interpreter_tests.exe"); Arguments=@((Join-Path $paths.VmRoot 'test\contracts')) },
        [pscustomobject]@{ Name='trazas'; Path=(Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero_vm_trace_tests.exe"); Arguments=@((Join-Path $paths.VmRoot 'test\contracts')) }
    )
    foreach ($vmTest in $vmLegacyTests) {
        if (-not (Test-Path -LiteralPath $vmTest.Path)) { throw "Falta la prueba de VM '$($vmTest.Path)'." }
        $testResult = Invoke-CapturedProcess -Executable $vmTest.Path -Arguments $vmTest.Arguments -WorkingDirectory $paths.VmRoot
        if ($testResult.ExitCode -ne 0) { Add-Failure "Suite VM $($vmTest.Name): $($testResult.Stdout) $($testResult.Stderr)" }
    }
    $contractRunner = Join-Path $paths.VmRoot 'scripts\test-contracts.ps1'
    $contractResult = Invoke-CapturedProcess -Executable 'powershell.exe' -Arguments @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',$contractRunner,
        '-Configuration',$Configuration,'-ProjectRoot',$paths.VmRoot,'-Executable',$vmExe
    ) -WorkingDirectory $paths.VmRoot
    if ($contractResult.ExitCode -ne 0) { Add-Failure "Suite contractual VM: $($contractResult.Stdout) $($contractResult.Stderr)" }

    foreach ($case in (Import-Csv -LiteralPath (Join-Path $manualRoot 'manual_cases.csv'))) {
        $lci = Join-Path $manualRoot $case.file
        $expected = Normalize-Lf ([IO.File]::ReadAllText((Join-Path $manualRoot $case.expected_stdout)))
        $vm = Invoke-TransferredVm -Id "manual.$($case.id)" -SourceLci $lci -MaxSteps ([int]$case.max_steps)
        $stdoutOk = -not $vm.Stdout.Contains("`r") -and (Normalize-Lf $vm.Stdout) -ceq $expected
        $ok = $vm.CheckExit -eq [int]$case.check_exit -and
              $vm.RunExit -eq [int]$case.run_exit -and
              $vm.Status -eq $case.expected_status -and $stdoutOk
        if (-not $ok) { Add-Failure "ISA manual $($case.id)" }
        $manualResults.Add([pscustomobject]@{
            case=$case.id; sha256=$vm.SourceHash; copied_sha256=$vm.CopyHash
            check_exit=$vm.CheckExit; vm_exit=$vm.RunExit; status=$vm.Status
            steps=$vm.Steps; stdout_match=$stdoutOk; result=if($ok){'PASS'}else{'FAIL'}
        })
    }

    foreach ($case in (Import-Csv -LiteralPath (Join-Path $casesRoot 'e2e_cases.csv'))) {
        $source = Join-Path $casesRoot $case.source
        $expected = Normalize-Lf ([IO.File]::ReadAllText((Join-Path $casesRoot $case.expected_stdout)))
        foreach ($backend in @('reference','optimized')) {
            $artifact = Join-Path $artifactRoot "$($case.id).$backend.lci"
            if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
            $compile = Invoke-CapturedProcess -Executable $compilerExe -Arguments @(
                $source, '-o', $artifact, '--backend', $backend
            ) -WorkingDirectory $paths.CompilerRoot
            if ($compile.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $artifact)) {
                Add-Failure "Compilacion E2E $($case.id)/$backend"
                continue
            }
            $vm = Invoke-TransferredVm -Id "$($case.id).$backend" -SourceLci $artifact -MaxSteps 1000000
            $stdoutOk = -not $vm.Stdout.Contains("`r") -and (Normalize-Lf $vm.Stdout) -ceq $expected
            $ok = $vm.CheckExit -eq 0 -and $vm.RunExit -eq [int]$case.expected_exit -and
                  $vm.Status -eq $case.expected_status -and $stdoutOk
            if (-not $ok) { Add-Failure "E2E $($case.id)/$backend" }
            $allResults.Add([pscustomobject]@{
                case=$case.id; backend=$backend
                source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
                lci_sha256=$vm.SourceHash; copied_sha256=$vm.CopyHash
                compiler_exit=$compile.ExitCode; check_exit=$vm.CheckExit; vm_exit=$vm.RunExit
                status=$vm.Status; steps=$vm.Steps; stdout_match=$stdoutOk
                vm_elapsed_ns=$vm.ElapsedNanoseconds; result=if($ok){'PASS'}else{'FAIL'}
            })
        }
    }

    foreach ($case in (Import-Csv -LiteralPath (Join-Path $casesRoot 'limit_cases.csv'))) {
        $source = Join-Path $casesRoot $case.source
        foreach ($backend in @('reference','optimized')) {
            $artifact = Join-Path $artifactRoot "$($case.id).$backend.lci"
            if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
            $compile = Invoke-CapturedProcess -Executable $compilerExe -Arguments @(
                $source, '-o', $artifact, '--backend', $backend
            ) -WorkingDirectory $paths.CompilerRoot
            if ($compile.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $artifact)) {
                Add-Failure "Compilacion LIMIT $($case.id)/$backend"
                continue
            }
            $vm = Invoke-TransferredVm -Id "$($case.id).$backend" -SourceLci $artifact -MaxSteps ([int]$case.max_steps)
            $stdoutOk = -not $vm.Stdout.Contains("`r") -and (Normalize-Lf $vm.Stdout) -ceq ''
            $ok = $vm.CheckExit -eq 0 -and $vm.RunExit -eq [int]$case.expected_exit -and
                  $vm.Status -eq $case.expected_status -and
                  $vm.Steps -eq [int64]$case.max_steps -and $stdoutOk
            if (-not $ok) { Add-Failure "LIMIT E2E $($case.id)/$backend" }
            $allResults.Add([pscustomobject]@{
                case=$case.id; backend=$backend
                source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
                lci_sha256=$vm.SourceHash; copied_sha256=$vm.CopyHash
                compiler_exit=$compile.ExitCode; check_exit=$vm.CheckExit; vm_exit=$vm.RunExit
                status=$vm.Status; steps=$vm.Steps; stdout_match=$stdoutOk
                vm_elapsed_ns=$vm.ElapsedNanoseconds; result=if($ok){'PASS'}else{'FAIL'}
            })
        }
    }

    foreach ($case in (Import-Csv -LiteralPath (Join-Path $casesRoot 'compiler_error_cases.csv'))) {
        $source = Join-Path $casesRoot $case.source
        foreach ($backend in @('reference','optimized')) {
            $artifact = Join-Path $artifactRoot "$($case.id).$backend.lci"
            if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
            $compile = Invoke-CapturedProcess -Executable $compilerExe -Arguments @(
                $source, '-o', $artifact, '--backend', $backend
            ) -WorkingDirectory $paths.CompilerRoot
            $ok = $compile.ExitCode -ne 0 -and -not (Test-Path -LiteralPath $artifact)
            if (-not $ok) { Add-Failure "Rechazo $($case.id)/$backend" }
            $allResults.Add([pscustomobject]@{
                case=$case.id; backend=$backend
                source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
                lci_sha256='NO_GENERADO'; copied_sha256='NO_COPIADO'
                compiler_exit=$compile.ExitCode; check_exit='NO_EJECUTADO'; vm_exit='NO_EJECUTADO'
                status='RECHAZO_COMPILADOR'; steps='NO_EJECUTADO'; stdout_match=$true
                vm_elapsed_ns='NO_EJECUTADO'; result=if($ok){'PASS'}else{'FAIL'}
            })
        }
    }
}
finally {
    if (Test-Path -LiteralPath $transferRoot) {
        $resolvedTransfer = (Resolve-Path -LiteralPath $transferRoot).Path
        if (-not $resolvedTransfer.StartsWith($paths.VmRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'La carpeta temporal resuelta quedo fuera de la VM.'
        }
        Remove-Item -LiteralPath $resolvedTransfer -Recurse -Force
    }
    Write-Utf8Csv -Rows $manualResults.ToArray() -Path (Join-Path $resultsRoot 'manual_isa_results.csv')
    Write-Utf8Csv -Rows $allResults.ToArray() -Path (Join-Path $resultsRoot 'integration_results.csv')
    $vmHashesAfter = Get-VmProtectedHashes -VmRoot $paths.VmRoot
    Assert-HashMapsEqual -Before $vmHashesBefore -After $vmHashesAfter
}

Write-Host "MANUAL_TOTAL=$($manualResults.Count) INTEGRATION_TOTAL=$($allResults.Count) FAIL=$($failures.Count)"
if ($failures.Count -ne 0) { exit 1 }
Write-Host 'MODULE6_INTEGRATION=PASS'
