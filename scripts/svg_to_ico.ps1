# Convert static/icon.svg into a multi-size Windows ICO.
# Uses ImageMagick when available. Otherwise uses System.Drawing to render
# the SVG primitives in the project's icon: rect/gradient, text, circle, path.
# This fallback is deliberately limited, not a general-purpose SVG renderer.
param(
    [Parameter(Mandatory = $true)][string]$SvgPath,
    [Parameter(Mandatory = $true)][string]$IcoPath
)

$ErrorActionPreference = 'Stop'
$svgFile = (Resolve-Path -LiteralPath $SvgPath).ProviderPath
$icoFile = [System.IO.Path]::GetFullPath($IcoPath)
$icoDir = [System.IO.Path]::GetDirectoryName($icoFile)
if (-not (Test-Path -LiteralPath $icoDir)) {
    New-Item -ItemType Directory -Path $icoDir -Force | Out-Null
}

$magick = Get-Command magick.exe -CommandType Application -ErrorAction SilentlyContinue
if ($null -ne $magick) {
    Write-Host 'Creating ICO from SVG with ImageMagick...'
    & $magick.Source -background none $svgFile -define 'icon:auto-resize=256,128,64,48,32,16' $icoFile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $icoFile)) {
        throw "ImageMagick failed (exit code $LASTEXITCODE)."
    }
    exit 0
}

Write-Host 'ImageMagick unavailable: using PowerShell/System.Drawing SVG fallback...'
Add-Type -AssemblyName System.Drawing
[xml]$document = Get-Content -LiteralPath $svgFile -Raw -Encoding UTF8
$root = $document.DocumentElement

function As-Number([string]$value) {
    return [double]::Parse($value, [System.Globalization.CultureInfo]::InvariantCulture)
}
function As-Color([string]$value) {
    try { return [System.Drawing.ColorTranslator]::FromHtml($value) }
    catch { throw "Unsupported SVG color: $value" }
}
function As-Coord([string]$value, [double]$extent) {
    if ($value.EndsWith('%')) {
        return (As-Number $value.Substring(0, $value.Length - 1)) * $extent / 100.0
    }
    return As-Number $value
}

if ($root.LocalName -ne 'svg' -or
    (As-Number $root.GetAttribute('width')) -ne 256 -or
    (As-Number $root.GetAttribute('height')) -ne 256) {
    throw 'PowerShell fallback currently supports a 256x256 SVG only.'
}
$gradient = $root.SelectSingleNode(".//*[local-name()='linearGradient']")
if ($null -eq $gradient) { throw 'SVG linearGradient was not found.' }
$stops = $gradient.SelectNodes("./*[local-name()='stop']")
if ($stops.Count -ne 2) { throw 'Fallback supports a two-stop gradient only.' }
$colorA = As-Color $stops[0].GetAttribute('stop-color')
$colorB = As-Color $stops[1].GetAttribute('stop-color')
$x1 = As-Coord $gradient.GetAttribute('x1') 256
$y1 = As-Coord $gradient.GetAttribute('y1') 256
$x2 = As-Coord $gradient.GetAttribute('x2') 256
$y2 = As-Coord $gradient.GetAttribute('y2') 256

$pixelFormat = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
$baseBitmap = [System.Drawing.Bitmap]::new(256, 256, $pixelFormat)
$graphics = [System.Drawing.Graphics]::FromImage($baseBitmap)
try {
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    foreach ($node in $root.ChildNodes) {
        if ($node.NodeType -ne [System.Xml.XmlNodeType]::Element) { continue }
        switch ($node.LocalName) {
            'defs' { continue }
            'rect' {
                if ($node.GetAttribute('fill') -ne "url(#$($gradient.GetAttribute('id')))") {
                    throw 'Only the declared gradient rectangle is supported by the fallback.'
                }
                $rx = [single](As-Number $node.GetAttribute('rx'))
                $left = if ($node.HasAttribute('x')) { [single](As-Number $node.GetAttribute('x')) } else { [single]0 }
                $top  = if ($node.HasAttribute('y')) { [single](As-Number $node.GetAttribute('y')) } else { [single]0 }
                $w = [single](As-Number $node.GetAttribute('width'))
                $h = [single](As-Number $node.GetAttribute('height'))
                $d = 2 * $rx
                $outline = [System.Drawing.Drawing2D.GraphicsPath]::new()
                $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
                    [System.Drawing.PointF]::new([single]$x1, [single]$y1),
                    [System.Drawing.PointF]::new([single]$x2, [single]$y2), $colorA, $colorB)
                try {
                    $outline.AddArc($left, $top, $d, $d, 180, 90)
                    $outline.AddArc($left + $w - $d, $top, $d, $d, 270, 90)
                    $outline.AddArc($left + $w - $d, $top + $h - $d, $d, $d, 0, 90)
                    $outline.AddArc($left, $top + $h - $d, $d, $d, 90, 90)
                    $outline.CloseFigure()
                    $graphics.FillPath($brush, $outline)
                }
                finally { $brush.Dispose(); $outline.Dispose() }
            }
            'text' {
                if ($node.GetAttribute('text-anchor') -ne 'middle') {
                    throw 'Fallback supports centered SVG text only.'
                }
                $fontSize = [single](As-Number $node.GetAttribute('font-size'))
                $fontName = $node.GetAttribute('font-family').Split(',')[0].Trim()
                $fontStyle = if ($node.GetAttribute('font-weight') -eq 'bold') {
                    [System.Drawing.FontStyle]::Bold
                } else { [System.Drawing.FontStyle]::Regular }
                $font = [System.Drawing.Font]::new($fontName, $fontSize, $fontStyle,
                    [System.Drawing.GraphicsUnit]::Pixel)
                $brush = [System.Drawing.SolidBrush]::new((As-Color $node.GetAttribute('fill')))
                $format = [System.Drawing.StringFormat]::new()
                try {
                    $format.Alignment = [System.Drawing.StringAlignment]::Center
                    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
                    $cx = [single](As-Number $node.GetAttribute('x'))
                    $baseline = [single](As-Number $node.GetAttribute('y'))
                    # System.Drawing uses line boxes, not SVG baselines: close visual approximation.
                    $bounds = [System.Drawing.RectangleF]::new($cx - 128,
                        $baseline - $fontSize * 1.22, 256, $fontSize * 1.55)
                    $graphics.DrawString($node.InnerText, $font, $brush, $bounds, $format)
                }
                finally { $font.Dispose(); $brush.Dispose(); $format.Dispose() }
            }
            'circle' {
                $r = [single](As-Number $node.GetAttribute('r'))
                $cx = [single](As-Number $node.GetAttribute('cx'))
                $cy = [single](As-Number $node.GetAttribute('cy'))
                $brush = [System.Drawing.SolidBrush]::new((As-Color $node.GetAttribute('fill')))
                try { $graphics.FillEllipse($brush, $cx - $r, $cy - $r, 2 * $r, 2 * $r) }
                finally { $brush.Dispose() }
            }
            'path' {
                $d = $node.GetAttribute('d')
                if ($d -match '[A-KN-Za-kn-z]') {
                    throw "Unsupported SVG path commands in: $d"
                }
                $numbers = [regex]::Matches($d, '-?(?:\d+(?:\.\d*)?|\.\d+)')
                if ($numbers.Count -lt 4 -or $numbers.Count % 2 -ne 0) {
                    throw 'SVG path must contain at least two points.'
                }
                $points = [System.Collections.Generic.List[System.Drawing.PointF]]::new()
                for ($i = 0; $i -lt $numbers.Count; $i += 2) {
                    $points.Add([System.Drawing.PointF]::new(
                        [single](As-Number $numbers[$i].Value),
                        [single](As-Number $numbers[$i + 1].Value)))
                }
                $pen = [System.Drawing.Pen]::new((As-Color $node.GetAttribute('stroke')),
                    [single](As-Number $node.GetAttribute('stroke-width')))
                try {
                    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
                    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
                    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
                    $graphics.DrawLines($pen, $points.ToArray())
                }
                finally { $pen.Dispose() }
            }
            default { throw "Unsupported SVG element: $($node.LocalName)" }
        }
    }
}
finally { $graphics.Dispose() }

try {
    $sizes = @(256, 128, 64, 48, 32, 16)
    $pngImages = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, $pixelFormat)
        $g = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [System.IO.MemoryStream]::new()
        try {
            $g.Clear([System.Drawing.Color]::Transparent)
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.DrawImage($baseBitmap, 0, 0, $size, $size)
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $pngImages.Add($stream.ToArray())
        }
        finally { $g.Dispose(); $bitmap.Dispose(); $stream.Dispose() }
    }

    $writer = [System.IO.BinaryWriter]::new([System.IO.File]::Create($icoFile))
    try {
        # ICO header: reserved, icon type, image count.
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = [uint32](6 + 16 * $sizes.Count)
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $sizeByte = [byte]($sizes[$i] % 256)  # 0 encodes size 256.
            $writer.Write($sizeByte)
            $writer.Write($sizeByte)
            $writer.Write([byte]0) # color count
            $writer.Write([byte]0) # reserved
            $writer.Write([uint16]1) # planes
            $writer.Write([uint16]32) # bits per pixel
            $writer.Write([uint32]$pngImages[$i].Length)
            $writer.Write($offset)
            $offset += [uint32]$pngImages[$i].Length
        }
        foreach ($png in $pngImages) { $writer.Write([byte[]]$png) }
    }
    finally { $writer.Dispose() }
}
finally { $baseBitmap.Dispose() }
Write-Host "Created $icoFile"
