. (Join-Path $PSScriptRoot 'Common.ps1')

$integrationRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputRoot = Join-Path $integrationRoot 'profiling-cases'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$rows = [Collections.Generic.List[object]]::new()
$utf8 = [Text.UTF8Encoding]::new($false)

function Add-Case {
    param([string]$Id,[string]$Family,[int]$Size,[string]$Source,[string]$Expected)
    $path = Join-Path $outputRoot "$Id.lc"
    [IO.File]::WriteAllText($path, $Source, $utf8)
    $rows.Add([pscustomobject]@{
        case=$Id; family=$Family; size=$Size; source="$Id.lc"; expected_stdout=$Expected
    })
}

function New-BalancedExpression {
    param([int]$Start,[int]$Count)
    if ($Count -eq 1) { return "$Start" }
    $leftCount = [int]($Count / 2)
    $rightCount = $Count - $leftCount
    '((' + (New-BalancedExpression -Start $Start -Count $leftCount) + ') + (' +
        (New-BalancedExpression -Start ($Start + $leftCount) -Count $rightCount) + '))'
}

foreach ($size in @(10,40,80,160)) {
    $builder = [Text.StringBuilder]::new("int x;`nx = 0;`n")
    for ($i=0; $i -lt $size; ++$i) { [void]$builder.AppendLine('x = x + 1;') }
    [void]$builder.AppendLine('print(x);')
    Add-Case -Id "secuencia_$size" -Family 'secuencia' -Size $size -Source $builder.ToString() -Expected "$size`n"
}

foreach ($size in @(10,25,50,100)) {
    $builder = [Text.StringBuilder]::new()
    for ($i=0; $i -lt $size; ++$i) { [void]$builder.AppendLine("int v$i;") }
    [void]$builder.AppendLine('v0 = 0;')
    for ($i=1; $i -lt $size; ++$i) { [void]$builder.AppendLine("v$i = v$($i-1) + 1;") }
    [void]$builder.AppendLine("print(v$($size-1));")
    Add-Case -Id "variables_$size" -Family 'variables' -Size $size -Source $builder.ToString() -Expected "$(($size-1))`n"
}

foreach ($size in @(4,8,16,32)) {
    $expression = New-BalancedExpression -Start 1 -Count $size
    $sum = [int](($size * ($size + 1)) / 2)
    Add-Case -Id "presion_$size" -Family 'presion' -Size $size -Source "print($expression);`n" -Expected "$sum`n"
}

foreach ($size in @(5,10,20,40)) {
    $builder = [Text.StringBuilder]::new("int x;`nx = 0;`n")
    for ($i=1; $i -le $size; ++$i) {
        [void]$builder.AppendLine("if (x < $i) { x = x + 1; }")
    }
    [void]$builder.AppendLine('print(x);')
    Add-Case -Id "control_$size" -Family 'control' -Size $size -Source $builder.ToString() -Expected "$size`n"
}

Write-Utf8Csv -Rows $rows.ToArray() -Path (Join-Path $outputRoot 'profiling_cases.csv')
Write-Host "PROFILING_CASES=$($rows.Count)"
