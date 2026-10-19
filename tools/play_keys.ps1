# Launches the game and sends a scripted key sequence, taking screenshots along the way.
# Steps: "wait:<s>", "key:<SendKeys code>", "vk:<hex virtual key>[,ext]" (a real key press; ext for arrows etc.),
# "down:<hex>[,ext]" / "up:<hex>[,ext]" (hold / release a key), "click:<x>,<y>", "shot:<name>".
# Keys and clicks are only sent while one of the game's own windows is in the foreground; otherwise the
# step is skipped and reported, so input can never land in a dialog or another program.
param([string[]]$Steps, [switch]$Keep, [string[]]$GameArgs = @(), [string]$Exe = "Naruto The Broken Bond PC.exe", [string]$ExePath = "")
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -Namespace Native -Name Mouse -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
"@
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root "app\out\build\win-amd64-release"
$shots = Join-Path $root "shots"
New-Item -ItemType Directory -Force $shots | Out-Null
$start = Get-Date
$exeFull = if ($ExePath) { $ExePath } else { Join-Path $bin $Exe }
$wd = Split-Path $exeFull
$p = if ($GameArgs) { Start-Process $exeFull -WorkingDirectory $wd -ArgumentList $GameArgs -PassThru } else { Start-Process $exeFull -WorkingDirectory $wd -PassThru }
$shell = New-Object -ComObject WScript.Shell

# Brings the game forward and reports whether its window really is the foreground window now.
function Test-GameFocused {
    [void]$shell.AppActivate($p.Id)
    Start-Sleep -Milliseconds 150
    $owner = [uint32]0
    [void][Native.Mouse]::GetWindowThreadProcessId([Native.Mouse]::GetForegroundWindow(), [ref]$owner)
    return $owner -eq $p.Id
}

foreach ($step in $Steps) {
    if ($p.HasExited) { Write-Output "game exited $($p.ExitCode) before '$step'"; break }
    $kind, $arg = $step.Split(":", 2)
    if ($kind -in "key", "vk", "down", "up", "click" -and -not (Test-GameFocused)) {
        Write-Output "skipped '$step': the game window is not in the foreground"
        continue
    }
    switch ($kind) {
        "wait" { Start-Sleep -Milliseconds ([int]([double]$arg * 1000)) }
        "key"  { [System.Windows.Forms.SendKeys]::SendWait($arg); Start-Sleep -Milliseconds 100 }
        "vk" {
            $code, $ext = $arg.Split(",")
            $vk = [byte][Convert]::ToInt32($code, 16); $scan = [byte][Native.Mouse]::MapVirtualKey($vk, 0)
            $flags = if ($ext) { 1 } else { 0 }
            [Native.Mouse]::keybd_event($vk, $scan, $flags, [System.UIntPtr]::Zero); Start-Sleep -Milliseconds 80
            [Native.Mouse]::keybd_event($vk, $scan, $flags -bor 2, [System.UIntPtr]::Zero); Start-Sleep -Milliseconds 100
        }
        { $_ -in "down", "up" } {
            # down:<hex>[,ext] holds a key, up:<hex>[,ext] releases it.
            $code, $ext = $arg.Split(",")
            $vk = [byte][Convert]::ToInt32($code, 16); $scan = [byte][Native.Mouse]::MapVirtualKey($vk, 0)
            $flags = if ($ext) { 1 } else { 0 }
            if ($kind -eq "up") { $flags = $flags -bor 2 }
            [Native.Mouse]::keybd_event($vk, $scan, $flags, [System.UIntPtr]::Zero); Start-Sleep -Milliseconds 30
        }
        "click" {
            $x, $y = $arg.Split(",") | ForEach-Object { [int]$_ }
            [Native.Mouse]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 120
            [Native.Mouse]::mouse_event(2, 0, 0, 0, [System.UIntPtr]::Zero); Start-Sleep -Milliseconds 60
            [Native.Mouse]::mouse_event(4, 0, 0, 0, [System.UIntPtr]::Zero); Start-Sleep -Milliseconds 150
        }
        "shot" {
            $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
            $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
            $f = Join-Path $shots "$arg.png"; $bmp.Save($f); $g.Dispose(); $bmp.Dispose(); Write-Output "shot $f"
        }
    }
}
if (-not $p.HasExited -and -not $Keep) { [void]$p.CloseMainWindow(); [void]$p.WaitForExit(8000); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; Write-Output "stopped" } elseif ($p.HasExited) { Write-Output "exited $($p.ExitCode)" }
if (-not $ExePath) {
    $log = Get-ChildItem (Join-Path $bin "logs") | Where-Object LastWriteTime -gt $start | Sort-Object LastWriteTime | Select-Object -Last 1
    Write-Output "log: $($log.FullName)"
    if ($log) { Select-String -Path $log.FullName -Pattern "critical|FATAL" | Select-Object -First 3 | ForEach-Object Line }
}
