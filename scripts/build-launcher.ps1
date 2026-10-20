[CmdletBinding()]
param(
    [string]$SdkPrefix,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6,
    [switch]$Test,
    [switch]$NativeRenderer,
    [string]$GameBuildDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkPrefix) { $SdkPrefix = Join-Path $projectRoot 'build/sdk-install' }
$SdkPrefix = [IO.Path]::GetFullPath($SdkPrefix)
if (-not (Test-Path -LiteralPath (Join-Path $SdkPrefix 'lib/cmake/rexglue/rexglueConfig.cmake'))) {
    throw 'Build the SDK with scripts/build-sdk.ps1 or pass -SdkPrefix pointing to an installed SDK.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

$buildRoot = Join-Path $projectRoot 'build/launcher-native'
if (-not $GameBuildDirectory) { $GameBuildDirectory = Join-Path $projectRoot 'build/native' }
$GameBuildDirectory = [IO.Path]::GetFullPath($GameBuildDirectory)
# Match the adjacent native executable's APIs when rebuilding only its launcher.
$apiOptions = @('-DBROKENBOND_NATIVE_D3D12=ON', '-DBROKENBOND_NATIVE_VULKAN=OFF')
$nativeCache = Join-Path $GameBuildDirectory 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $nativeCache)) { throw 'Configure a native game build first or pass -GameBuildDirectory.' }
if ((Get-Content -LiteralPath $nativeCache -Raw) -notmatch '(?m)^BROKENBOND_NATIVE_RENDERER:BOOL=ON\s*$') {
    throw 'The launcher requires a native game build.'
}
if (Test-Path -LiteralPath $nativeCache) {
    $cache = Get-Content -LiteralPath $nativeCache
    foreach ($name in 'BROKENBOND_NATIVE_D3D12', 'BROKENBOND_NATIVE_VULKAN', 'NATIVE_RENDERER_PLUME_SOURCE') {
        $entry = $cache | Where-Object { $_ -match "^${name}:[^=]+=" } | Select-Object -First 1
        if ($entry) { $apiOptions += "-D${name}=$($entry.Substring($entry.IndexOf('=') + 1))" }
    }
}
& cmake -S (Join-Path $projectRoot 'launcher') -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=clang++' "-DCMAKE_PREFIX_PATH=$SdkPrefix" "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue" '-DBROKENBOND_LAUNCHER_NATIVE_RENDERER=ON' @apiOptions
if ($LASTEXITCODE -ne 0) { throw 'Launcher configuration failed.' }
& cmake --build $buildRoot --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
if ($Test) {
    & cmake --build $buildRoot --target broken_bond_launcher_test broken_bond_key_bindings_test broken_bond_localization_test broken_bond_runtime_input_test --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Launcher test build failed.' }
    & ctest --test-dir $buildRoot --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Launcher tests failed.' }
}
$gameBuild = $GameBuildDirectory
New-Item -ItemType Directory -Force $gameBuild | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'broken_bond_launcher.exe') -Destination $gameBuild -Force
& cmake -E copy_directory_if_different (Join-Path $buildRoot 'assets') (Join-Path $gameBuild 'assets')
if ($LASTEXITCODE -ne 0) { throw 'Launcher font copy failed.' }
Write-Host "Launcher: $(Join-Path $gameBuild 'broken_bond_launcher.exe')"
