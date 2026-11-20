# Writes resources/icon.ico from resources/icon.svg.
# Needs ImageMagick (magick) on PATH.
# Usage: pwsh scripts/build-icon.ps1
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true # stop when magick exits non-zero

if (-not (Get-Command magick -ErrorAction SilentlyContinue)) {
    throw 'magick must be on PATH, install ImageMagick from https://imagemagick.org'
}

# repo root
Set-Location (Join-Path $PSScriptRoot '..')

# sizes Windows picks from, small tray icon to large Explorer tile
$sizes_pixels = 16, 20, 24, 32, 40, 48, 64, 256

# clean stage
$stage = 'build-icon'
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory $stage | Out-Null

# one png per size
$pngs = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size_pixels in $sizes_pixels) {
    $png = Join-Path $PWD "$stage/$size_pixels.png"
    # density 384: draw at 1024 pixels, then shrink
    # png32: 8-bit RGBA, the only png kind an ico accepts
    magick -background none -density 384 resources/icon.svg -resize "${size_pixels}x${size_pixels}" -strip "png32:$png"
    $pngs.Add([System.IO.File]::ReadAllBytes($png))
}

# ico layout: header, one directory entry per image, then the png files
# See https://en.wikipedia.org/wiki/ICO_(file_format)
$header_bytes = 6
$entry_bytes = 16
$image_offset_bytes = $header_bytes + $entry_bytes * $sizes_pixels.Count

$stream = [System.IO.File]::Create((Join-Path $PWD 'resources/icon.ico'))
$writer = [System.IO.BinaryWriter]::new($stream)

# header
$writer.Write([uint16]0)                    # reserved
$writer.Write([uint16]1)                    # type, 1 = icon
$writer.Write([uint16]$sizes_pixels.Count)  # image count

# directory
for ($i = 0; $i -lt $sizes_pixels.Count; $i++) {
    $size_byte = $sizes_pixels[$i] % 256    # 0 means 256
    $writer.Write([byte]$size_byte)         # width
    $writer.Write([byte]$size_byte)         # height
    $writer.Write([byte]0)                  # palette colors, 0 = none
    $writer.Write([byte]0)                  # reserved
    $writer.Write([uint16]1)                # color planes
    $writer.Write([uint16]32)               # bits per pixel
    $writer.Write([uint32]$pngs[$i].Length) # image bytes
    $writer.Write([uint32]$image_offset_bytes)
    $image_offset_bytes += $pngs[$i].Length
}

# images
foreach ($png_bytes in $pngs) {
    $writer.Write($png_bytes)
}
$writer.Dispose()

Remove-Item -Recurse -Force $stage

$icon_bytes = (Get-Item resources/icon.ico).Length
Write-Output "icon=resources/icon.ico sizes=$($sizes_pixels -join ',') bytes=$icon_bytes"
