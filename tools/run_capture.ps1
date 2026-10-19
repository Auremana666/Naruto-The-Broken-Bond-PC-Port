# Runs the game for a while, saving screenshots of the screen at the given times (seconds), then stops it.
param([int[]]$At = @(15, 30, 45), [string]$OutDir = "$PSScriptRoot\..\shots", [switch]$Keep)

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
$bin = Join-Path (Split-Path $PSScriptRoot) "app\out\build\win-amd64-release"
New-Item -ItemType Directory -Force $OutDir | Out-Null

$start = Get-Date
$p = Start-Process (Join-Path $bin "Naruto The Broken Bond PC.exe") -WorkingDirectory $bin -PassThru
foreach ($t in $At) {
    while (((Get-Date) - $start).TotalSeconds -lt $t -and -not $p.HasExited) { Start-Sleep -Milliseconds 200 }
    if ($p.HasExited) { break }
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $file = Join-Path $OutDir ("shot_{0:D3}s.png" -f $t)
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "saved $file"
}
if (-not $p.HasExited -and -not $Keep) { Stop-Process -Id $p.Id -Force; Write-Output "stopped" } elseif ($p.HasExited) { Write-Output "exited $($p.ExitCode)" }
$log = Get-ChildItem (Join-Path $bin "logs") | Where-Object LastWriteTime -gt $start | Sort-Object LastWriteTime | Select-Object -Last 1
Write-Output "log: $($log.FullName)"
