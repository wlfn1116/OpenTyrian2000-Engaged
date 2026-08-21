<#
.SYNOPSIS
Generate the Android, iOS, and macOS app icons from visualc/tyrian2000.ico.

.DESCRIPTION
None of the three build systems can derive a bitmap icon from a .ico, so this writes them
at the fixed sizes each one wants, from the largest frame in the icon file. Scaling is
nearest-neighbour: the source is pixel art, and a smooth filter turns it to mush.

Android gets adaptive-icon foregrounds, whose artwork must stay inside the central 72 of
108 density-independent pixels or a launcher mask will clip it, plus square legacy
bitmaps. iOS gets opaque icons, because alpha in an iOS app icon renders as black and is
rejected by the store tooling. macOS gets an .iconset of the rounded square Apple's icon
grid asks for, which iconutil packs into an .icns during the build.

Re-run after changing the source icon. The output is committed, so a build never needs it.
#>
[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\visualc\tyrian2000.ico'),
    [string]$Root   = (Join-Path $PSScriptRoot '..')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Matches ic_launcher_background in the Android resources, and is the flat backdrop the
# iOS icons are composited onto.
$BackColor = [System.Drawing.Color]::FromArgb(255, 11, 16, 36)

function Get-LargestFrame([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $best = 0
    for ($i = 0; $i -lt $count; $i++) {
        $w = $bytes[6 + $i * 16]
        if ($w -eq 0) { $w = 256 }
        if ($w -gt $best) { $best = $w }
    }
    $icon = New-Object System.Drawing.Icon($path, $best, $best)
    $bmp = $icon.ToBitmap()
    $icon.Dispose()
    return $bmp
}

# Draw $art centred on a $size square, scaled to $coverage of the edge. A transparent
# background keeps the alpha for adaptive foregrounds; otherwise it is filled opaque.
function New-IconBitmap($art, [int]$size, [double]$coverage, [bool]$opaque) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        if ($opaque) { $g.Clear($BackColor) } else { $g.Clear([System.Drawing.Color]::Transparent) }
        $g.InterpolationMode = 'NearestNeighbor'
        $g.PixelOffsetMode = 'Half'
        $edge = [int][Math]::Round($size * $coverage)
        $off = [int][Math]::Round(($size - $edge) / 2.0)
        $g.DrawImage($art, $off, $off, $edge, $edge)
    } finally {
        $g.Dispose()
    }
    return $bmp
}

# Draw $art on the rounded square of Apple's macOS icon grid: an 824-of-1024 plate with a
# corner radius of 22.5% of its edge, centred on a transparent canvas. Unlike iOS, macOS
# expects the icon to carry that shape itself rather than filling the whole square.
function New-MacIconBitmap($art, [int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.SmoothingMode = 'AntiAlias'

        $plate = $size * (824.0 / 1024.0)
        $off = ($size - $plate) / 2.0
        $d = $plate * 0.45  # diameter of the corner arcs

        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddArc($off, $off, $d, $d, 180, 90)
        $path.AddArc($off + $plate - $d, $off, $d, $d, 270, 90)
        $path.AddArc($off + $plate - $d, $off + $plate - $d, $d, $d, 0, 90)
        $path.AddArc($off, $off + $plate - $d, $d, $d, 90, 90)
        $path.CloseFigure()

        $brush = New-Object System.Drawing.SolidBrush($BackColor)
        $g.FillPath($brush, $path)
        $brush.Dispose()
        $path.Dispose()

        $g.InterpolationMode = 'NearestNeighbor'
        $g.PixelOffsetMode = 'Half'
        $edge = [int][Math]::Round($plate * 0.86)
        $artOff = [int][Math]::Round(($size - $edge) / 2.0)
        $g.DrawImage($art, $artOff, $artOff, $edge, $edge)
    } finally {
        $g.Dispose()
    }
    return $bmp
}

function Save-Png($bmp, [string]$path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  {0}" -f (Resolve-Path -Relative $path))
}

$art = Get-LargestFrame $Source
Write-Host ("source: {0} ({1}x{1})" -f $Source, $art.Width)

# Android. The foreground canvas is 108dp with a 72dp safe zone, so the artwork covers
# two thirds of the edge; the legacy square has no mask and can run wider.
Write-Host 'android:'
$androidRes = Join-Path $Root 'android\app\src\main\res'
$densities = @{ 'mdpi' = 48; 'hdpi' = 72; 'xhdpi' = 96; 'xxhdpi' = 144; 'xxxhdpi' = 192 }
foreach ($d in $densities.GetEnumerator()) {
    $legacy = $d.Value
    $adaptive = [int][Math]::Round($legacy * 108.0 / 48.0)
    Save-Png (New-IconBitmap $art $legacy 0.86 $true) `
             (Join-Path $androidRes ("mipmap-{0}\ic_launcher.png" -f $d.Key))
    Save-Png (New-IconBitmap $art $adaptive (72.0 / 108.0) $false) `
             (Join-Path $androidRes ("mipmap-{0}\ic_launcher_foreground.png" -f $d.Key))
}

# iOS, flat icons. Names carry the point size and scale iOS looks for through
# CFBundleIconFiles, the fallback for anything that does not read the asset catalog.
Write-Host 'ios (flat):'
$iosIcons = Join-Path $Root 'ios\icons'
$iosSizes = @{
    'AppIcon60x60@2x.png'     = 120
    'AppIcon60x60@3x.png'     = 180
    'AppIcon76x76@2x.png'     = 152
    'AppIcon83.5x83.5@2x.png' = 167
}
foreach ($f in $iosSizes.GetEnumerator()) {
    Save-Png (New-IconBitmap $art $f.Value 0.86 $true) (Join-Path $iosIcons $f.Key)
}

# iOS asset catalog. One 1024 image per appearance, all with a transparent background so
# the system draws its own material behind the ship rather than a flat slab of navy. The
# tinted appearance is monochrome, which is what iOS expects to colourise.
Write-Host 'ios (asset catalog):'
$appIconSet = Join-Path $Root 'ios\Assets.xcassets\AppIcon.appiconset'
Save-Png (New-IconBitmap $art 1024 0.80 $false) (Join-Path $appIconSet 'AppIcon-1024.png')
Save-Png (New-IconBitmap $art 1024 0.80 $false) (Join-Path $appIconSet 'AppIcon-1024-dark.png')

# Monochrome copy for the tinted appearance: luminance in, alpha preserved.
$tinted = New-IconBitmap $art 1024 0.80 $false
for ($y = 0; $y -lt $tinted.Height; $y++) {
    for ($x = 0; $x -lt $tinted.Width; $x++) {
        $p = $tinted.GetPixel($x, $y)
        if ($p.A -eq 0) { continue }
        $l = [int](0.299 * $p.R + 0.587 * $p.G + 0.114 * $p.B)
        $tinted.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($p.A, $l, $l, $l))
    }
}
Save-Png $tinted (Join-Path $appIconSet 'AppIcon-1024-tinted.png')

# macOS. An .iconset directory, which the build hands to iconutil; the names and sizes
# below are the ones that tool requires, and it rejects a set containing anything else.
Write-Host 'macos:'
$iconset = Join-Path $Root 'macos\tyrian2000.iconset'
$macSizes = @{
    'icon_16x16.png'      = 16
    'icon_16x16@2x.png'   = 32
    'icon_32x32.png'      = 32
    'icon_32x32@2x.png'   = 64
    'icon_128x128.png'    = 128
    'icon_128x128@2x.png' = 256
    'icon_256x256.png'    = 256
    'icon_256x256@2x.png' = 512
    'icon_512x512.png'    = 512
    'icon_512x512@2x.png' = 1024
}
foreach ($f in $macSizes.GetEnumerator()) {
    Save-Png (New-MacIconBitmap $art $f.Value) (Join-Path $iconset $f.Key)
}

$art.Dispose()
Write-Host 'done'
