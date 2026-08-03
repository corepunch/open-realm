param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$PackageDir = '',
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $PackageDir) {
    $PackageDir = Join-Path $repoRoot 'dist\windows'
}
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
$ucrtBin = Join-Path $MsysRoot 'ucrt64\bin'

if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) {
    throw "MSYS2 bash was not found at $bash"
}

$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
# This project builds unity translation units and does not emit header dependency
# files. Force the DLLs to rebuild so a header-only engine fix cannot leave stale
# binaries in the Windows package.
$buildCommand = "make -B -j$Jobs BIN_DIR=build-windows/bin LIB_DIR=build-windows/lib openwarcraft3"

Push-Location $repoRoot
try {
    & $bash -lc $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "OpenWarcraft3 Windows build failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'build-windows\bin\openwarcraft3.exe') -Destination $PackageDir -Force
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build-windows\lib') -Filter '*.dll' |
    Copy-Item -Destination $PackageDir -Force

foreach ($runtime in 'SDL2.dll', 'zlib1.dll', 'libepoxy-0.dll') {
    Copy-Item -LiteralPath (Join-Path $ucrtBin $runtime) -Destination $PackageDir -Force
}

Copy-Item -LiteralPath (Join-Path $repoRoot 'share') -Destination $PackageDir -Recurse -Force

Write-Host "Windows package created at $PackageDir"
