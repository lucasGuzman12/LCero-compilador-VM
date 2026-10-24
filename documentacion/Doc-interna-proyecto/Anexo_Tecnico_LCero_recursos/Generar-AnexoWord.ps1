param(
    [string]$MarkdownPath = (Join-Path $PSScriptRoot '..\Anexo_Tecnico_LCero_Implementacion_y_Resultados.md'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\Anexo_Tecnico_LCero_Implementacion_y_Resultados.docx')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$markdown = (Resolve-Path -LiteralPath $MarkdownPath).Path
$output = [IO.Path]::GetFullPath($OutputPath)
$base = Split-Path -Parent $markdown
$lines = [IO.File]::ReadAllLines($markdown, [Text.Encoding]::UTF8)

function Clean-Inline([string]$Text) {
    $value = $Text.Trim()
    $value = [regex]::Replace($value, '!\[([^]]+)\]\([^)]+\)', '$1')
    $value = [regex]::Replace($value, '\[([^]]+)\]\(([^)]+)\)', '$1 ($2)')
    $value = $value.Replace('**', '').Replace('__', '').Replace('`', '')
    if ($value.StartsWith('*') -and $value.EndsWith('*') -and $value.Length -gt 1) {
        $value = $value.Substring(1, $value.Length - 2)
    }
    $value
}

function Add-Paragraph($Selection, $Document, [string]$Text, [int]$Style = -1,
                       [bool]$Italic = $false, [int]$Alignment = 3) {
    $Selection.Style = $Document.Styles.Item($Style)
    $Selection.ParagraphFormat.Alignment = $Alignment
    $Selection.Font.Italic = [int]$Italic
    $Selection.TypeText((Clean-Inline $Text))
    $Selection.TypeParagraph()
    $Selection.Font.Italic = 0
}

function Add-Code($Selection, $Document, [string[]]$CodeLines) {
    $Selection.Style = $Document.Styles.Item(-1)
    $Selection.ParagraphFormat.Alignment = 0
    $Selection.ParagraphFormat.LeftIndent = 18
    $Selection.ParagraphFormat.RightIndent = 10
    $Selection.ParagraphFormat.SpaceBefore = 4
    $Selection.ParagraphFormat.SpaceAfter = 7
    $Selection.ParagraphFormat.LineSpacingRule = 0
    $Selection.Font.Name = 'Consolas'
    $Selection.Font.Size = 8.5
    $start = $Selection.Start
    $Selection.TypeText(($CodeLines -join "`v"))
    $end = $Selection.End
    $range = $Document.Range($start, $end)
    $range.Shading.BackgroundPatternColor = 15132390
    $Selection.TypeParagraph()
    $Selection.Font.Name = 'Aptos'
    $Selection.Font.Size = 10.5
    $Selection.ParagraphFormat.LeftIndent = 0
    $Selection.ParagraphFormat.RightIndent = 0
}

function Add-Equation($Selection, $Document, [string]$Expression) {
    $Selection.Style = $Document.Styles.Item(-1)
    $Selection.ParagraphFormat.Alignment = 1
    $Selection.Font.Name = 'Cambria Math'
    $Selection.Font.Size = 11
    $start = $Selection.Start
    $Selection.TypeText($Expression)
    $end = $Selection.End
    $range = $Document.Range($start, $end)
    try {
        $equation = $Document.OMaths.Add($range)
        $equation.BuildUp()
    } catch {
        # La expresión lineal sigue siendo legible si una versión de Word no
        # admite convertir un símbolo Unicode concreto.
    }
    $Selection.SetRange($end, $end)
    $Selection.TypeParagraph()
    $Selection.Font.Name = 'Aptos'
    $Selection.Font.Size = 10.5
}

function Add-MarkdownTable($Selection, $Document, [string[]]$Rows) {
    $parsed = @()
    foreach ($row in $Rows) {
        $cells = @($row.Trim().Trim('|').Split('|') | ForEach-Object { Clean-Inline $_ })
        if ($cells.Count -gt 0 -and -not ($cells | Where-Object { $_ -notmatch '^:?-{3,}:?$' })) {
            continue
        }
        $parsed += ,$cells
    }
    if ($parsed.Count -eq 0) { return }
    $columns = ($parsed | ForEach-Object Count | Measure-Object -Maximum).Maximum
    $range = $Selection.Range
    $table = $Document.Tables.Add($range, $parsed.Count, $columns)
    try { $table.Style = $Document.Styles.Item(-155) } catch { }
    $table.AllowAutoFit = $true
    $table.AutoFitBehavior(2)
    $table.Rows.AllowBreakAcrossPages = 0
    $table.Rows.Item(1).HeadingFormat = -1
    $fontSize = if ($columns -ge 7) { 7.2 } elseif ($columns -ge 5) { 8.0 } else { 8.7 }
    for ($r = 0; $r -lt $parsed.Count; ++$r) {
        for ($c = 0; $c -lt $columns; ++$c) {
            $text = if ($c -lt $parsed[$r].Count) { $parsed[$r][$c] } else { '' }
            $cellRange = $table.Cell($r + 1, $c + 1).Range
            $cellRange.Text = $text
            $cellRange.Font.Name = 'Aptos'
            $cellRange.Font.Size = $fontSize
            $cellRange.ParagraphFormat.SpaceAfter = 0
            $cellRange.ParagraphFormat.Alignment = 0
            if ($r -eq 0) {
                $cellRange.Font.Bold = -1
                $cellRange.Font.Color = 16777215
                $cellRange.Shading.BackgroundPatternColor = 6302017
            }
        }
    }
    $Selection.SetRange($table.Range.End, $table.Range.End)
    $Selection.TypeParagraph()
}

$word = $null
$document = $null
try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $document = $word.Documents.Add()
    $selection = $word.Selection

    $document.PageSetup.PageWidth = 595.3
    $document.PageSetup.PageHeight = 841.9
    $document.PageSetup.TopMargin = 56.7
    $document.PageSetup.BottomMargin = 56.7
    $document.PageSetup.LeftMargin = 62.4
    $document.PageSetup.RightMargin = 56.7

    $normal = $document.Styles.Item(-1)
    $normal.Font.Name = 'Aptos'
    $normal.Font.Size = 10.5
    $normal.ParagraphFormat.SpaceAfter = 5
    $normal.ParagraphFormat.LineSpacingRule = 1
    $normal.ParagraphFormat.LineSpacing = 13
    $normal.ParagraphFormat.Alignment = 3

    foreach ($styleId in @(-2, -3, -4)) {
        $style = $document.Styles.Item($styleId)
        $style.Font.Name = 'Aptos Display'
        $style.Font.Color = 6302017
        $style.Font.Bold = -1
        $style.ParagraphFormat.KeepWithNext = -1
    }
    $document.Styles.Item(-2).Font.Size = 17
    $document.Styles.Item(-2).ParagraphFormat.PageBreakBefore = -1
    $document.Styles.Item(-3).Font.Size = 13
    $document.Styles.Item(-4).Font.Size = 11

    # Portada.
    $selection.ParagraphFormat.Alignment = 1
    $selection.Font.Name = 'Aptos Display'
    $selection.Font.Size = 26
    $selection.Font.Bold = -1
    $selection.TypeText('L-Cero')
    $selection.TypeParagraph()
    $selection.Font.Size = 18
    $selection.TypeText('Compilador y máquina virtual abstracta')
    $selection.TypeParagraph()
    $selection.TypeParagraph()
    $selection.Font.Size = 16
    $selection.Font.Color = 6302017
    $selection.TypeText('ANEXO TÉCNICO DE IMPLEMENTACIÓN, EJECUCIÓN Y EVALUACIÓN EXPERIMENTAL')
    $selection.TypeParagraph()
    $selection.TypeParagraph()
    $selection.Font.Name = 'Aptos'
    $selection.Font.Size = 11
    $selection.Font.Bold = 0
    $selection.Font.Color = 0
    $selection.TypeText('Documento técnico de transferencia para el informe académico definitivo')
    $selection.TypeParagraph()
    $selection.TypeParagraph()
    $selection.TypeText('Fecha: 24 de octubre de 2026')
    $selection.TypeParagraph()
    $selection.TypeParagraph()
    $selection.TypeText('Integrantes: ________________________________________________')
    $selection.TypeParagraph()
    $selection.TypeText('Docente / comisión: _________________________________________')
    $selection.TypeParagraph()
    $selection.TypeParagraph()
    $selection.Font.Italic = -1
    $selection.TypeText('Este anexo documenta la implementación y la evidencia disponible. No reemplaza el informe académico principal.')
    $selection.Font.Italic = 0
    $selection.InsertBreak(7)

    # Índice automático editable.
    $selection.Style = $document.Styles.Item(-2)
    $selection.TypeText('Índice')
    $selection.TypeParagraph()
    $toc = $document.TablesOfContents.Add($selection.Range, $true, 1, 3)
    $selection.SetRange($toc.Range.End, $toc.Range.End)
    $selection.InsertBreak(7)

    # Encabezado y paginación.
    $section = $document.Sections.Item(1)
    $header = $section.Headers.Item(1).Range
    $header.Text = 'L-Cero — Anexo técnico de implementación y resultados'
    $header.Font.Name = 'Aptos'
    $header.Font.Size = 8
    $header.Font.Color = 8421504
    $header.ParagraphFormat.Alignment = 2
    $footer = $section.Footers.Item(1).Range
    $footer.ParagraphFormat.Alignment = 1
    $footer.Text = 'Página '
    $footer.Collapse(0)
    [void]$footer.Fields.Add($footer, 33)
    $footer.InsertAfter(' de ')
    $footer.Collapse(0)
    [void]$footer.Fields.Add($footer, 26)

    $startIndex = 0
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        if ($lines[$i] -match '^# 1\. ') { $startIndex = $i; break }
    }

    $index = $startIndex
    while ($index -lt $lines.Count) {
        $line = $lines[$index]
        if ([string]::IsNullOrWhiteSpace($line) -or $line.Trim() -eq '---') {
            ++$index
            continue
        }

        if ($line -match '^(#{1,3})\s+(.+)$') {
            $level = $matches[1].Length
            $styleId = @(-2, -3, -4)[$level - 1]
            Add-Paragraph $selection $document $matches[2] $styleId $false 0
            ++$index
            continue
        }

        if ($line -match '^```') {
            $code = [Collections.Generic.List[string]]::new()
            ++$index
            while ($index -lt $lines.Count -and $lines[$index] -notmatch '^```') {
                $code.Add($lines[$index])
                ++$index
            }
            if ($index -lt $lines.Count) { ++$index }
            Add-Code $selection $document $code.ToArray()
            continue
        }

        if ($line -match '^\$\$(.+)\$\$$') {
            Add-Equation $selection $document $matches[1]
            ++$index
            continue
        }

        if ($line -match '^!\[([^]]+)\]\(([^)]+)\)$') {
            $caption = $matches[1]
            $relative = $matches[2].Replace('/', [IO.Path]::DirectorySeparatorChar)
            $imagePath = [IO.Path]::GetFullPath((Join-Path $base $relative))
            if (-not (Test-Path -LiteralPath $imagePath)) {
                throw "No existe la figura '$imagePath'."
            }
            $selection.ParagraphFormat.Alignment = 1
            $shape = $selection.InlineShapes.AddPicture($imagePath, $false, $true, $selection.Range)
            if ($shape.Width -gt 450) {
                $ratio = 450.0 / $shape.Width
                $shape.Width = 450
                $shape.Height = $shape.Height * $ratio
            }
            $selection.SetRange($shape.Range.End, $shape.Range.End)
            $selection.TypeParagraph()
            Add-Paragraph $selection $document $caption -35 $false 1
            ++$index
            continue
        }

        if ($line.TrimStart().StartsWith('|') -and $index + 1 -lt $lines.Count -and
            $lines[$index + 1] -match '^\s*\|?\s*:?-{3,}') {
            $rows = [Collections.Generic.List[string]]::new()
            while ($index -lt $lines.Count -and $lines[$index].TrimStart().StartsWith('|')) {
                $rows.Add($lines[$index])
                ++$index
            }
            Add-MarkdownTable $selection $document $rows.ToArray()
            continue
        }

        if ($line -match '^>\s*(.*)$') {
            $parts = [Collections.Generic.List[string]]::new()
            while ($index -lt $lines.Count -and $lines[$index] -match '^>\s*(.*)$') {
                $parts.Add($matches[1])
                ++$index
            }
            $selection.ParagraphFormat.LeftIndent = 22
            $selection.ParagraphFormat.RightIndent = 15
            Add-Paragraph $selection $document ($parts -join ' ') -1 $true 3
            $selection.ParagraphFormat.LeftIndent = 0
            $selection.ParagraphFormat.RightIndent = 0
            continue
        }

        if ($line -match '^\s*-\s+(.+)$') {
            $selection.Style = $document.Styles.Item(-49)
            $selection.ParagraphFormat.Alignment = 0
            $selection.TypeText((Clean-Inline $matches[1]))
            $selection.TypeParagraph()
            ++$index
            continue
        }

        if ($line -match '^\s*\d+\.\s+(.+)$') {
            $selection.Style = $document.Styles.Item(-50)
            $selection.ParagraphFormat.Alignment = 0
            $selection.TypeText((Clean-Inline $matches[1]))
            $selection.TypeParagraph()
            ++$index
            continue
        }

        $paragraph = [Collections.Generic.List[string]]::new()
        while ($index -lt $lines.Count) {
            $candidate = $lines[$index]
            if ([string]::IsNullOrWhiteSpace($candidate) -or
                $candidate -match '^(#{1,3})\s+' -or $candidate -match '^```' -or
                $candidate -match '^\$\$' -or $candidate -match '^!\[' -or
                $candidate.TrimStart().StartsWith('|') -or $candidate -match '^>' -or
                $candidate -match '^\s*-\s+' -or $candidate -match '^\s*\d+\.\s+' -or
                $candidate.Trim() -eq '---') { break }
            $paragraph.Add($candidate.Trim())
            ++$index
        }
        if ($paragraph.Count -gt 0) {
            Add-Paragraph $selection $document ($paragraph -join ' ') -1 $false 3
        } else {
            ++$index
        }
    }

    $toc.Update()
    $document.Fields.Update() | Out-Null
    $document.Repaginate()
    [IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
    $document.SaveAs2($output, 16)
    $pages = $document.ComputeStatistics(2)
    $words = $document.ComputeStatistics(0)
    $document.Save()
    "DOCX=$output"
    "PAGES=$pages"
    "WORDS=$words"
} finally {
    if ($null -ne $document) {
        $document.Close(0)
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document)
    }
    if ($null -ne $word) {
        $word.Quit()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($word)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}

# Validación mínima del paquete Open XML.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($output)
try {
    $required = @('[Content_Types].xml', 'word/document.xml', 'word/styles.xml')
    foreach ($name in $required) {
        if ($null -eq $archive.GetEntry($name)) {
            throw "El DOCX generado no contiene '$name'."
        }
    }
    "OPENXML_ENTRIES=$($archive.Entries.Count)"
} finally {
    $archive.Dispose()
}
