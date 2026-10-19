# Copy sdcard-layout\{Mac,PC,Linux,Logs,Audio} onto a mounted POCKETOPS volume (Windows).
# Only copies new/changed files (robocopy + FAT 2s time fuzz).
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\sync-to-pocketops.ps1
#    or:  scripts\sync-to-pocketops.cmd

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Src = Join-Path $Root "sdcard-layout"
$Folders = @("Mac", "PC", "Linux", "Logs", "Audio")

if (-not (Test-Path -LiteralPath $Src -PathType Container)) {
    Write-Error "Missing source: $Src"
}

function Find-PocketOpsVolume {
    $disk = Get-CimInstance -ClassName Win32_LogicalDisk -Filter "DriveType=2 OR DriveType=3" |
        Where-Object { $_.VolumeName -and ($_.VolumeName.Trim().ToUpper() -eq "POCKETOPS") } |
        Select-Object -First 1
    if ($disk) {
        return ($disk.DeviceID.TrimEnd("\") + "\")
    }
    return $null
}

$Dest = Find-PocketOpsVolume
if (-not $Dest) {
    Write-Host "No POCKETOPS drive mounted (volume label POCKETOPS)." -ForegroundColor Red
    Write-Host "Expose the card on the stick, then run again."
    exit 1
}

Write-Host "Source: $Src"
Write-Host "Dest:   $Dest"

$rootReadme = Join-Path $Src "README.txt"
if (Test-Path -LiteralPath $rootReadme -PathType Leaf) {
    Copy-Item -LiteralPath $rootReadme -Destination (Join-Path $Dest "README.txt") -Force
    Write-Host "  synced README.txt"
}

foreach ($name in $Folders) {
    $from = Join-Path $Src $name
    $to = Join-Path $Dest $name
    if (-not (Test-Path -LiteralPath $from -PathType Container)) {
        New-Item -ItemType Directory -Path $from -Force | Out-Null
    }
    New-Item -ItemType Directory -Path $to -Force | Out-Null

    # /E subdirs; /FFT = FAT 2-second times (skip false "changed"); /XF skip repo placeholders
    & robocopy.exe $from $to /E /FFT /XF .gitkeep /R:2 /W:1 /NFL /NDL /NJH
    $code = $LASTEXITCODE
    # robocopy: 0–7 = OK (copied / skipped / extras); >= 8 = failure
    if ($code -ge 8) {
        Write-Error "robocopy failed for $name/ (exit $code)"
    }
    Write-Host "  synced $name/"
}

Write-Host "Done."
exit 0

