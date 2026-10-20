[CmdletBinding()]
param(
    [string]$SdkPrefix,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6,
    [switch]$Regenerate
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkPrefix) { $SdkPrefix = Join-Path $projectRoot 'build/sdk-install' }
$SdkPrefix = [IO.Path]::GetFullPath($SdkPrefix)
$rexglue = Join-Path $SdkPrefix 'bin/rexglue.exe'
if (-not (Test-Path -LiteralPath $rexglue)) {
    throw 'Build the SDK with scripts/build-sdk.ps1 or pass -SdkPrefix pointing to an installed SDK.'
}
$xex = Join-Path $projectRoot 'game/default.xex'
$expectedHash = '8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576'
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'The game executable does not match the supported revision. Review the recompilation configuration before using another revision.'
}

if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

Push-Location $projectRoot
try {
    # The upstream stamp records the SDK version, but local SDK patches can
    # change code generation without changing that version.
    $toolStamp = Join-Path $projectRoot 'build/codegen-tool.sha256'
    $toolFiles = @($rexglue, (Join-Path $SdkPrefix 'bin/rexruntime.dll'))
    $toolSignature = ((Get-FileHash -LiteralPath $toolFiles -Algorithm SHA256).Hash -join ':')
    $previousSignature = if (Test-Path -LiteralPath $toolStamp) { (Get-Content -LiteralPath $toolStamp -Raw).Trim() } else { '' }
    $codegenOptions = @()
    if ($Regenerate -or $toolSignature -ne $previousSignature) { $codegenOptions += '--ignore-stamp' }
    & $rexglue codegen (Join-Path $projectRoot 'broken_bond_manifest.toml') @codegenOptions
    if ($LASTEXITCODE -ne 0) { throw 'Game code generation failed.' }
    New-Item -ItemType Directory -Force (Split-Path -Parent $toolStamp) | Out-Null
    Set-Content -LiteralPath $toolStamp -Value $toolSignature -Encoding ascii
    & cmake --preset windows-release "-DCMAKE_PREFIX_PATH=$SdkPrefix" "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue"
    if ($LASTEXITCODE -ne 0) { throw 'Game configuration failed.' }
    & cmake --build --preset windows-release --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
} finally {
    Pop-Location
}
