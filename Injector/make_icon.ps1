param([string]$Out = "icon.ico")
Add-Type -AssemblyName System.Drawing

function New-DiceImage([int]$SizePx) {
    $s = [double]$SizePx
    $iw = [int]$s
    $bmp = New-Object System.Drawing.Bitmap($iw, $iw, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [float]($s * 0.03)
    $rw = [float]($s - 2 * $pad)
    $rad = [float]($s * 0.22)
    $rect = New-Object System.Drawing.RectangleF($pad, $pad, $rw, $rw)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc([float]$rect.X, [float]$rect.Y, [float]($rad * 2), [float]($rad * 2), 180.0, 90.0)
    $path.AddArc([float]($rect.Right - $rad * 2), [float]$rect.Y, [float]($rad * 2), [float]($rad * 2), 270.0, 90.0)
    $path.AddArc([float]($rect.Right - $rad * 2), [float]($rect.Bottom - $rad * 2), [float]($rad * 2), [float]($rad * 2), 0.0, 90.0)
    $path.AddArc([float]$rect.X, [float]($rect.Bottom - $rad * 2), [float]($rad * 2), [float]($rad * 2), 90.0, 90.0)
    $path.CloseFigure()

    $c1 = [System.Drawing.Color]::FromArgb(255, 92, 155, 255)
    $c2 = [System.Drawing.Color]::FromArgb(255, 24, 72, 190)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $c1, $c2, 45.0)
    $g.FillPath($brush, $path)
    $penW = [float]($s * 0.03)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(130, 255, 255, 255), $penW)
    $g.DrawPath($pen, $path)

    $dotBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 244, 248, 255))
    $off = [float]($s * 0.26)
    $r = [float]($s * 0.075)
    $cr = [float]($s * 0.09)
    $cx = [float]($s / 2.0)
    $d1x = [float]$off;            $d1y = [float]$off
    $d2x = [float]($s - $off);     $d2y = [float]$off
    $d3x = [float]$off;            $d3y = [float]($s - $off)
    $d4x = [float]($s - $off);     $d4y = [float]($s - $off)
    $dots = @()
    $dots += , @($d1x, $d1y)
    $dots += , @($d2x, $d2y)
    $dots += , @($d3x, $d3y)
    $dots += , @($d4x, $d4y)
    foreach ($d in $dots) {
        $dx = [float]($d[0] - $r)
        $dy = [float]($d[1] - $r)
        $dr = [float](2 * $r)
        $g.FillEllipse($dotBrush, $dx, $dy, $dr, $dr)
    }
    $g.FillEllipse($dotBrush, [float]($cx - $cr), [float]($cx - $cr), [float](2 * $cr), [float](2 * $cr))

    $g.Dispose()
    return $bmp
}

$sizes = @(16, 24, 32, 48, 64, 256)
$images = @()
foreach ($s in $sizes) {
    $bmp = New-DiceImage $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += , @($s, $ms.ToArray())
    $bmp.Dispose()
}

$fs = New-Object System.IO.FileStream($Out, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $s = $img[0]
    $data = $img[1]
    $dim = 0
    if ($s -lt 256) { $dim = $s }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($img in $images) {
    $bw.Write([Byte[]]$img[1])
}
$bw.Close()
$fs.Close()
Write-Host "Icon written: $Out"
