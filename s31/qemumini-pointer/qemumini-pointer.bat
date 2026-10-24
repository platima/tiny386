@echo off
rem qemumini-pointer.bat IMAGE [-Check] [-Undo] [-Yes] [-Out FILE]
rem VMDisp9x 1.2025.0.119's QEMUMINI.DRV in IMAGE made to leave the mouse
rem pointer to Windows: see qemumini-pointer.ps1 beside this file, and
rem README.txt.  Runs it with Windows PowerShell, this once without the
rem policy that stops unsigned scripts (nothing is changed for good).
setlocal
if "%~1"=="" (
  echo usage: qemumini-pointer IMAGE [-Check] [-Undo] [-Yes] [-Out FILE]
  echo IMAGE: the driver's floppy image, or a Windows 95 hard disk image
  exit /b 1
)
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0qemumini-pointer.ps1" %*
exit /b %ERRORLEVEL%
