# Runs the game repeatedly; each time it stops on an indirect call to code the static analysis missed,
# adds that address as a function seed, regenerates the code and rebuilds.
# -Rexglue: the patched SDK's rexglue.exe (see the README).
param([int]$MaxIterations = 20, [int]$RunSeconds = 60, [string]$Rexglue = "rexglue.exe")

$root = Split-Path $PSScriptRoot
$app = Join-Path $root "app"
$bin = Join-Path $app "out\build\win-amd64-release"
$seeds = Join-Path $app "analysis-seeds.toml"

for ($i = 1; $i -le $MaxIterations; $i++) {
    $start = Get-Date
    $p = Start-Process (Join-Path $bin "Naruto The Broken Bond PC.exe") -WorkingDirectory $bin -PassThru
    $deadline = (Get-Date).AddSeconds($RunSeconds)
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "[$i] still running after $RunSeconds s"; break }

    $log = Get-ChildItem (Join-Path $bin "logs") | Where-Object LastWriteTime -gt $start | Sort-Object LastWriteTime | Select-Object -Last 1
    $fatal = Select-String -Path $log.FullName -Pattern "Call to invalid or unregistered function at guest address (0x[0-9A-Fa-f]+)" | Select-Object -First 1
    if (-not $fatal) {
        Write-Output "[$i] exited $($p.ExitCode) without a missing-function report ($($log.Name)):"
        Get-Content $log.FullName -Tail 8
        break
    }

    $address = $fatal.Matches[0].Groups[1].Value.ToUpper().Replace("0X", "0x")
    if (Select-String -Path $seeds -Pattern "`"$address`"" -Quiet) { Write-Output "[$i] $address already seeded; stopping"; break }
    Add-Content -Path $seeds -Value "`"$address`" = {} # indirect call at runtime ($($log.Name))"
    Write-Output "[$i] seeded $address"

    & $Rexglue codegen (Join-Path $app "narutobb_manifest.toml") *> (Join-Path $root "codegen.log")
    if ($LASTEXITCODE -ne 0) { Write-Output "codegen failed"; break }
    cmake --build $bin *> (Join-Path $root "app_build.log")
    if ($LASTEXITCODE -ne 0) { Write-Output "build failed"; break }
}
