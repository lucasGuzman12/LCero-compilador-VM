Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-LCeroProjects {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
    $directories = @(Get-ChildItem -LiteralPath $root -Directory)
    $compilerCandidates = @($directories | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName 'LCero-Compilador.slnx')
    })
    $vmCandidates = @($directories | Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'LCero-VM.slnx')) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'include\lcero\vm'))
    })
    if ($compilerCandidates.Count -ne 1 -or $vmCandidates.Count -ne 1) {
        throw "No se pudieron identificar de forma univoca el compilador y la VM bajo '$root'."
    }
    [pscustomobject]@{
        RepositoryRoot = $root
        CompilerRoot = $compilerCandidates[0].FullName
        VmRoot = $vmCandidates[0].FullName
    }
}

function Resolve-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'No se encontro vswhere.exe.'
    }
    $installation = & $vswhere -latest -products Microsoft.VisualStudio.Product.Community -property installationPath
    if ([string]::IsNullOrWhiteSpace($installation)) {
        throw 'No se encontro Visual Studio Community.'
    }
    $msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $msbuild)) {
        throw "No se encontro MSBuild en '$msbuild'."
    }
    $msbuild
}

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][string]$Value)
    if ($Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            ++$slashes
        } elseif ($character -eq '"') {
            [void]$builder.Append(('\' * ($slashes * 2 + 1)))
            [void]$builder.Append('"')
            $slashes = 0
        } else {
            if ($slashes -gt 0) { [void]$builder.Append(('\' * $slashes)) }
            [void]$builder.Append($character)
            $slashes = 0
        }
    }
    if ($slashes -gt 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    $builder.ToString()
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter()][string[]]$Arguments = @(),
        [Parameter(Mandatory)][string]$WorkingDirectory
    )
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = (($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) { throw "No se pudo iniciar '$Executable'." }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdoutTask.Wait()
    $stderrTask.Wait()
    $watch.Stop()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdoutTask.Result
        Stderr = $stderrTask.Result
        ElapsedNanoseconds = [int64]($watch.ElapsedTicks * (1000000000.0 / [Diagnostics.Stopwatch]::Frequency))
    }
}

function Write-Utf8Csv {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Rows,
        [Parameter(Mandatory)][string]$Path
    )
    $parent = Split-Path -Parent $Path
    if ($parent) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    $text = if ($Rows.Count -eq 0) { '' } else {
        (($Rows | ConvertTo-Csv -NoTypeInformation) -join "`n") + "`n"
    }
    [IO.File]::WriteAllText($Path, $text, [Text.UTF8Encoding]::new($false))
}

function Normalize-Lf {
    param([AllowEmptyString()][string]$Text)
    $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Get-VmProtectedHashes {
    param([Parameter(Mandatory)][string]$VmRoot)
    $excluded = '\\(build|bin|obj|out|vcpkg_installed|\.vs|\.module6-transfer)(\\|$)'
    $hashes = @{}
    Get-ChildItem -LiteralPath $VmRoot -File -Recurse | Where-Object {
        $_.FullName.Substring($VmRoot.Length) -notmatch $excluded -and
        $_.Extension -ne '.user'
    } | ForEach-Object {
        $relative = $_.FullName.Substring($VmRoot.Length).TrimStart('\')
        $hashes[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
    }
    $hashes
}

function Assert-HashMapsEqual {
    param(
        [Parameter(Mandatory)][hashtable]$Before,
        [Parameter(Mandatory)][hashtable]$After
    )
    $allKeys = @($Before.Keys + $After.Keys | Sort-Object -Unique)
    $changes = @($allKeys | Where-Object {
        -not $Before.ContainsKey($_) -or -not $After.ContainsKey($_) -or $Before[$_] -ne $After[$_]
    })
    if ($changes.Count -ne 0) {
        throw "La prueba modifico archivos protegidos de la VM: $($changes -join ', ')"
    }
}
