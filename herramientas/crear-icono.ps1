# Dibuja el ícono de Mapache (res\app.ico): cuadrado redondeado verde (#1D9E75) con una carita de
# mapache blanca (orejas, cara y el antifaz oscuro). Guarda varios tamaños en un .ico con imágenes PNG.
#
# Uso: powershell -ExecutionPolicy Bypass -File herramientas\crear-icono.ps1 [-Vista <carpeta para ver las imágenes>]
param(
    [string]$Salida = (Join-Path $PSScriptRoot '..\res\app.ico'),
    [string]$Vista = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$tamanos = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

function P([single]$x, [single]$y) { New-Object System.Drawing.PointF($x, $y) }

function New-Redondeado([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

# El dibujo en un cuadrado de lado $lado (diseñado en 120 x 120) con la esquina en ($x0, $y0)
function Draw-Logo($g, [single]$x0, [single]$y0, [single]$lado) {
    $k = $lado / 120.0
    function Q([single]$x, [single]$y) { P ($x0 + $x * $k) ($y0 + $y * $k) }

    $verde = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x1D, 0x9E, 0x75))
    $blanco = [System.Drawing.Brushes]::White
    $antifaz = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x23, 0x2B, 0x2F))
    $orejaDentro = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x8F, 0xD9, 0xBF))

    $g.FillPath($verde, (New-Redondeado $x0 $y0 $lado $lado (28 * $k)))

    # Orejas
    $g.FillPolygon($blanco, [System.Drawing.PointF[]]@((Q 18 58), (Q 26 16), (Q 56 38)))
    $g.FillPolygon($blanco, [System.Drawing.PointF[]]@((Q 102 58), (Q 94 16), (Q 64 38)))
    if ($lado -ge 40) {
        $g.FillPolygon($orejaDentro, [System.Drawing.PointF[]]@((Q 27 48), (Q 31 27), (Q 46 39)))
        $g.FillPolygon($orejaDentro, [System.Drawing.PointF[]]@((Q 93 48), (Q 89 27), (Q 74 39)))
    }

    # Cara: ancha arriba y en punta abajo
    $cara = New-Object System.Drawing.Drawing2D.GraphicsPath
    $cara.AddBezier((Q 60 30), (Q 86 30), (Q 106 42), (Q 106 60))
    $cara.AddBezier((Q 106 60), (Q 106 78), (Q 80 100), (Q 60 102))
    $cara.AddBezier((Q 60 102), (Q 40 100), (Q 14 78), (Q 14 60))
    $cara.AddBezier((Q 14 60), (Q 14 42), (Q 34 30), (Q 60 30))
    $cara.CloseFigure()
    $g.FillPath($blanco, $cara)

    # Antifaz: dos manchas unidas por el medio
    $g.FillEllipse($antifaz, $x0 + 17 * $k, $y0 + 48 * $k, 44 * $k, 27 * $k)
    $g.FillEllipse($antifaz, $x0 + 59 * $k, $y0 + 48 * $k, 44 * $k, 27 * $k)
    $g.FillRectangle($antifaz, $x0 + 40 * $k, $y0 + 52 * $k, 40 * $k, 14 * $k)

    # Ojos (más grandes en los íconos chicos para que se vean)
    $r = $(if ($lado -lt 30) { 7.5 } else { 6.0 })
    $g.FillEllipse($blanco, $x0 + (40 - $r) * $k, $y0 + (61 - $r) * $k, 2 * $r * $k, 2 * $r * $k)
    $g.FillEllipse($blanco, $x0 + (80 - $r) * $k, $y0 + (61 - $r) * $k, 2 * $r * $k, 2 * $r * $k)

    # Nariz
    $g.FillEllipse($antifaz, $x0 + 52 * $k, $y0 + 82 * $k, 16 * $k, 9 * $k)
}

function New-Imagen([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap($n, $n, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $margen = [Math]::Max(0.5, $n * 0.03)
    Draw-Logo $g $margen $margen ($n - 2 * $margen)
    $g.Dispose()
    return $bmp
}

$pngs = @()
foreach ($n in $tamanos) {
    $bmp = New-Imagen $n
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , @($n, $ms.ToArray())
    if ($Vista -and $n -in 16, 24, 32, 48, 256) {
        New-Item -ItemType Directory -Force $Vista | Out-Null
        $bmp.Save((Join-Path $Vista ("icono-{0}.png" -f $n)), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $bmp.Dispose(); $ms.Dispose()
}

# Archivo .ico: cabecera, una entrada por tamaño y después las imágenes PNG
$fs = [System.IO.File]::Create([System.IO.Path]::GetFullPath($Salida))
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($p in $pngs) {
    $n = $p[0]; $datos = $p[1]
    $bw.Write([byte]($(if ($n -ge 256) { 0 } else { $n })))
    $bw.Write([byte]($(if ($n -ge 256) { 0 } else { $n })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$datos.Length); $bw.Write([uint32]$offset)
    $offset += $datos.Length
}
foreach ($p in $pngs) { $bw.Write([byte[]]$p[1]) }
$bw.Close(); $fs.Close()
"{0}: {1} tamaños, {2:N0} bytes" -f $Salida, $pngs.Count, (Get-Item $Salida).Length

if ($Vista) {
    # Hoja para revisar: 256, 48, 32, 24 y 16 sobre fondo claro y oscuro
    $hoja = New-Object System.Drawing.Bitmap(420, 560)
    $gv = [System.Drawing.Graphics]::FromImage($hoja)
    $gv.Clear([System.Drawing.Color]::FromArgb(255, 245, 245, 245))
    $gv.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 32, 32, 32))), 0, 280, 420, 280)
    foreach ($fondoY in 10, 290) {
        $xx = 10
        foreach ($n in 256, 48, 32, 24, 16) {
            $img = [System.Drawing.Image]::FromFile((Join-Path $Vista ("icono-{0}.png" -f $n)))
            $yy = $(if ($n -eq 256) { $fondoY } else { $fondoY + 10 })
            if ($n -ne 256) { $xx = 276 + 0; $yy = $fondoY + @{48 = 0; 32 = 60; 24 = 104; 16 = 140 }[$n] }
            $gv.DrawImage($img, $xx, $yy, $n, $n)
            $img.Dispose()
        }
    }
    $gv.Dispose()
    $hoja.Save((Join-Path $Vista 'vista-previa.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $hoja.Dispose()
    "Vista previa: " + (Join-Path $Vista 'vista-previa.png')
}
