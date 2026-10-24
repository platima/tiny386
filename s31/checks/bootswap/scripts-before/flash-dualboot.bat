@echo off
setlocal EnableExtensions
rem tiny386 beside Linux on the ESP32-S31-Korvo-1: write the dual-boot set.
rem
rem   flash-dualboot.bat COMport [all or app]
rem
rem   all  (the default) tiny386 at 0xE20000, then the dual-boot bootloader
rem        at 0x2000 and the dual-boot partition table at 0x8000.  Nothing
rem        of Linux is written.  For Linux be5b725 or the fork release
rem        v2026.09.20 only (both have the same partition table).  tiny386
rem        goes first, in an esptool run of its own: if that run is cut
rem        short, Linux is still as it was.
rem   app  tiny386 at 0xE20000 only, for a newer tiny386 once "all" is in.
rem
rem First, in Linux: "poweroff", and wait for it.  The script resets the
rem board into download mode before it reads it, and keeps it there (esptool
rem -a no-reset) until the last write, which resets it to start.
rem
rem The port must be given (COM10 on Plat's PC).  Before anything is
rem written the script reads the board, which writes nothing: its MAC (which
rem must be KORVO1_MAC in flash-paths.cmd, if that is set), and the partition
rem table at 0x8000, which must be Linux's for "all" (or the dual-boot one, to
rem write the set again) and the dual-boot one for "app".  Over Linux's table
rem it also names the bootloader at 0x2000 by SHA-256 and, the first time,
rem reads the whole flash into BACKUP_DIR (backup\ beside this script) before
rem writing, so that back-to-linux.bat can put back exactly what was there.
rem The tiny386 image is TINY386_BIN in flash-paths.cmd.  The script checks
rem the files, prints what it will do and writes only after you type Y.
rem See FLASH.txt and dualboot\DUALBOOT.md.

set "HERE=%~dp0"
call "%HERE%flash-paths.cmd"
if not defined ESPTOOL set "ESPTOOL=esptool"
if not defined BACKUP_DIR set "BACKUP_DIR=%HERE%backup"

set "PORT="
set "MODE=all"
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="all" (set "MODE=all") else if /i "%~1"=="app" (set "MODE=app") else (set "PORT=%~1")
shift
goto args
:argsdone
if defined PORT goto port_given
echo Usage: flash-dualboot.bat COMport [all or app]
echo The port must be given, for example:  flash-dualboot.bat COM10
exit /b 1
:port_given

set "BL=%HERE%images\korvo1-dualboot\bootloader.bin"
set "PT=%HERE%images\korvo1-dualboot\partition-table.bin"
set "BL_SHA=b57469cd1542fe83a4f468f93a7e8e9bfa5af138f15a38e6867a0a54fccff29f"
set "PT_SHA=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346"
rem the tables the board may hold at 0x8000 (0xC00 bytes)
set "PT_LINUX=520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da"
set "PT_DUAL=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346"
set "PT_OWN=afbe6410c2040c5ec6e447e94b90d91e172a3c0ed3203464b65bee8b1cbc1a26"
rem the Linux bootloaders this folder knows: SHA-256 of their bytes at 0x2000
set "BL_BE5B725=547c6a5b30551de678c14fa979ed6d50821d346988b111d531b09610b157a6fe"
set "BL_BE5B725_SIZE=22000"
set "BL_RELEASE=41d5a68752936c7b65dadb3081e4ebefc649af76cc1af450bda508e3b3455d1b"
set "BL_RELEASE_SIZE=23024"
rem the tiny386 partition in the dual-boot table: 0xE20000, 0x1E0000 bytes
set "APP_MAX=1966080"
rem the backup: the whole flash, and 0x2000 to 0x8FFF, as they were
set "BK_FULL=%BACKUP_DIR%\korvo1-before.bin"
set "BK_SLICE=%BACKUP_DIR%\korvo1-2000-8fff-before.bin"
set "BOARD_TOUCHED="

echo.
echo tiny386 dual boot for the Korvo-1, mode %MODE%, port %PORT%
echo.
echo Before this: in Linux, "poweroff", and wait for it to finish.  This
echo script resets the board into download mode and keeps it there until
echo the last write, so that Linux never runs with the card half written.
echo.
echo Checking the files ...
call :need_esptool || goto failed
if /i not "%MODE%"=="all" goto check_app
call :check_hash "%BL%" %BL_SHA% || goto failed
call :check_hash "%PT%" %PT_SHA% || goto failed
:check_app
call :check_image "%TINY386_BIN%" %APP_MAX% || goto failed

echo.
echo Reading the board on %PORT% (this writes nothing) ...
set "BOARD_TOUCHED=1"
call :read_board || goto failed
if /i "%MODE%"=="app" goto board_app
if "%TABLE%"=="linux" goto board_linux
if "%TABLE%"=="dual" goto board_dual_again
goto board_wrong
:board_app
if "%TABLE%"=="dual" goto board_dual_app
goto board_wrong
:board_linux
echo   Linux's partition table, as this set expects: be5b725 or the release.
call :read_linux_boot || goto failed
goto board_ok
:board_dual_again
echo   The dual-boot table is in already: this writes the set again.
goto board_ok
:board_dual_app
echo   The dual-boot table is in: tiny386 at 0xE20000 can be replaced.
goto board_ok
:board_wrong
call :explain_table
goto failed
:board_ok

rem The backup: before the first write over Linux's table only.  One that
rem is there already is kept, if it is of this board and of what the flash
rem holds now; otherwise the script stops rather than overwrite it.
set "NEED_BACKUP="
if /i not "%MODE%"=="all" goto backup_decided
if not "%TABLE%"=="linux" goto backup_decided
if exist "%BK_SLICE%" goto backup_compare
set "NEED_BACKUP=1"
goto backup_decided
:backup_compare
call :check_backup || goto failed
call :hash_of "%SLICE_NOW%" || goto failed
set "NOWH=%HASH%"
set "BKH="
set /p BKH=<"%BK_SLICE%.sha256"
if /i "%NOWH%"=="%BKH%" goto backup_same
echo   %BACKUP_DIR% holds a backup whose 0x2000 to 0x8FFF differ from this
echo   flash now: it is of another state of this board.  It is not
echo   overwritten.  Rename that folder (keep it), then run this again to take
echo   a new backup.
goto failed
:backup_same
echo   The backup in %BACKUP_DIR% is of this board and of this same bootloader
echo   and table: it is kept, and no new one is taken.
:backup_decided

set "A1=no-reset"
if /i not "%MODE%"=="all" set "A1=hard-reset"
set WRITES1=0xE20000 "%TINY386_BIN%"
set WRITES2=0x2000 "%BL%" 0x8000 "%PT%"
set "CMD1=%ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a %A1% write-flash -fm keep -ff keep -fs keep %WRITES1%"
set "CMD2=%ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep %WRITES2%"

echo.
echo Will do, on the board on %PORT%:
if defined NEED_BACKUP echo   first read the whole flash, 16 MB, which takes a few minutes, into
if defined NEED_BACKUP echo     %BK_FULL%
if defined NEED_BACKUP echo   and 0x2000 to 0x8FFF into %BK_SLICE%
if defined NEED_BACKUP echo   (back-to-linux.bat puts them back; nothing is written if this fails)
echo   write 0xE20000  %TINY386_BIN%
echo             tiny386, %APP_SIZE% of %APP_MAX% bytes
if /i "%MODE%"=="all" echo   then, in a second esptool run:
if /i "%MODE%"=="all" echo   0x002000  images\korvo1-dualboot\bootloader.bin       the dual-boot bootloader, tiny386 while GPIO42 is held low
if /i "%MODE%"=="all" echo   0x008000  images\korvo1-dualboot\partition-table.bin  the Linux table plus tiny386 at 0xE20000
echo Only the 4 KB sectors these files cover are erased.  Linux, 0x9000 to
echo 0xE1FFFF, is left as it is.
echo.
echo Command:
echo   %CMD1%
if /i "%MODE%"=="all" echo   %CMD2%
echo.
set "ANS="
set /p "ANS=Type Y and press Enter to write, anything else stops: "
if /i not "%ANS%"=="Y" goto cancelled

if not defined NEED_BACKUP goto do_write
call :take_backup || goto backup_failed
:do_write
call %CMD1%
if errorlevel 1 goto write_failed_app
if /i not "%MODE%"=="all" goto written
call %CMD2%
if errorlevel 1 goto write_failed_boot

:written
echo.
echo Written.  Press RESET with no key held for Linux.  For tiny386: hold the
echo VOL+ key, press and release RESET, keep holding VOL+ for two seconds.
exit /b 0

:cancelled
echo Stopped.  Nothing was written.
call :reset_note
exit /b 1

:backup_failed
echo.
echo The backup could not be read or kept (see above).  Nothing was written.
call :reset_note
exit /b 1

:write_failed_app
echo.
echo esptool reported an error (see above) while writing tiny386 at 0xE20000.
echo The bootloader and partition table were not touched: Linux starts as it
echo did.  Press RESET with nothing held, or run this again.
exit /b 1

:write_failed_boot
echo.
echo esptool reported an error (see above) while writing the bootloader and
echo partition table.  If the board does not boot now, run this again, or
echo back-to-linux.bat %PORT%.  Download mode (hold BOOT, press RESET) is
echo always there for a retry.
exit /b 1

:failed
echo.
echo Stopped before writing anything.
if defined BOARD_TOUCHED call :reset_note
exit /b 1

rem Subroutines
:reset_note
echo The board may be waiting in download mode: press RESET with nothing held
echo to start Linux again.
exit /b 0

:need_esptool
set "ETV="
for /f "delims=" %%v in ('%ESPTOOL% version 2^>nul ^| findstr /r /b "[0-9]"') do set "ETV=%%v"
if defined ETV goto need_esptool_version
echo   esptool was not found ("%ESPTOOL% version" printed no version).
echo   Install it with:  pip install "esptool>=5.4"
exit /b 1
:need_esptool_version
set "ETMAJ=0"
set "ETMIN=0"
for /f "tokens=1,2 delims=." %%a in ("%ETV%") do (set "ETMAJ=%%a" & set "ETMIN=%%b")
if %ETMAJ% GTR 5 goto need_esptool_ok
if %ETMAJ% EQU 5 if %ETMIN% GEQ 4 goto need_esptool_ok
echo   esptool %ETV% is too old for the ESP32-S31.  pip install "esptool>=5.4"
exit /b 1
:need_esptool_ok
echo   esptool %ETV%
exit /b 0

:check_hash
if exist "%~1" goto check_hash_do
echo   not found: %~1
exit /b 1
:check_hash_do
certutil -hashfile "%~1" SHA256 | findstr /i /c:"%~2" >nul
if errorlevel 1 goto check_hash_bad
echo   %~nx1: SHA-256 as built (%~2)
exit /b 0
:check_hash_bad
echo   %~1 is not the file that was built and checked:
echo   its SHA-256 differs from %~2
exit /b 1

rem HASH set to the file's SHA-256, lower or upper case, no spaces
:hash_of
set "HASH="
if not exist "%~1" exit /b 1
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%~1" SHA256') do if not defined HASH set "HASH=%%h"
if not defined HASH exit /b 1
set "HASH=%HASH: =%"
exit /b 0

:check_image
if exist "%~1" goto check_image_size
echo   tiny386 image not found: %~1
echo   Set TINY386_BIN in flash-paths.cmd to the tiny386.bin to write.
exit /b 1
:check_image_size
set "APP_SIZE=%~z1"
if %APP_SIZE% LEQ %2 goto check_image_info
echo   %~1 is %APP_SIZE% bytes, more than its partition holds (%2 bytes).
exit /b 1
:check_image_info
set "II=%TEMP%\tiny386-flash-image-info.txt"
call %ESPTOOL% -c esp32s31 image-info "%~1" > "%II%" 2>&1
if errorlevel 1 goto check_image_bad
findstr /c:"(invalid)" "%II%" >nul && goto check_image_bad
findstr /c:"Chip ID: 32 (ESP32-S31)" "%II%" >nul || goto check_image_bad
echo   %~nx1: %APP_SIZE% bytes, a valid ESP32-S31 app image:
findstr /b /c:"Project name" /c:"App version" /c:"ESP-IDF" /c:"Validation hash" "%II%"
exit /b 0
:check_image_bad
echo   esptool image-info does not accept %~1 as a valid ESP32-S31 image:
type "%II%"
exit /b 1

rem The board on PORT: its MAC printed (and held to KORVO1_MAC if that is
rem set), and TABLE set to linux, dual, own (tiny386's own table) or other,
rem from the SHA-256 of 0x8000 to 0x8BFF.  Reads only; the chip is left in
rem download mode (-a no-reset).
:read_board
set "TABLE=none"
set "TNOW=%TEMP%\tiny386-table-now.bin"
set "RB=%TEMP%\tiny386-read-board.txt"
if exist "%TNOW%" del "%TNOW%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-mac > "%RB%" 2>&1
if errorlevel 1 goto read_board_bad
findstr /c:"MAC" "%RB%"
set "MACLINE="
for /f "delims=" %%l in ('findstr /c:"MAC" "%RB%"') do set "MACLINE=%%l"
if not defined KORVO1_MAC goto read_board_table
findstr /i /c:"%KORVO1_MAC%" "%RB%" >nul && goto read_board_mac_ok
echo   This board's MAC is not KORVO1_MAC in flash-paths.cmd, %KORVO1_MAC%:
echo   it is not the Korvo-1 this folder is set up for.
exit /b 1
:read_board_mac_ok
echo   the MAC is KORVO1_MAC's: the Korvo-1 this folder is set up for.
:read_board_table
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x8000 0xC00 "%TNOW%" > "%RB%" 2>&1
if errorlevel 1 goto read_board_bad
if not exist "%TNOW%" goto read_board_bad
set "TABLE=other"
certutil -hashfile "%TNOW%" SHA256 | findstr /i /c:"%PT_LINUX%" >nul && set "TABLE=linux"
certutil -hashfile "%TNOW%" SHA256 | findstr /i /c:"%PT_DUAL%" >nul && set "TABLE=dual"
certutil -hashfile "%TNOW%" SHA256 | findstr /i /c:"%PT_OWN%" >nul && set "TABLE=own"
echo   partition table at 0x8000: %TABLE%
exit /b 0
:read_board_bad
echo   esptool could not read the board on %PORT%:
type "%RB%"
echo   Is the Korvo-1 on that port (the UART Type-C), and no terminal holding it?
exit /b 1

rem Over Linux's table: the bootloader at 0x2000 named by SHA-256 (BLK
rem be5b725, release or other), and 0x2000 to 0x8FFF read into SLICE_NOW.
rem Reads only.
:read_linux_boot
set "BLK=other"
set "TB=%TEMP%\tiny386-boot-now.bin"
set "SLICE_NOW=%TEMP%\tiny386-2000-8fff-now.bin"
if exist "%TB%" del "%TB%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x2000 %BL_BE5B725_SIZE% "%TB%" > "%RB%" 2>&1
if errorlevel 1 goto read_linux_boot_bad
call :hash_of "%TB%" || goto read_linux_boot_bad
if /i "%HASH%"=="%BL_BE5B725%" set "BLK=be5b725"
if exist "%TB%" del "%TB%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x2000 %BL_RELEASE_SIZE% "%TB%" > "%RB%" 2>&1
if errorlevel 1 goto read_linux_boot_bad
call :hash_of "%TB%" || goto read_linux_boot_bad
if /i "%HASH%"=="%BL_RELEASE%" set "BLK=release"
if exist "%SLICE_NOW%" del "%SLICE_NOW%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x2000 0x7000 "%SLICE_NOW%" > "%RB%" 2>&1
if errorlevel 1 goto read_linux_boot_bad
if not exist "%SLICE_NOW%" goto read_linux_boot_bad
if "%BLK%"=="be5b725" echo   bootloader at 0x2000: be5b725's (547c6a5b), as in be5b725's flash.bin
if "%BLK%"=="release" echo   bootloader at 0x2000: the fork release v2026.09.20's (41d5a687).  The
if "%BLK%"=="release" echo   dual-boot bootloader is be5b725's with the key added; it should start
if "%BLK%"=="release" echo   the release's loader as well, but that has not been tried.
if "%BLK%"=="other" echo   bootloader at 0x2000: none this folder knows (neither be5b725's nor
if "%BLK%"=="other" echo   the release's).  The backup taken before writing is then the only
if "%BLK%"=="other" echo   exact way back: keep it.
exit /b 0
:read_linux_boot_bad
echo   esptool could not read 0x2000 to 0x8FFF on %PORT%:
type "%RB%"
exit /b 1

rem A backup that is there already: whole, of this board
:check_backup
call :hash_of "%BK_SLICE%" || goto check_backup_bad
set "BKH="
if exist "%BK_SLICE%.sha256" set /p BKH=<"%BK_SLICE%.sha256"
if /i not "%HASH%"=="%BKH%" goto check_backup_bad
if not exist "%BACKUP_DIR%\korvo1-before-mac.txt" goto check_backup_bad
if not defined MACLINE goto check_backup_mac
findstr /c:"%MACLINE%" "%BACKUP_DIR%\korvo1-before-mac.txt" >nul && exit /b 0
:check_backup_mac
echo   The backup in %BACKUP_DIR% was taken from a board whose MAC is not
echo   this one's.  It is not overwritten: rename that folder (keep it), or
echo   set BACKUP_DIR in flash-paths.cmd, then run this again.
exit /b 1
:check_backup_bad
echo   The backup in %BACKUP_DIR% is not whole (a file missing, or not the
echo   bytes recorded beside it).  It is not overwritten: rename that folder
echo   (keep it), then run this again to take a new one.
exit /b 1

rem The first write's backup: the whole flash, then 0x2000 to 0x8FFF as read
rem before the prompt (the chip has stayed in download mode since), each
rem with its SHA-256 beside it, the MAC, and a note.  Reads only.
:take_backup
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"
if not exist "%BACKUP_DIR%" exit /b 1
echo.
echo Reading the whole flash into %BK_FULL% ...
if exist "%BK_FULL%.part" del "%BK_FULL%.part"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x0 0x1000000 "%BK_FULL%.part"
if errorlevel 1 exit /b 1
if not exist "%BK_FULL%.part" exit /b 1
call :size_is "%BK_FULL%.part" 16777216 || exit /b 1
if exist "%BK_FULL%" del "%BK_FULL%"
move /y "%BK_FULL%.part" "%BK_FULL%" >nul
if not exist "%BK_FULL%" exit /b 1
call :hash_of "%BK_FULL%" || exit /b 1
>"%BK_FULL%.sha256" echo %HASH%
copy /y "%SLICE_NOW%" "%BK_SLICE%" >nul
call :size_is "%BK_SLICE%" 28672 || exit /b 1
call :hash_of "%BK_SLICE%" || exit /b 1
>"%BK_SLICE%.sha256" echo %HASH%
>"%BACKUP_DIR%\korvo1-before-mac.txt" echo %MACLINE%
>"%BACKUP_DIR%\korvo1-before.txt" echo Read from the board on %PORT% by flash-dualboot.bat before its first write, %DATE% %TIME%.
>>"%BACKUP_DIR%\korvo1-before.txt" echo Partition table at 0x8000: Linux's (520538d3).  Bootloader at 0x2000: %BLK%.
>>"%BACKUP_DIR%\korvo1-before.txt" echo back-to-linux.bat COMport puts 0x2000 to 0x8FFF back from korvo1-2000-8fff-before.bin;
>>"%BACKUP_DIR%\korvo1-before.txt" echo back-to-linux.bat COMport whole puts the whole flash back from korvo1-before.bin.
echo   kept: %BK_FULL% and %BK_SLICE%, each with its SHA-256 beside it.
exit /b 0

:size_is
if "%~z1"=="%~2" exit /b 0
echo   %~1 is %~z1 bytes, not %~2.
exit /b 1

:explain_table
if "%TABLE%"=="linux" echo   Linux's partition table is at 0x8000: the dual-boot set is not in yet, so there is no tiny386 partition.  Run flash-dualboot.bat %PORT% first.
if "%TABLE%"=="own" echo   tiny386's own partition table is at 0x8000.  Either this Korvo-1 has the standalone set, whose bootloader has overwritten the Linux loader, so Linux must go back first with back-to-linux.bat %PORT% full be5b725 (or whole, from a backup); or this is the DOS board, whose image is written as FLASH.txt section 6 says.
if "%TABLE%"=="other" echo   The partition table at 0x8000 is none this folder knows: upstream Linux v0.4.0 or later, another board, or a blank flash.  The dual-boot table would cut that Linux's partitions short.  Nothing here is for it.
exit /b 0
