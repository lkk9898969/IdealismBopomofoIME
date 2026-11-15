# Generates the icons in src/tip/ (16/20/24/32/48 px, PNG-compressed entries):
#   zhuyin.ico  "注"  profile icon (input method list)
#   mode_zh.ico "中"  taskbar mode indicator, Chinese
#   mode_en.ico "英"  taskbar mode indicator, English
#   mode_zh_full.ico / mode_en_full.ico  the same in full-width (全形) mode, orange
# Usage: pwsh -NoProfile -File tools/make_icon.ps1
Add-Type -AssemblyName System.Drawing

function New-GlyphIcon([string]$glyph, [System.Drawing.Color]$back, [string]$out) {
    $sizes = 16, 20, 24, 32, 48
    $pngs = foreach ($s in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap $s, $s
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = 'AntiAlias'
        $g.TextRenderingHint = 'AntiAliasGridFit'
        $g.Clear([System.Drawing.Color]::Transparent)
        $brush = New-Object System.Drawing.SolidBrush $back
        $g.FillRectangle($brush, 0, 0, $s, $s)
        $font = New-Object System.Drawing.Font 'Microsoft JhengHei', ($s * 0.62), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $fmt = New-Object System.Drawing.StringFormat
        $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
        $g.DrawString($glyph, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 0, $s, $s), $fmt)
        $g.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        , $ms.ToArray()
    }
    $fs = [System.IO.File]::Create($out)
    $w = New-Object System.IO.BinaryWriter $fs
    $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]
        $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([UInt16]1); $w.Write([UInt16]32)
        $w.Write([UInt32]$pngs[$i].Length); $w.Write([UInt32]$offset)
        $offset += $pngs[$i].Length
    }
    foreach ($p in $pngs) { $w.Write($p) }
    $w.Close()
    Write-Output "Wrote $out"
}

$dir = Join-Path $PSScriptRoot '..\src\tip'
New-GlyphIcon '注' ([System.Drawing.Color]::FromArgb(255, 30, 100, 200)) (Join-Path $dir 'zhuyin.ico')
New-GlyphIcon '中' ([System.Drawing.Color]::FromArgb(255, 30, 100, 200)) (Join-Path $dir 'mode_zh.ico')
New-GlyphIcon '英' ([System.Drawing.Color]::FromArgb(255, 90, 90, 90)) (Join-Path $dir 'mode_en.ico')
New-GlyphIcon '中' ([System.Drawing.Color]::FromArgb(255, 215, 110, 0)) (Join-Path $dir 'mode_zh_full.ico')
New-GlyphIcon '英' ([System.Drawing.Color]::FromArgb(255, 215, 110, 0)) (Join-Path $dir 'mode_en_full.ico')
