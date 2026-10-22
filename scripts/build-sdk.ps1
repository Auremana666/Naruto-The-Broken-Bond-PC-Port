[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 128)]
    [int]$Jobs = [Math]::Min([Environment]::ProcessorCount, 12)
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sdkBuild = Join-Path $projectRoot 'build/sdk'
$sdkInstall = Join-Path $projectRoot 'build/sdk-install'
$sdkSource = Join-Path $projectRoot 'sdk'
foreach ($patchName in @('rexglue-enable-usbcam.patch', 'rexglue-discovery.patch', 'rexglue-xma-decoder.patch', 'rexglue-mnk-modifiers.patch', 'rexglue-pc-features.patch')) {
    $sdkPatch = Join-Path $projectRoot "patches/$patchName"
    & git -C $sdkSource apply --check $sdkPatch 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        & git -C $sdkSource apply $sdkPatch
        if ($LASTEXITCODE -ne 0) { throw "Applying SDK patch $patchName failed." }
    } else {
        & git -C $sdkSource apply --reverse --check $sdkPatch 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "SDK patch $patchName cannot be applied or verified. Existing SDK changes were preserved."
        }
    }
}

$mspackSource = Join-Path $projectRoot 'sdk/thirdparty/libmspack/cabextract/mspack/lzxd.c'
if ((Get-Content -LiteralPath $mspackSource -TotalCount 1) -eq '../../libmspack/mspack/lzxd.c') {
    throw 'The libmspack checkout contains symlink placeholders. Initialize the SDK submodules with Git core.symlinks=true before building.'
}

if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Build Tools with the C++ workload are required.'
    }
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) {
        throw 'No Visual Studio installation with the C++ workload was found.'
    }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

& cmake -S (Join-Path $projectRoot 'sdk') -B $sdkBuild -G 'Ninja Multi-Config' `
    '-DCMAKE_C_COMPILER=clang' '-DCMAKE_CXX_COMPILER=clang++' `
    '-DCMAKE_C_FLAGS=-march=x86-64-v2' '-DCMAKE_CXX_FLAGS=-march=x86-64-v2' `
    "-DCMAKE_INSTALL_PREFIX=$sdkInstall" '-DREXGLUE_BUILD_TESTS=OFF'
if ($LASTEXITCODE -ne 0) { throw 'SDK configuration failed.' }

& cmake --build $sdkBuild --config Release --target rexglue --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'SDK code generation tool build failed.' }

$configurations = @('Release')
if ($Configuration -ne 'Release') { $configurations += $Configuration }
foreach ($buildConfiguration in $configurations) {
    & cmake --build $sdkBuild --config $buildConfiguration --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "SDK $buildConfiguration build failed." }

    & cmake --install $sdkBuild --config $buildConfiguration
    if ($LASTEXITCODE -ne 0) { throw "SDK $buildConfiguration installation failed." }
}

Write-Host "SDK installed to $sdkInstall"
