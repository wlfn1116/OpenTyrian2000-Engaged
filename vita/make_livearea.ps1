# Build indexed Vita LiveArea PNGs from switch/icon.jpg via GIF quantization.
Add-Type -AssemblyName System.Drawing

$root   = $PSScriptRoot
$srcPath = Join-Path $root "..\switch\icon.jpg"
$src = $null
if (Test-Path $srcPath) {
    try { $src = [System.Drawing.Image]::FromFile((Resolve-Path $srcPath)) } catch { $src = $null }
}

function Save-IndexedPng([int]$w, [int]$h, [string]$path) {
    $canvas = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::FromArgb(255, 8, 10, 24))
    if ($script:src -ne $null) {
        # Fill the target while preserving aspect ratio and cropping from the center.
        $sr = $script:src.Width / $script:src.Height
        $tr = $w / $h
        if ($sr -gt $tr) { $dh = $h; $dw = [int][math]::Ceiling($h * $sr) }
        else             { $dw = $w; $dh = [int][math]::Ceiling($w / $sr) }
        $dx = [int](($w - $dw) / 2)
        $dy = [int](($h - $dh) / 2)
        $g.DrawImage($script:src, $dx, $dy, $dw, $dh)
    }
    $g.Dispose()

    # Quantize through GIF, then save the reloaded indexed image as PNG.
    $ms = New-Object System.IO.MemoryStream
    $canvas.Save($ms, [System.Drawing.Imaging.ImageFormat]::Gif)
    $canvas.Dispose()
    $ms.Position = 0
    $indexed = New-Object System.Drawing.Bitmap($ms)

    $dir = Split-Path $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $indexed.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)

    $fmt = $indexed.PixelFormat
    $indexed.Dispose()
    $ms.Dispose()
    Write-Host ("wrote {0}  ({1}x{2}, {3})" -f $path, $w, $h, $fmt)
}

Save-IndexedPng 128 128 (Join-Path $root "sce_sys\icon0.png")
Save-IndexedPng 960 544 (Join-Path $root "sce_sys\pic0.png")
Save-IndexedPng 840 500 (Join-Path $root "sce_sys\livearea\contents\bg0.png")
Save-IndexedPng 280 158 (Join-Path $root "sce_sys\livearea\contents\startup.png")

if ($src -ne $null) { $src.Dispose() }
Write-Host "LiveArea assets generated."
