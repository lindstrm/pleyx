# Convert PNG to ICO (PNG-in-ICO format, supported by Windows Vista+)
param(
    [Parameter(Mandatory=$true)]
    [string]$InputPng,

    [Parameter(Mandatory=$true)]
    [string]$OutputIco
)

$pngData = [System.IO.File]::ReadAllBytes($InputPng)

# Read PNG dimensions from header
# PNG signature: 8 bytes
# IHDR chunk: 4 bytes length + 4 bytes type + 4 bytes width + 4 bytes height + ...
$width = [BitConverter]::ToInt32(@($pngData[19], $pngData[18], $pngData[17], $pngData[16]), 0)
$height = [BitConverter]::ToInt32(@($pngData[23], $pngData[22], $pngData[21], $pngData[20]), 0)

# Clamp to 256 (0 means 256 in ICO format)
$iconWidth = if ($width -ge 256) { 0 } else { [byte]$width }
$iconHeight = if ($height -ge 256) { 0 } else { [byte]$height }

$ico = New-Object System.IO.MemoryStream

# ICONDIR header (6 bytes)
$ico.Write([byte[]]@(0, 0), 0, 2)           # Reserved
$ico.Write([byte[]]@(1, 0), 0, 2)           # Type (1 = icon)
$ico.Write([byte[]]@(1, 0), 0, 2)           # Count (1 image)

# ICONDIRENTRY (16 bytes)
$ico.WriteByte($iconWidth)                   # Width
$ico.WriteByte($iconHeight)                  # Height
$ico.WriteByte(0)                            # Color count (0 for >256 colors)
$ico.WriteByte(0)                            # Reserved
$ico.Write([byte[]]@(1, 0), 0, 2)           # Color planes
$ico.Write([byte[]]@(32, 0), 0, 2)          # Bits per pixel

# Size of PNG data (4 bytes, little-endian)
$sizeBytes = [BitConverter]::GetBytes([int]$pngData.Length)
$ico.Write($sizeBytes, 0, 4)

# Offset to PNG data (4 bytes) - 6 + 16 = 22
$offsetBytes = [BitConverter]::GetBytes([int]22)
$ico.Write($offsetBytes, 0, 4)

# PNG data
$ico.Write($pngData, 0, $pngData.Length)

[System.IO.File]::WriteAllBytes($OutputIco, $ico.ToArray())
$ico.Close()

Write-Host "Created ICO: $OutputIco ($width x $height)"
