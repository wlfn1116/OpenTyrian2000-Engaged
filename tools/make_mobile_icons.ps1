<#
.SYNOPSIS
Generate the Android and iOS launcher icons from visualc/tyrian2000.ico.

.DESCRIPTION
Both mobile ports need bitmap icons at fixed sizes, which neither build system can
derive from a .ico. This writes them from the largest frame in the icon file. Scaling is
nearest-neighbour: the source is pixel art, and a smooth filter turns it to mush.

Android gets adaptive-icon foregrounds, whose artwork must stay inside the central 72 of
108 density-independent pixels or a launcher mask will clip it, plus square legacy
bitmaps. iOS gets opaque icons, because alpha in an iOS app icon renders as black and is
rejected by the store tooling.

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

# iOS. Names carry the point size and scale iOS looks for through CFBundleIconFiles.
Write-Host 'ios:'
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

$art.Dispose()
Write-Host 'done'
