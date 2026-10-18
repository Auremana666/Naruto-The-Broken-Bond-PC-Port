param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][long]$WindowHandle,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$RawOutputPath
)
$ErrorActionPreference = 'Stop'
if ((Test-Path -LiteralPath $OutputPath) -or (Test-Path -LiteralPath $RawOutputPath)) {
    throw 'Capture outputs must be new files.'
}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeWindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("dwmapi.dll")] public static extern int DwmFlush();
}
'@
$window = [IntPtr]$WindowHandle
[uint32]$owner = 0
[NativeWindowCapture]::GetWindowThreadProcessId($window, [ref]$owner) | Out-Null
if ($owner -ne $ProcessId -or -not [NativeWindowCapture]::IsWindowVisible($window) -or
    [NativeWindowCapture]::IsIconic($window)) { throw 'Capture requires the visible owned window.' }
$rect = New-Object NativeWindowCapture+Rect
if (-not [NativeWindowCapture]::GetClientRect($window, [ref]$rect) -or
    $rect.Right -ne 536 -or $rect.Bottom -ne 296) { throw 'Unexpected client dimensions.' }
$flushResult = [NativeWindowCapture]::DwmFlush()
if ($flushResult -lt 0) { [Runtime.InteropServices.Marshal]::ThrowExceptionForHR($flushResult) }
# PrintWindow captures the owned client without desktop pixels or decorations.
# GDI's unused fourth byte is not presentation alpha; preserve it only in raw.
$pixelFormat = [Drawing.Imaging.PixelFormat]::Format32bppRgb
$bitmap = [Drawing.Bitmap]::new($rect.Right, $rect.Bottom, $pixelFormat)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
try {
    $dc = $graphics.GetHdc()
    try { $captured = [NativeWindowCapture]::PrintWindow($window, $dc, 3) }
    finally { $graphics.ReleaseHdc($dc) }
    if (-not $captured) { throw 'PrintWindow failed.' }
    $area = [Drawing.Rectangle]::new(0, 0, $rect.Right, $rect.Bottom)
    $data = $bitmap.LockBits($area, [Drawing.Imaging.ImageLockMode]::ReadOnly, $pixelFormat)
    try {
        $raw = [IO.FileStream]::new([IO.Path]::GetFullPath($RawOutputPath), [IO.FileMode]::CreateNew)
        try {
            $row = [byte[]]::new($rect.Right * 4)
            for ($y = 0; $y -lt $rect.Bottom; ++$y) {
                [Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0, $y * $data.Stride), $row, 0, $row.Length)
                $raw.Write($row, 0, $row.Length)
            }
        } finally { $raw.Dispose() }
    } finally { $bitmap.UnlockBits($data) }
    $png = [IO.FileStream]::new([IO.Path]::GetFullPath($OutputPath), [IO.FileMode]::CreateNew)
    try { $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png) }
    finally { $png.Dispose() }
} finally { $graphics.Dispose(); $bitmap.Dispose() }
[ordered]@{
    process = $ProcessId
    window = $WindowHandle
    client_size = @(536, 296)
    method = 'PrintWindow'
    flags = 3
    dwm_flush = $true
    visible = $true
    minimized = $false
    raw_layout = 'BGRX8'
    row_order = 'top-to-bottom'
} | ConvertTo-Json -Compress
