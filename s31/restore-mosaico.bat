@echo off
rem Puts the ESP-Mosaico's NOR flash back as flash-mosaico.bat saved it
rem before its first write (backup\mosaico-nor-before.bin, 16 MB): the
rem factory firmware as it was.  The NAND is put back separately, in
rem tiny386's USB disk mode (restore mode, README.md), before this.
rem
rem   restore-mosaico.bat COMport
rem
rem Download mode first: switch off, hold BOOT, press POWER, let go of BOOT.
setlocal EnableExtensions
set "HERE=%~dp0"
if not defined ESPTOOL set "ESPTOOL=esptool"
if not defined BACKUP_DIR set "BACKUP_DIR=%HERE%backup"
set "PORT=%~1"
if "%PORT%"=="" (
    echo Usage: restore-mosaico.bat COMport
    exit /b 1
)
set "BK=%BACKUP_DIR%\mosaico-nor-before.bin"
if not exist "%BK%" (
    echo There is no saved flash at %BK%.
    exit /b 1
)
for %%s in ("%BK%") do if not "%%~zs"=="16777216" (
    echo %BK% is not 16 MB: not a whole saved flash.
    exit /b 1
)
echo Will write %BK% over the whole flash of the board on %PORT%.
set "ANS="
set /p "ANS=Type Y and press Enter to go on, anything else stops: "
if /i not "%ANS%"=="Y" (
    echo Stopped: nothing was written.
    exit /b 1
)
call %ESPTOOL% -c esp32s31 -p %PORT% --before no-reset --after no-reset write-flash -fm keep -ff keep -fs keep 0 "%BK%"
if errorlevel 1 (
    echo The write failed.  Put the board in download mode and run this again.
    exit /b 1
)
echo Written.  Switch the board off and on with POWER.
exit /b 0
