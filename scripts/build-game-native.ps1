[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PlumeSource,
    [Parameter(Mandatory)]
    [string]$XenosRecompThirdparty,
    [string]$SdkPrefix,
    [string]$BuildDirectory,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6
)

# Builds broken_bond with the native renderer next to the Xenos plugin in a
# separate build directory, so build/game keeps the Xenos-only executable.
# The native renderer is selected at launch with renderer = "native".
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkPrefix) { $SdkPrefix = Join-Path $projectRoot 'build/sdk-install' }
$SdkPrefix = [IO.Path]::GetFullPath($SdkPrefix)
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build/native' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$PlumeSource = (Resolve-Path -LiteralPath $PlumeSource).Path
$XenosRecompThirdparty = (Resolve-Path -LiteralPath $XenosRecompThirdparty).Path
$xex = Join-Path $projectRoot 'game/default.xex'
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne
    '8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576') {
    throw 'The native renderer mapping only supports the documented Broken Bond executable.'
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'game/xeshader.sdb'))) {
    throw 'The shader cache needs game/xeshader.sdb; extract the game first.'
}

if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

Push-Location $projectRoot
try {
    # Code generation is shared with the Xenos build (scripts/build-game.ps1).
    & cmake --preset windows-release -B $BuildDirectory "-DCMAKE_PREFIX_PATH=$SdkPrefix" `
        "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue" -DBROKENBOND_NATIVE_RENDERER=ON `
        "-DNATIVE_RENDERER_PLUME_SOURCE=$PlumeSource" "-DBROKENBOND_XENOS_RECOMP_THIRDPARTY=$XenosRecompThirdparty"
    if ($LASTEXITCODE -ne 0) { throw 'Native renderer configuration failed.' }
    & cmake --build $BuildDirectory --target broken_bond broken_bond_launcher --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Native renderer build failed.' }
} finally {
    Pop-Location
}
