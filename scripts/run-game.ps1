[CmdletBinding()]
param(
    [ValidateSet('info', 'debug', 'trace')]
    [string]$LogLevel = 'info',
    [switch]$Keyboard,
    # native: the build of scripts/build-game-native.ps1, with a JSONL journal next to the log.
    [ValidateSet('xenos', 'native')]
    [string]$Renderer = 'xenos'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = if ($Renderer -eq 'native') { 'build/native' } else { 'build/game' }
$executable = Join-Path $projectRoot "$buildDirectory/broken_bond.exe"
$gameRoot = Join-Path $projectRoot 'game'
$userRoot = Join-Path $projectRoot 'build/user'
$logRoot = Join-Path $projectRoot 'build/logs'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build the game with scripts/$(if ($Renderer -eq 'native') { 'build-game-native.ps1' } else { 'build-game.ps1' }) first."
}
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'default.xex'))) { throw 'Extract the game with scripts/extract-game.ps1 first.' }
New-Item -ItemType Directory -Force $userRoot, $logRoot | Out-Null
$runName = "runtime-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss')
$logFile = Join-Path $logRoot "$runName.log"
Write-Host "Runtime log: $logFile"
$launchArguments = @(
    '--game_data_root', ('"{0}"' -f $gameRoot),
    '--user_data_root', ('"{0}"' -f $userRoot),
    '--gpu_plugin', 'xenos',
    '--log_file', ('"{0}"' -f $logFile),
    '--log_level', $LogLevel
)
if ($Keyboard) { $launchArguments += '--mnk_mode' }
$previousFrameDirectory = $env:REX_NATIVE_FRAME_DIR
if ($Renderer -eq 'native') {
    $journal = [IO.Path]::ChangeExtension($logFile, '.native.jsonl')
    Write-Host "Native renderer journal: $journal"
    $launchArguments += @('--renderer', 'native', '--native_renderer_bootstrap_path', ('"{0}"' -f $journal))
    # Every native run leaves the pipelines it created (pipelines.json at the
    # close) for scripts/update-pipeline-cache.ps1, unless a frame directory
    # was already chosen.
    if (-not $env:REX_NATIVE_FRAME_DIR) {
        $env:REX_NATIVE_FRAME_DIR = Join-Path $projectRoot "build/pipeline-runs/$runName"
        New-Item -ItemType Directory -Force $env:REX_NATIVE_FRAME_DIR | Out-Null
    }
    Write-Host "Native renderer frame directory: $env:REX_NATIVE_FRAME_DIR"
}
try {
    $process = Start-Process -FilePath $executable -ArgumentList $launchArguments -WorkingDirectory $projectRoot -PassThru -Wait
} finally {
    $env:REX_NATIVE_FRAME_DIR = $previousFrameDirectory
}
if ($process.ExitCode -ne 0) { throw "Game exited with code $($process.ExitCode). See $logFile" }
