[CmdletBinding()]
param(
    [string]$OpenScadPath,
    [ValidateSet('draft', 'production')]
    [string]$Quality = 'production'
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'turtleguard_upper.scad'
$outputDirectory = Join-Path $PSScriptRoot 'stl'

if (-not $OpenScadPath) {
    $command = Get-Command openscad -ErrorAction SilentlyContinue
    if ($command) {
        $OpenScadPath = $command.Source
    } else {
        $candidates = @(
            "$env:ProgramFiles\OpenSCAD\openscad.exe",
            "${env:ProgramFiles(x86)}\OpenSCAD\openscad.exe"
        )
        $OpenScadPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
}

if (-not $OpenScadPath -or -not (Test-Path -LiteralPath $OpenScadPath)) {
    throw 'OpenSCAD was not found. Install it or pass -OpenScadPath C:\path\to\openscad.exe.'
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$parts = @(
    'lid',
    'electronics_sled',
    'gps_sled',
    'optical_cartridge',
    'env_cartridge',
    'tripod_adapter'
)

foreach ($partName in $parts) {
    $target = Join-Path $outputDirectory "$partName.stl"
    Write-Host "Rendering $partName -> $target"
    $arguments = @(
        '-o', $target,
        # Backslash-escaped quotes must survive Windows argv parsing so the
        # OpenSCAD -D expressions remain string assignments.
        '-D', "render_quality=\`"$Quality\`"",
        '-D', "part=\`"$partName\`"",
        $source
    )
    $process = Start-Process -FilePath $OpenScadPath -ArgumentList $arguments `
        -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "OpenSCAD failed while rendering $partName (exit code $($process.ExitCode))."
    }
}

Write-Host "Exported $($parts.Count) STL files to $outputDirectory"

