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

# Bundle the full MinGW runtime closure (SDL2, zlib, libepoxy plus transitive
# deps like libgcc/libwinpthread) beside the exe: a clean Windows install has
# no MSYS2. The shared script fails loudly on unresolved or missing required
# DLLs instead of producing a package that cannot start (issue #462).
$bashRepo = $repoRoot -replace '\\','/'
$bashPkg = ([System.IO.Path]::GetFullPath($PackageDir)) -replace '\\','/'
& $bash -lc "bash '$bashRepo/dist-scripts/windows/bundle_mingw_dlls.sh' '$bashPkg' '$bashRepo/build-windows/bin/openwarcraft3.exe' '$bashRepo'/build-windows/lib/*.dll"
if ($LASTEXITCODE -ne 0) {
    throw "Windows DLL bundling failed with exit code $LASTEXITCODE"
}

# install-share (an order-only step of the exe build) stages engine fonts plus
# per-game defaults into build/share; ship that tree, not the raw share/ dir.
Copy-Item -LiteralPath (Join-Path $repoRoot 'build\share') -Destination $PackageDir -Recurse -Force

Write-Host "Windows package created at $PackageDir"
