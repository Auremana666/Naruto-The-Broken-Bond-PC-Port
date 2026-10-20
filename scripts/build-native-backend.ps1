[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PlumeSource,
    [Parameter(Mandatory)]
    [string]$FmtSource,
    [string]$BuildDirectory,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$PlumeSource = (Resolve-Path -LiteralPath $PlumeSource).Path
$FmtSource = (Resolve-Path -LiteralPath $FmtSource).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build/native-backend' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Force $BuildDirectory | Out-Null

if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

$configureArgs = @(
    '-S', (Join-Path $projectRoot 'tools/native-renderer/isolated'),
    '-B', $BuildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=clang++',
    "-DNATIVE_RENDERER_PLUME_SOURCE=$PlumeSource",
    "-DNATIVE_RENDERER_FMT_SOURCE=$FmtSource"
)
$buildArgs = @('--build', $BuildDirectory, '--target', 'broken_bond_native_backend', '--parallel', "$Jobs", '--verbose')
[ordered]@{
    configure = @('cmake') + $configureArgs
    build = @('cmake') + $buildArgs
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $BuildDirectory 'commands.json') -Encoding utf8

& cmake @configureArgs 2>&1 | Tee-Object -FilePath (Join-Path $BuildDirectory 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'Native backend configuration failed. See configure.log.' }
& cmake @buildArgs 2>&1 | Tee-Object -FilePath (Join-Path $BuildDirectory 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Native backend compilation/linking failed. See build.log.' }
Write-Host "Native backend compiled and linked. Logs: $BuildDirectory"
