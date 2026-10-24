param(
    [ValidateRange(1,20)][int]$Repetitions = 7,
    [ValidateRange(0,10)][int]$Warmups = 2,
    [ValidateSet('Release')][string]$Configuration = 'Release'
)

. (Join-Path $PSScriptRoot 'Common.ps1')

$integrationRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$paths = Resolve-LCeroProjects -RepositoryRoot (Join-Path $integrationRoot '..\..')
$profileExe = Join-Path $paths.CompilerRoot "bin\x64\$Configuration\LCero.Compiler.Tests.exe"
$vmExe = Join-Path $paths.VmRoot "bin\x64\$Configuration\lcero-vm.exe"
$caseRoot = Join-Path $integrationRoot 'profiling-cases'
$artifactRoot = Join-Path $integrationRoot 'artifacts\profiling'
$resultsRoot = Join-Path $integrationRoot 'results'
$transferRoot = Join-Path $paths.VmRoot '.module6-transfer'
[IO.Directory]::CreateDirectory($artifactRoot) | Out-Null
[IO.Directory]::CreateDirectory($resultsRoot) | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $caseRoot 'profiling_cases.csv'))) {
    & (Join-Path $PSScriptRoot 'Generate-ProfilingCases.ps1')
}
foreach ($required in @($profileExe,$vmExe)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Falta '$required'. Compile Release|x64 primero." }
}

function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object)
    if ($ordered.Count % 2 -eq 1) { return [double]$ordered[[int]($ordered.Count / 2)] }
    ([double]$ordered[$ordered.Count / 2 - 1] + [double]$ordered[$ordered.Count / 2]) / 2.0
}

$vmHashesBefore = Get-VmProtectedHashes -VmRoot $paths.VmRoot
$compilerRuns = [Collections.Generic.List[object]]::new()
$vmRuns = [Collections.Generic.List[object]]::new()
$summaries = [Collections.Generic.List[object]]::new()

try {
    foreach ($case in (Import-Csv -LiteralPath (Join-Path $caseRoot 'profiling_cases.csv'))) {
        $source = Join-Path $caseRoot $case.source
        foreach ($backend in @('reference','optimized')) {
            $artifact = Join-Path $artifactRoot "$($case.case).$backend.lci"
            for ($iteration = -$Warmups; $iteration -lt $Repetitions; ++$iteration) {
                $profile = Invoke-CapturedProcess -Executable $profileExe -Arguments @(
                    '--profile-case', $source, $backend, $artifact
                ) -WorkingDirectory $paths.CompilerRoot
                if ($profile.ExitCode -ne 0) { throw "Fallo profiling de $($case.case)/$($backend): $($profile.Stderr)" }
                if ($iteration -ge 0) {
                    $metric = @($profile.Stdout | ConvertFrom-Csv)[0]
                    $compilerRuns.Add([pscustomobject]@{
                        case=$case.case; family=$case.family; size=[int]$case.size; repetition=$iteration
                        backend=$backend; total_ns=[int64]$metric.total_ns
                        invalidation_ns=[int64]$metric.invalidation_ns; read_ns=[int64]$metric.read_ns
                        lexer_ns=[int64]$metric.lexer_ns; parser_ns=[int64]$metric.parser_ns
                        semantic_ns=[int64]$metric.semantic_ns; ir_generation_ns=[int64]$metric.ir_generation_ns
                        backend_ns=[int64]$metric.backend_ns; validation_ns=[int64]$metric.validation_ns
                        publication_ns=[int64]$metric.publication_ns; source_bytes=[int]$metric.source_bytes
                        tokens=[int]$metric.tokens; declarations=[int]$metric.declarations
                        statements=[int]$metric.statements; ir_instructions=[int]$metric.ir_instructions
                        temporaries=[int]$metric.temporaries; labels=[int]$metric.labels
                        physical_instructions=[int]$metric.physical_instructions
                        memory_slots=[int]$metric.memory_slots; loads=[int]$metric.loads; stores=[int]$metric.stores
                        cfg_blocks=[int]$metric.cfg_blocks; cfg_edges=[int]$metric.cfg_edges
                        interference_vertices=[int]$metric.interference_vertices
                        interference_edges=[int]$metric.interference_edges; spills=[int]$metric.spills
                        liveness_iterations=[int]$metric.liveness_iterations
                    })
                }
            }

            [IO.Directory]::CreateDirectory($transferRoot) | Out-Null
            $copied = Join-Path $transferRoot "$($case.case).$backend.lci"
            Copy-Item -LiteralPath $artifact -Destination $copied -Force
            $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash
            $copyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $copied).Hash
            if ($sourceHash -ne $copyHash) { throw "Hash desigual para $($case.case)/$backend." }
            $expected = Normalize-Lf $case.expected_stdout
            for ($iteration = -$Warmups; $iteration -lt $Repetitions; ++$iteration) {
                $vm = Invoke-CapturedProcess -Executable $vmExe -Arguments @('--max-steps','10000000',$copied) -WorkingDirectory $paths.VmRoot
                $match = [regex]::Match($vm.Stderr, 'LCERO_STATUS=([A-Z_]+)\s+LCERO_STEPS=([0-9]+)')
                if ($vm.ExitCode -ne 0 -or -not $match.Success -or $match.Groups[1].Value -ne 'HALT' -or
                    $vm.Stdout.Contains("`r") -or (Normalize-Lf $vm.Stdout) -cne $expected) {
                    throw "La VM no preservo la semantica de $($case.case)/$backend."
                }
                if ($iteration -ge 0) {
                    $vmRuns.Add([pscustomobject]@{
                        case=$case.case; family=$case.family; size=[int]$case.size; repetition=$iteration
                        backend=$backend; elapsed_ns=$vm.ElapsedNanoseconds
                        steps=[int64]$match.Groups[2].Value; status=$match.Groups[1].Value
                        lci_sha256=$sourceHash; copied_sha256=$copyHash
                    })
                }
            }
            Remove-Item -LiteralPath $copied -Force
        }
    }

    foreach ($group in ($compilerRuns | Group-Object case,backend)) {
        $first = $group.Group[0]
        $vmGroup = @($vmRuns | Where-Object { $_.case -eq $first.case -and $_.backend -eq $first.backend })
        $artifact = Join-Path $artifactRoot "$($first.case).$($first.backend).lci"
        $summaries.Add([pscustomobject]@{
            case=$first.case; family=$first.family; size=$first.size; backend=$first.backend
            source_bytes=$first.source_bytes; lci_bytes=(Get-Item -LiteralPath $artifact).Length
            compile_total_median_ns=[int64](Get-Median @($group.Group.total_ns))
            backend_median_ns=[int64](Get-Median @($group.Group.backend_ns))
            vm_median_ns=[int64](Get-Median @($vmGroup.elapsed_ns))
            dynamic_steps=[int64]$vmGroup[0].steps
            ir_instructions=$first.ir_instructions; temporaries=$first.temporaries
            physical_instructions=$first.physical_instructions; loads=$first.loads; stores=$first.stores
            static_memory_accesses=($first.loads + $first.stores); memory_slots=$first.memory_slots
            cfg_blocks=$first.cfg_blocks; cfg_edges=$first.cfg_edges
            interference_vertices=$first.interference_vertices
            interference_edges=$first.interference_edges; spills=$first.spills
            liveness_iterations=$first.liveness_iterations; repetitions=$Repetitions; warmups=$Warmups
        })
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
    Write-Utf8Csv -Rows $compilerRuns.ToArray() -Path (Join-Path $resultsRoot 'compiler_runs.csv')
    Write-Utf8Csv -Rows $vmRuns.ToArray() -Path (Join-Path $resultsRoot 'vm_runs.csv')
    Write-Utf8Csv -Rows $summaries.ToArray() -Path (Join-Path $resultsRoot 'profiling_summary.csv')
    Assert-HashMapsEqual -Before $vmHashesBefore -After (Get-VmProtectedHashes -VmRoot $paths.VmRoot)
}

Write-Host "COMPILER_RUNS=$($compilerRuns.Count) VM_RUNS=$($vmRuns.Count) SUMMARIES=$($summaries.Count)"
Write-Host 'MODULE6_PROFILING=PASS'
