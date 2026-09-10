Add-Type -AssemblyName System.Drawing

$outFile = "D:\STM32CubeMX-6.11.1-Win\project\Source\Core\BSP\lcdfont.h"
$font = New-Object System.Drawing.Font("Consolas", 19, [System.Drawing.FontStyle]::Bold)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('/* ASCII 16x24 bitmap font, generated from Consolas Bold.')
$lines.Add(' * Covers 0x20 - 0x7E. Layout: row-major, 2 bytes per row, MSB = leftmost pixel. */')
$lines.Add('#ifndef __LCDFONT_H')
$lines.Add('#define __LCDFONT_H')
$lines.Add('')
$lines.Add('#include <stdint.h>')
$lines.Add('')
$lines.Add('#define FONT_W    16')
$lines.Add('#define FONT_H    24')
$lines.Add('#define FONT_SIZE (FONT_H * 2)')
$lines.Add('')
$lines.Add('static const uint8_t ascii_16x24[][FONT_SIZE] = {')

for ($c = 32; $c -le 126; $c++) {
    $bmp = New-Object System.Drawing.Bitmap(16, 24)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $g.DrawString([string]([char]$c), $font, $brush, 0, 1)

    $bytes = New-Object System.Collections.Generic.List[string]
    for ($y = 0; $y -lt 24; $y++) {
        $row = 0
        for ($x = 0; $x -lt 16; $x++) {
            $p = $bmp.GetPixel($x, $y)
            if ($p.R -gt 127) { $row = $row -bor (1 -shl (15 - $x)) }
        }
        $hi = ($row -shr 8) -band 0xFF
        $lo = $row -band 0xFF
        $bytes.Add(('0x{0:X2}' -f $hi))
        $bytes.Add(('0x{0:X2}' -f $lo))
    }
    $lines.Add('    {' + ($bytes -join ',') + '},  /* 0x' + ('{0:X2}' -f $c) + ' ' + ([char]$c) + ' */')

    $g.Dispose()
    $bmp.Dispose()
}

$lines.Add('};')
$lines.Add('')
$lines.Add('#endif /* __LCDFONT_H */')

[System.IO.File]::WriteAllLines($outFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "written: $outFile"
