. (Join-Path $PSScriptRoot 'Common.ps1')

$integrationRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$summaryPath = Join-Path $integrationRoot 'results\profiling_summary.csv'
$chartRoot = Join-Path $integrationRoot 'graphs'
if (-not (Test-Path -LiteralPath $summaryPath)) {
    throw 'Falta results/profiling_summary.csv. Ejecute Run-Profiling.ps1 primero.'
}
[IO.Directory]::CreateDirectory($chartRoot) | Out-Null
$data = @(Import-Csv -LiteralPath $summaryPath)
$culture = [Globalization.CultureInfo]::InvariantCulture

function Escape-Xml([string]$Text) { [Security.SecurityElement]::Escape($Text) }
function Number([double]$Value) { $Value.ToString('0.###', $culture) }

function New-ScatterSvg {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$XLabel,
        [Parameter(Mandatory)][string]$YLabel,
        [Parameter(Mandatory)][object[]]$Series
    )
    $width=1200; $height=720; $left=105; $right=40; $top=80; $bottom=100
    $plotWidth=$width-$left-$right; $plotHeight=$height-$top-$bottom
    $points = @($Series | ForEach-Object { $_.Points })
    $xMax = [Math]::Max(1.0, [double](($points | Measure-Object X -Maximum).Maximum))
    $yMax = [Math]::Max(1.0, [double](($points | Measure-Object Y -Maximum).Maximum) * 1.08)
    $svg = [Text.StringBuilder]::new()
    [void]$svg.AppendLine("<svg xmlns=`"http://www.w3.org/2000/svg`" width=`"$width`" height=`"$height`" viewBox=`"0 0 $width $height`">")
    [void]$svg.AppendLine('<rect width="100%" height="100%" fill="white"/>')
    [void]$svg.AppendLine("<text x=`"$($width/2)`" y=`"38`" text-anchor=`"middle`" font-family=`"Segoe UI, sans-serif`" font-size=`"25`" font-weight=`"600`">$(Escape-Xml $Title)</text>")
    for($i=0;$i -le 5;++$i){
        $x=$left+$plotWidth*$i/5; $y=$top+$plotHeight-$plotHeight*$i/5
        [void]$svg.AppendLine("<line x1=`"$(Number $x)`" y1=`"$top`" x2=`"$(Number $x)`" y2=`"$($top+$plotHeight)`" stroke=`"#e5e7eb`"/>")
        [void]$svg.AppendLine("<line x1=`"$left`" y1=`"$(Number $y)`" x2=`"$($left+$plotWidth)`" y2=`"$(Number $y)`" stroke=`"#e5e7eb`"/>")
        [void]$svg.AppendLine("<text x=`"$(Number $x)`" y=`"$($top+$plotHeight+25)`" text-anchor=`"middle`" font-family=`"Segoe UI, sans-serif`" font-size=`"13`">$(Number ($xMax*$i/5))</text>")
        [void]$svg.AppendLine("<text x=`"$($left-12)`" y=`"$(Number ($y+5))`" text-anchor=`"end`" font-family=`"Segoe UI, sans-serif`" font-size=`"13`">$(Number ($yMax*$i/5))</text>")
    }
    [void]$svg.AppendLine("<line x1=`"$left`" y1=`"$top`" x2=`"$left`" y2=`"$($top+$plotHeight)`" stroke=`"#111827`" stroke-width=`"2`"/>")
    [void]$svg.AppendLine("<line x1=`"$left`" y1=`"$($top+$plotHeight)`" x2=`"$($left+$plotWidth)`" y2=`"$($top+$plotHeight)`" stroke=`"#111827`" stroke-width=`"2`"/>")
    [void]$svg.AppendLine("<text x=`"$($left+$plotWidth/2)`" y=`"$($height-35)`" text-anchor=`"middle`" font-family=`"Segoe UI, sans-serif`" font-size=`"17`">$(Escape-Xml $XLabel)</text>")
    [void]$svg.AppendLine("<text x=`"28`" y=`"$($top+$plotHeight/2)`" text-anchor=`"middle`" transform=`"rotate(-90 28 $($top+$plotHeight/2))`" font-family=`"Segoe UI, sans-serif`" font-size=`"17`">$(Escape-Xml $YLabel)</text>")
    $legendX=$left+15; $legendY=$top+20
    foreach($seriesItem in $Series){
        [void]$svg.AppendLine("<circle cx=`"$legendX`" cy=`"$legendY`" r=`"6`" fill=`"$($seriesItem.Color)`"/><text x=`"$($legendX+13)`" y=`"$($legendY+5)`" font-family=`"Segoe UI, sans-serif`" font-size=`"14`">$(Escape-Xml $seriesItem.Name)</text>")
        $legendY += 22
        foreach($point in $seriesItem.Points){
            $px=$left+$plotWidth*[double]$point.X/$xMax
            $py=$top+$plotHeight-$plotHeight*[double]$point.Y/$yMax
            [void]$svg.AppendLine("<circle cx=`"$(Number $px)`" cy=`"$(Number $py)`" r=`"6`" fill=`"$($seriesItem.Color)`" stroke=`"white`" stroke-width=`"1.5`"><title>$(Escape-Xml $point.Label): x=$(Number $point.X), y=$(Number $point.Y)</title></circle>")
        }
    }
    [void]$svg.AppendLine("<text x=`"$($width-35)`" y=`"$($height-18)`" text-anchor=`"end`" font-family=`"Segoe UI, sans-serif`" font-size=`"11`" fill=`"#4b5563`">Medianas de ejecuciones Release x64; ver CSV para datos originales.</text>")
    [void]$svg.AppendLine('</svg>')
    [IO.File]::WriteAllText($Path,$svg.ToString(),[Text.UTF8Encoding]::new($false))
}

function New-BackendSeries([string]$Property,[double]$Divisor=1.0) {
    @(
        [pscustomobject]@{Name='Normal (referencia)';Color='#2563eb';Points=@($data|Where-Object backend -eq 'reference'|ForEach-Object{[pscustomobject]@{X=[double]$_.source_bytes;Y=[double]$_.$Property/$Divisor;Label="$($_.family)-$($_.size)"}})},
        [pscustomobject]@{Name='Optimizado (Chaitin)';Color='#dc2626';Points=@($data|Where-Object backend -eq 'optimized'|ForEach-Object{[pscustomobject]@{X=[double]$_.source_bytes;Y=[double]$_.$Property/$Divisor;Label="$($_.family)-$($_.size)"}})}
    )
}

New-ScatterSvg -Path (Join-Path $chartRoot '01_tiempo_compilacion.svg') -Title 'Tiempo de compilación por tamaño de fuente' -XLabel 'Tamaño fuente (bytes)' -YLabel 'Mediana (ms)' -Series (New-BackendSeries 'compile_total_median_ns' 1000000)
New-ScatterSvg -Path (Join-Path $chartRoot '02_instrucciones_fisicas.svg') -Title 'Instrucciones físicas emitidas' -XLabel 'Tamaño fuente (bytes)' -YLabel 'Instrucciones' -Series (New-BackendSeries 'physical_instructions')
New-ScatterSvg -Path (Join-Path $chartRoot '03_accesos_memoria_estaticos.svg') -Title 'Accesos estáticos a memoria' -XLabel 'Tamaño fuente (bytes)' -YLabel 'LOAD + STORE' -Series (New-BackendSeries 'static_memory_accesses')
New-ScatterSvg -Path (Join-Path $chartRoot '04_memoria_declarada.svg') -Title 'Memoria declarada por el backend' -XLabel 'Tamaño fuente (bytes)' -YLabel 'Slots MEM' -Series (New-BackendSeries 'memory_slots')
New-ScatterSvg -Path (Join-Path $chartRoot '05_tiempo_vm.svg') -Title 'Tiempo externo de ejecución en la VM' -XLabel 'Tamaño fuente (bytes)' -YLabel 'Mediana (ms)' -Series (New-BackendSeries 'vm_median_ns' 1000000)
$optimizerSeries = @([pscustomobject]@{Name='Backend optimizado';Color='#7c3aed';Points=@($data|Where-Object backend -eq 'optimized'|ForEach-Object{[pscustomobject]@{X=[double]$_.temporaries;Y=[double]$_.backend_median_ns/1000000;Label="$($_.family)-$($_.size), aristas=$($_.interference_edges)"}})})
New-ScatterSvg -Path (Join-Path $chartRoot '06_costo_optimizador.svg') -Title 'Costo del backend optimizado según temporales' -XLabel 'Temporales virtuales' -YLabel 'Mediana del backend (ms)' -Series $optimizerSeries

Write-Host 'CHARTS_GENERATED=6'
