@echo off
rem tiny386 for the ESP-Mosaico: writes the bootloader, partition table and
rem app (images\mosaico, or images\mosaico-net with "net") over the board's
rem Type-C port, after saving the whole 16 MB NOR flash the first time.
rem The storage partition (the TINY386 drive: your inis and BIOSes) and the
rem NAND are not touched.  restore-mosaico.bat puts the saved flash back.
rem
rem   flash-mosaico.bat COMport [net]
rem
rem The board must be in download mode first: switch it off, hold BOOT,
rem press POWER briefly, let go of BOOT.  The port is the one that appears
rem then.  esptool 5.4 or later (pip install esptool); set ESPTOOL to use
rem another, for example  set ESPTOOL=python -m esptool
setlocal EnableExtensions
set "HERE=%~dp0"
if not defined ESPTOOL set "ESPTOOL=esptool"
if not defined BACKUP_DIR set "BACKUP_DIR=%HERE%backup"
set "PORT=%~1"
if "%PORT%"=="" goto usage
set "SET=mosaico"
if /i "%~2"=="net" set "SET=mosaico-net"
set "IMG=%HERE%images\%SET%"
set "BK=%BACKUP_DIR%\mosaico-nor-before.bin"

echo.
echo tiny386 for the ESP-Mosaico (%SET%), port %PORT%
echo.
echo Checking the files ...
for %%f in (bootloader.bin partition-table.bin tiny386.bin) do (
    call :check "%IMG%\%%f" %%f || goto failed
)

if exist "%BK%" (
    echo   A saved copy of this flash is there already: %BK%
    echo   It is kept, and no new one is taken.
    set "NEED_BK="
) else (
    set "NEED_BK=1"
)

set "WRITE=%ESPTOOL% -c esp32s31 -p %PORT% --before no-reset --after no-reset write-flash -fm keep -ff keep -fs keep 0x2000 "%IMG%\bootloader.bin" 0x8000 "%IMG%\partition-table.bin" 0x10000 "%IMG%\tiny386.bin""
echo.
echo Will do, on the board on %PORT%:
if defined NEED_BK echo   first read the whole 16 MB flash into %BK%
if defined NEED_BK echo   (a few minutes; restore-mosaico.bat writes it back)
echo   write 0x2000 bootloader.bin, 0x8000 partition-table.bin, 0x10000 tiny386.bin
echo.
echo This replaces the factory firmware.  The NAND is not touched here: the
echo first start of tiny386 offers it as a drive to save before it is used.
echo.
set "ANS="
set /p "ANS=Type Y and press Enter to go on, anything else stops: "
if /i not "%ANS%"=="Y" goto cancelled

if not defined NEED_BK goto write
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"
echo.
echo Reading the flash ...
call %ESPTOOL% -c esp32s31 -p %PORT% --before no-reset --after no-reset read-flash 0 0x1000000 "%BK%.part"
if errorlevel 1 goto bk_failed
for %%s in ("%BK%.part") do if not "%%~zs"=="16777216" goto bk_failed
move /y "%BK%.part" "%BK%" >nul
certutil -hashfile "%BK%" SHA256 > "%BK%.sha256.txt"
echo   Saved: %BK%

:write
echo.
echo Writing ...
call %WRITE%
if errorlevel 1 goto write_failed
echo.
echo Written.  Switch the board off and on with POWER (BOOT not held).
echo The first start goes into USB disk mode by itself: README.md step 4.
exit /b 0

:check
if not exist "%~1" (
    echo   missing: %~1
    exit /b 1
)
set "WANT="
for /f "tokens=1,2" %%a in ('findstr /c:"%~2" "%IMG%\SHA256SUMS"') do if "%%b"=="%~2" set "WANT=%%a"
set "GOT="
for /f "skip=1 tokens=* delims=" %%h in ('certutil -hashfile "%~1" SHA256') do if not defined GOT set "GOT=%%h"
set "GOT=%GOT: =%"
if /i not "%GOT%"=="%WANT%" (
    echo   %~2 is not the file SHA256SUMS names: got %GOT%
    exit /b 1
)
echo   %~2 ok
exit /b 0

:usage
echo Usage: flash-mosaico.bat COMport [net]
echo   for example:  flash-mosaico.bat COM12
exit /b 1
:cancelled
echo Stopped: nothing was written.
exit /b 1
:bk_failed
echo.
echo The flash could not be read whole: nothing was written.  Is the board
echo in download mode (switch off, hold BOOT, press POWER)?
if exist "%BK%.part" del "%BK%.part"
exit /b 1
:write_failed
echo.
echo The write failed part way.  The saved copy is in %BACKUP_DIR%.  Put
echo the board in download mode again and run this again.
exit /b 1
:failed
echo Stopped: nothing was written.
exit /b 1
