@echo off
rem Naruto The Broken Bond PC - save backup tool.
rem Zips the game's hdd folder (the saves) with the date and time in the name, so it can be kept
rem safe or sent for help. Works next to the game or in a folder inside the game folder. The zip
rem goes next to this tool; if that folder is protected (for example under Program Files), it goes
rem on the Desktop instead.
setlocal
title Naruto The Broken Bond PC - Save Backup
set "HERE=%~dp0"

rem Find the game: this folder, or the folder above it.
set "GAME="
if exist "%HERE%hdd\" set "GAME=%HERE%"
if not defined GAME if exist "%HERE%..\hdd\" set "GAME=%HERE%..\"
if not defined GAME (
  echo No saves were found. Keep this tool in the game folder, or in a folder inside it,
  echo and play the game once and save before running it.
  echo.
  pause
  exit /b 1
)

tasklist /fi "imagename eq Naruto The Broken Bond PC.exe" | find /i "Naruto The Broken Bond PC.exe" >nul
if not errorlevel 1 (
  echo The game is still running. Close it first, so the saves are complete, then run this again.
  echo.
  pause
  exit /b 1
)

for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"`) do set "STAMP=%%T"
set "NAME=Save Backup %STAMP%.zip"

rem Next to this tool if Windows lets us write there, otherwise on the Desktop.
set "OUT=%HERE%"
copy /y nul "%HERE%write_test.tmp" >nul 2>&1
if exist "%HERE%write_test.tmp" (
  del "%HERE%write_test.tmp" >nul 2>&1
) else (
  for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('Desktop')"`) do set "OUT=%%D\"
)

echo Backing up your saves...
powershell -NoProfile -Command "Compress-Archive -LiteralPath ($env:GAME + 'hdd') -DestinationPath ($env:OUT + $env:NAME) -Force"
if errorlevel 1 (
  echo.
  echo The backup failed. Please try again, or copy the hdd folder by hand.
  echo.
  pause
  exit /b 1
)

echo.
echo Done: "%NAME%"
echo Saved in: %OUT%
echo Send this file if you need help with a save.
echo.
pause
