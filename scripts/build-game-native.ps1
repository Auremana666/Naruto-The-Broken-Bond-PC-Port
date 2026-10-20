[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PlumeSource,
    [Parameter(Mandatory)]
    [string]$XenosRecompThirdparty,
    [string]$SdkPrefix,
    [string]$BuildDirectory,
    [ValidateSet('d3d12', 'vulkan', 'both')]
    [string]$Backend = 'd3d12',
    [string]$SpirvVal,
    [switch]$Tests,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6
)

# Builds the native game and matching launcher in a separate build directory.
# This release defaults to Direct3D 12; Vulkan remains opt-in for development.
# No Xenos plugin is staged.
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
    $backendOptions = @(
        "-DBROKENBOND_NATIVE_D3D12=$(if ($Backend -ne 'vulkan') { 'ON' } else { 'OFF' })",
        "-DBROKENBOND_NATIVE_VULKAN=$(if ($Backend -ne 'd3d12') { 'ON' } else { 'OFF' })",
        "-DBROKENBOND_NATIVE_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })"
    )
    if ($SpirvVal) { $backendOptions += "-DBROKENBOND_SPIRV_VAL=$((Resolve-Path -LiteralPath $SpirvVal).Path)" }
    # Code generation is shared with the Xenos build (scripts/build-game.ps1).
    & cmake --preset windows-release -B $BuildDirectory "-DCMAKE_PREFIX_PATH=$SdkPrefix" `
        "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue" -DBROKENBOND_NATIVE_RENDERER=ON `
        -DBROKENBOND_BUILD_LAUNCHER=ON -DBROKENBOND_LAUNCHER_NATIVE_RENDERER=ON `
        "-DNATIVE_RENDERER_PLUME_SOURCE=$PlumeSource" "-DBROKENBOND_XENOS_RECOMP_THIRDPARTY=$XenosRecompThirdparty" @backendOptions
    if ($LASTEXITCODE -ne 0) { throw 'Native renderer configuration failed.' }
    & cmake --build $BuildDirectory --target broken_bond broken_bond_launcher --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Native renderer build failed.' }
    if ($Tests) {
        & cmake --build $BuildDirectory --target broken_bond_native_tests broken_bond_launcher_test --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Native renderer test build failed.' }
        & ctest --test-dir $BuildDirectory --output-on-failure -R '^(native_|launcher_config)'
        if ($LASTEXITCODE -ne 0) { throw 'Native renderer tests failed.' }
    }
} finally {
    Pop-Location
}
