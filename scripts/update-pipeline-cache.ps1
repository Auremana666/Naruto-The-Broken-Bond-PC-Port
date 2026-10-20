[CmdletBinding()]
param(
    # Start from these sources only, instead of the current list plus every run.
    [string[]]$Sources
)

# Rebuilds the precompiled pipeline list (git-ignored src/gpu/shaders/pipeline_cache.inc)
# from the current list and the pipelines.json every native run left under
# build/pipeline-runs (scripts/run-game.ps1 -Renderer native). The current list
# comes first, so its order and its entries stay; the next native build embeds
# the result.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$list = Join-Path $projectRoot 'src/gpu/shaders/pipeline_cache.inc'
$outputDirectory = Join-Path $projectRoot 'build/pipeline-cache'
if (-not $Sources) {
    $Sources = @()
    if (Test-Path -LiteralPath $list) { $Sources += $list }
    $Sources += Get-ChildItem -LiteralPath (Join-Path $projectRoot 'build/pipeline-runs') -Recurse -File -Filter '*pipelines.json' |
        Where-Object Length -gt 0 | Sort-Object FullName | ForEach-Object FullName
}
if (-not $Sources) { throw 'No pipeline list or pipelines.json found.' }
& python -I (Join-Path $projectRoot 'tools/native-renderer/build-pipeline-cache.py') $outputDirectory @Sources
if ($LASTEXITCODE -ne 0) { throw "build-pipeline-cache.py failed with code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $outputDirectory 'pipeline_cache.inc') -Destination $list -Force
# A new or changed .inc is not a dependency the build tracks (__has_include).
(Get-Item -LiteralPath (Join-Path $projectRoot 'src/gpu/shaders/pipelines.cpp')).LastWriteTime = Get-Date
Write-Host "Pipeline list updated from $($Sources.Count) sources; rebuild with scripts/build-game-native.ps1."
