@echo off
setlocal EnableExtensions
rem tiny386 beside Linux on the ESP32-S31-Korvo-1: write the dual-boot set.
rem
rem   flash-dualboot.bat COMport [all, app or table] [linux or tiny386]
rem
rem   all  (the default) tiny386 at 0xE20000, then the dual-boot bootloader
rem        at 0x2000 and a dual-boot partition table at 0x8000.  Nothing
rem        of Linux is written.  For Linux be5b725 or the fork release
rem        v2026.09.20 only (both have the same partition table).  tiny386
rem        goes first, in an esptool run of its own: if that run is cut
rem        short, Linux is still as it was.
rem   app  tiny386 at 0xE20000 only, for a newer tiny386 once "all" is in.
rem        Which system starts by default stays as it is.
rem   table  only the partition table at 0x8000, once "all" is in: it
rem        chooses which system starts by default, and needs linux or
rem        tiny386.  The bootloader at 0x2000 must be the dual-boot one (the
rem        script reads it), or the key would do nothing.
rem
rem   linux    the dual-boot table with the Linux loader as the factory app
rem            (images\korvo1-dualboot): RESET with no key held starts
rem            Linux, VOL+ held through RESET starts tiny386.
rem   tiny386  the same table with the two app subtypes swapped
rem            (images\korvo1-dualboot-tiny386-default): RESET with no key
rem            held starts tiny386, VOL+ held through RESET starts Linux.
rem   "all" with neither keeps the default the board has (Linux over
rem   Linux's own table).  Both tables go with the same bootloader.
rem
rem First, in Linux: "poweroff", and wait for it (in tiny386: shut the guest
rem down).  The script resets the board into download mode before it reads
rem it, and keeps it there (esptool -a no-reset) until the last write, which
rem resets it to start.
rem
rem The port must be given (COM10 on Plat's PC).  Before anything is
rem written the script reads the board, which writes nothing: its MAC (which
rem must be KORVO1_MAC in flash-paths.cmd, if that is set), and the partition
rem table at 0x8000, which must be Linux's for "all" (or a dual-boot one, to
rem write the set again) and a dual-boot one for "app" and "table".  Over
rem Linux's table it also names the bootloader at 0x2000 by SHA-256 and, the
rem first time, reads the whole flash into BACKUP_DIR (backup\ beside this
rem script) before writing, so that back-to-linux.bat can put back exactly
rem what was there.  If a write by these scripts over Linux's table was
rem cut short (the start of the dual-boot bootloader or of tiny386's own at
rem 0x2000, or 0xFF), the backup taken before it is kept and this run
rem finishes the install; no backup is ever taken of such a state.  Over a
rem dual-boot table, "app" and "table" go on only with the dual-boot
rem bootloader at 0x2000.  The tiny386 image is TINY386_BIN in
rem flash-paths.cmd.  The script checks the files, prints what it will do
rem and writes only after you type Y.  See FLASH.txt and
rem dualboot\DUALBOOT.md.

set "HERE=%~dp0"
call "%HERE%flash-paths.cmd"
if not defined ESPTOOL set "ESPTOOL=esptool"
if not defined BACKUP_DIR set "BACKUP_DIR=%HERE%backup"

set "PORT="
set "MODE=all"
set "DEF="
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="all" (set "MODE=all") else if /i "%~1"=="app" (set "MODE=app") else if /i "%~1"=="table" (set "MODE=table") else if /i "%~1"=="linux" (set "DEF=linux") else if /i "%~1"=="tiny386" (set "DEF=tiny386") else (set "PORT=%~1")
shift
goto args
:argsdone
if defined PORT goto port_given
echo Usage: flash-dualboot.bat COMport [all, app or table] [linux or tiny386]
echo The port must be given, for example:  flash-dualboot.bat COM10
echo tiny386 the default at reset:          flash-dualboot.bat COM10 table tiny386
exit /b 1
:port_given
if /i not "%MODE%"=="table" goto mode_ok
if defined DEF goto mode_ok
echo Usage: flash-dualboot.bat COMport table linux     (Linux the default at reset)
echo    or: flash-dualboot.bat COMport table tiny386   (tiny386 the default at reset)
exit /b 1
:mode_ok
if /i not "%MODE%"=="app" goto mode_ok2
if not defined DEF goto mode_ok2
echo "app" writes tiny386 only and leaves the default at reset as it is.  To
echo choose the default:  flash-dualboot.bat %PORT% table %DEF%
exit /b 1
:mode_ok2
set "DEF_NAME=Linux"
if "%DEF%"=="tiny386" set "DEF_NAME=tiny386"

set "BL=%HERE%images\korvo1-dualboot\bootloader.bin"
set "PT=%HERE%images\korvo1-dualboot\partition-table.bin"
set "PT386=%HERE%images\korvo1-dualboot-tiny386-default\partition-table.bin"
set "BL_SHA=b57469cd1542fe83a4f468f93a7e8e9bfa5af138f15a38e6867a0a54fccff29f"
set "BL_SIZE=22352"
set "PT_SHA=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346"
set "PT386_SHA=5b90d4166afc39d72a1d6c7b16f3a2e4a5cc0193a3e0eb50990a4dbb9da58226"
rem the tables the board may hold at 0x8000 (0xC00 bytes)
set "PT_LINUX=520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da"
set "PT_DUAL=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346"
set "PT_DUAL386=5b90d4166afc39d72a1d6c7b16f3a2e4a5cc0193a3e0eb50990a4dbb9da58226"
set "PT_OWN=afbe6410c2040c5ec6e447e94b90d91e172a3c0ed3203464b65bee8b1cbc1a26"
rem the Linux bootloaders this folder knows: SHA-256 of their bytes at 0x2000
set "BL_BE5B725=547c6a5b30551de678c14fa979ed6d50821d346988b111d531b09610b157a6fe"
set "BL_BE5B725_SIZE=22000"
set "BL_RELEASE=41d5a68752936c7b65dadb3081e4ebefc649af76cc1af450bda508e3b3455d1b"
set "BL_RELEASE_SIZE=23024"
rem the first 256 bytes at 0x2000 (one flash page, written first and whole)
rem that a write by these scripts leaves when it is cut short: the start of
rem the dual-boot bootloader, of tiny386's own (flash-standalone.bat), or
rem an erased page.  Every bootloader this folder knows differs from the
rem others by byte 28.
set "HEAD_DUAL=4fd69a588e8b199d77d96b082938581e8fd4d1a2244252932ed9759e939068b0"
set "HEAD_OWN=7d9e47c773f62dfc9671ae4ae90b7ba32c38e0fb8c83f80b5c5e140e2bd8dda2"
set "HEAD_FF=3d6876a0146de8576eb2395a858de1213d1b92c65b779df3a331cfd5a4584546"
rem the DOS board's image in this folder, which must not go to a Korvo-1
set "DOS_BIN=%HERE%images\dosboard\tiny386.bin"
rem the tiny386 partition in the dual-boot tables: 0xE20000, 0x1E0000 bytes
set "APP_MAX=1966080"
rem the backup: the whole flash, and 0x2000 to 0x8FFF, as they were
set "BK_FULL=%BACKUP_DIR%\korvo1-before.bin"
set "BK_SLICE=%BACKUP_DIR%\korvo1-2000-8fff-before.bin"
set "BOARD_TOUCHED="
set "TABLE=none"

echo.
echo tiny386 dual boot for the Korvo-1, mode %MODE%, port %PORT%
echo.
echo Before this: in Linux, "poweroff", and wait for it to finish (in tiny386,
echo shut the guest down).  This script resets the board into download mode
echo and keeps it there until the last write, so that neither system runs
echo with the card half written.
echo.
echo Checking the files ...
call :need_esptool || goto failed
if /i "%MODE%"=="app" goto check_app
call :check_hash "%BL%" %BL_SHA% || goto failed
call :check_hash "%PT%" %PT_SHA% || goto failed
call :check_hash "%PT386%" %PT386_SHA% || goto failed
if /i "%MODE%"=="table" goto check_done
:check_app
call :check_image "%TINY386_BIN%" %APP_MAX% || goto failed
:check_done

echo.
echo Reading the board on %PORT% (this writes nothing) ...
set "BOARD_TOUCHED=1"
call :read_board || goto failed
if /i "%MODE%"=="app" goto board_app
if /i "%MODE%"=="table" goto board_table
if "%TABLE%"=="linux" goto board_linux
if "%TABLE%"=="dual" goto board_dual_again
if "%TABLE%"=="dual386" goto board_dual_again
goto board_wrong
:board_app
if "%TABLE%"=="dual" goto board_dual_app
if "%TABLE%"=="dual386" goto board_dual_app
goto board_wrong
:board_table
if "%TABLE%"=="dual" goto board_dual_table
if "%TABLE%"=="dual386" goto board_dual_table
goto board_wrong
:board_linux
echo   Linux's partition table, as this set expects: be5b725 or the release.
call :read_linux_boot || goto failed
goto board_ok
:board_dual_again
echo   A dual-boot table is in already: this writes the set again.
goto board_ok
:board_dual_app
echo   A dual-boot table is in: tiny386 at 0xE20000 can be replaced, if the
echo   bootloader is the dual-boot one.
call :read_dual_boot || goto failed
if "%BLK%"=="dual" goto board_ok
call :explain_bootloader_app
goto failed
:board_dual_table
echo   A dual-boot table is in: the table alone can be changed, if the
echo   bootloader is the dual-boot one.
call :read_dual_boot || goto failed
if "%BLK%"=="dual" goto board_ok
call :explain_bootloader
goto failed
:board_wrong
call :explain_table
goto failed
:board_ok

rem Which system starts with no key held, now (NOW) and after this (AFTER),
rem and the table to write (PTW) for "all" and "table".
set "NOW="
if "%TABLE%"=="dual" set "NOW=linux"
if "%TABLE%"=="dual386" set "NOW=tiny386"
set "AFTER=%NOW%"
if /i "%MODE%"=="app" goto default_decided
if defined DEF goto default_given
set "DEF=linux"
if "%NOW%"=="tiny386" set "DEF=tiny386"
:default_given
set "AFTER=%DEF%"
set "PTW=%PT%"
set "PTW_NAME=images\korvo1-dualboot\partition-table.bin"
set "PTW_WHAT=the Linux table plus tiny386 at 0xE20000, Linux the default"
if not "%DEF%"=="tiny386" goto default_table_set
set "PTW=%PT386%"
set "PTW_NAME=images\korvo1-dualboot-tiny386-default\partition-table.bin"
set "PTW_WHAT=the same with the two app subtypes swapped, tiny386 the default"
:default_table_set
if /i not "%MODE%"=="table" goto default_decided
if "%NOW%"=="%DEF%" goto table_same
:default_decided
call :says NOW_SAYS "%NOW%"
call :says AFTER_SAYS "%AFTER%"

rem The backup: before the first write over Linux's table only.  One that
rem is there already is kept, if it is of this board and of what the flash
rem holds now, or of what it held before a write by these scripts that was
rem cut short (HEADK); otherwise the script stops rather than overwrite it.
rem A cut write's state is never taken as a backup.
set "NEED_BACKUP="
if /i not "%MODE%"=="all" goto backup_decided
if not "%TABLE%"=="linux" goto backup_decided
if exist "%BK_SLICE%" goto backup_compare
if not "%HEADK%"=="other" goto backup_cut_none
set "NEED_BACKUP=1"
goto backup_decided
:backup_cut_none
call :explain_cut_none
goto failed
:backup_compare
call :check_backup || goto failed
call :hash_of "%SLICE_NOW%" || goto failed
set "NOWH=%HASH%"
set "BKH="
set /p BKH=<"%BK_SLICE%.sha256"
if /i "%NOWH%"=="%BKH%" goto backup_same
if not "%HEADK%"=="other" goto backup_cut
echo   %BACKUP_DIR% holds a backup of this board whose 0x2000 to 0x8FFF
echo   differ from this flash now, and the bootloader at 0x2000 is not one
echo   these scripts write: Linux was changed after the backup was taken
echo   (another Linux flashed, say).  The backup is not overwritten.  To keep
echo   it and take a new one of the flash as it is now, rename that folder
echo   (keep it), then run this again.  To put the board back as the backup
echo   has it instead:  back-to-linux.bat %PORT%
goto failed
:backup_cut
echo   The backup in %BACKUP_DIR% is of this board as it was before that cut
echo   write (Linux's table, as now): it is kept, no new one is taken, and
echo   this run finishes the install.
goto backup_decided
:backup_same
if not "%HEADK%"=="other" goto backup_of_cut
echo   The backup in %BACKUP_DIR% is of this board and of this same bootloader
echo   and table: it is kept, and no new one is taken.
goto backup_decided
:backup_of_cut
call :explain_backup_of_cut
goto failed
:backup_decided

set "A1=no-reset"
if /i not "%MODE%"=="all" set "A1=hard-reset"
set WRITES1=0xE20000 "%TINY386_BIN%"
if /i "%MODE%"=="table" set WRITES1=0x8000 "%PTW%"
set WRITES2=0x2000 "%BL%" 0x8000 "%PTW%"
set "CMD1=%ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a %A1% write-flash -fm keep -ff keep -fs keep %WRITES1%"
set "CMD2=%ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep %WRITES2%"

echo.
echo Will do, on the board on %PORT%:
if defined NEED_BACKUP echo   first read the whole flash, 16 MB, which takes a few minutes, into
if defined NEED_BACKUP echo     %BK_FULL%
if defined NEED_BACKUP echo   and 0x2000 to 0x8FFF into %BK_SLICE%
if defined NEED_BACKUP echo   (back-to-linux.bat puts them back; nothing is written if this fails)
if /i "%MODE%"=="table" goto plan_table
echo   write 0xE20000  %TINY386_BIN%
echo             tiny386, %APP_SIZE% of %APP_MAX% bytes
if /i "%MODE%"=="all" echo   then, in a second esptool run:
if /i "%MODE%"=="all" echo   0x002000  images\korvo1-dualboot\bootloader.bin       the dual-boot bootloader, the test app while GPIO42 is held low
if /i "%MODE%"=="all" echo   0x008000  %PTW_NAME%
if /i "%MODE%"=="all" echo             %PTW_WHAT%
echo Only the 4 KB sectors these files cover are erased.  Linux, 0x9000 to
echo 0xE1FFFF, is left as it is.
goto plan_said
:plan_table
echo   write 0x008000  %PTW_NAME%
echo             %PTW_WHAT%
echo Only the 4 KB sector at 0x8000 is erased and written: not tiny386, not
echo the bootloader, nothing of Linux (0x9000 to 0xE1FFFF).
:plan_said
echo At RESET now:      %NOW_SAYS%
echo After this write:  %AFTER_SAYS%
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
if errorlevel 1 goto write_failed_%MODE%
if /i not "%MODE%"=="all" goto written
call %CMD2%
if errorlevel 1 goto write_failed_boot

:written
echo.
if "%AFTER%"=="tiny386" goto written_tiny386
echo Written.  Press RESET with no key held for Linux.  For tiny386: hold the
echo VOL+ key, press and release RESET, keep holding VOL+ for two seconds.
exit /b 0
:written_tiny386
echo Written.  Press RESET with no key held for tiny386.  For Linux: hold the
echo VOL+ key, press and release RESET, keep holding VOL+ for two seconds.
exit /b 0

:table_same
echo   The table at 0x8000 already makes %DEF_NAME% the default: nothing to write.
call :reset_note
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

:write_failed_all
:write_failed_app
echo.
echo esptool reported an error (see above) while writing tiny386 at 0xE20000.
if "%TABLE%"=="dual386" goto write_failed_app_386
echo The bootloader and partition table were not touched: Linux starts as it
echo did.  Press RESET with nothing held, or run this again.
exit /b 1
:write_failed_app_386
echo The bootloader and partition table were not touched.  A tiny386 left half
echo written does not pass the bootloader's check, which then starts Linux
echo instead, key or no key.  Run this again to finish.
exit /b 1

:write_failed_table
echo.
echo esptool reported an error (see above) while writing the table at 0x8000.
echo If the board does not start now, the table was cut part way and no script
echo here knows it any more.  Write the Linux-default one back by hand:
echo   %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 write-flash -fm keep -ff keep -fs keep 0x8000 "%PT%"
echo then run this again.  Download mode (hold BOOT, press RESET) is always
echo there for a retry.
exit /b 1

:write_failed_boot
echo.
echo esptool reported an error (see above) while writing the bootloader and
echo partition table, so the board may start nothing now.  Either of these
echo mends it, and each reads the board first:
echo   back-to-linux.bat %PORT%              plain Linux (from the backup, if one is there)
echo   flash-dualboot.bat %PORT% all %DEF%    this again: it finishes the set, and keeps
echo                                         the backup taken before this run
echo If either says the table at 0x8000 is none it knows, the write was cut
echo inside the table itself: FLASH.txt, "If something goes wrong", has the
echo command by hand.  Download mode (hold BOOT, press RESET) is always there
echo for a retry.
exit /b 1

:failed
echo.
echo Stopped before writing anything.
if defined BOARD_TOUCHED call :reset_note
exit /b 1

rem Subroutines
:reset_note
if "%TABLE%"=="dual386" goto reset_note_386
echo The board may be waiting in download mode: press RESET with nothing held
echo to start Linux again.
exit /b 0
:reset_note_386
echo The board may be waiting in download mode: press RESET with nothing held
echo to start tiny386 again, or hold VOL+ through RESET for Linux.
exit /b 0

rem What a reset starts with each table: %1 the variable to set, %2 the
rem system that starts with no key held (linux, tiny386, or empty for Linux's
rem own table)
:says
set "%~1=no key: Linux; VOL+ held: tiny386"
if "%~2"=="tiny386" set "%~1=no key: tiny386; VOL+ held: Linux"
if "%~2"=="" set "%~1=Linux, key or no key (Linux's own bootloader and table)"
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
for %%d in ("%~dp1.") do echo   %%~nxd\%~nx1: SHA-256 as built (%~2)
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
if not exist "%DOS_BIN%" goto check_image_ok
call :hash_of "%~1" || goto check_image_bad
set "IMGH=%HASH%"
call :hash_of "%DOS_BIN%" || goto check_image_ok
if /i not "%IMGH%"=="%HASH%" goto check_image_ok
echo   %~1 is the DOS board's image (images\dosboard\tiny386.bin), not the
echo   Korvo-1's.  Set TINY386_BIN in flash-paths.cmd to
echo   images\korvo1-dualboot\tiny386.bin.
exit /b 1
:check_image_ok
echo   %~nx1: %APP_SIZE% bytes, a valid ESP32-S31 app image:
findstr /b /c:"Project name" /c:"App version" /c:"ESP-IDF" /c:"Validation hash" "%II%"
exit /b 0
:check_image_bad
echo   esptool image-info does not accept %~1 as a valid ESP32-S31 image:
type "%II%"
exit /b 1

rem The board on PORT: its MAC printed (and held to KORVO1_MAC if that is
rem set), and TABLE set to linux, dual (the dual boot, Linux the default),
rem dual386 (the dual boot, tiny386 the default), own (tiny386's own table)
rem or other, from the SHA-256 of 0x8000 to 0x8BFF.  Reads only; the chip
rem is left in download mode (-a no-reset).
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
certutil -hashfile "%TNOW%" SHA256 | findstr /i /c:"%PT_DUAL386%" >nul && set "TABLE=dual386"
certutil -hashfile "%TNOW%" SHA256 | findstr /i /c:"%PT_OWN%" >nul && set "TABLE=own"
echo   partition table at 0x8000: %TABLE%
exit /b 0
:read_board_bad
echo   esptool could not read the board on %PORT%:
type "%RB%"
echo   Is the Korvo-1 on that port (the UART Type-C), and no terminal holding it?
exit /b 1

rem Over a dual-boot table, for "table": BLK set to dual if the bootloader
rem at 0x2000 is the dual-boot one (the VOL+ key's), else other.  Reads only.
:read_dual_boot
set "BLK=other"
set "TB=%TEMP%\tiny386-boot-now.bin"
if exist "%TB%" del "%TB%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x2000 %BL_SIZE% "%TB%" > "%RB%" 2>&1
if errorlevel 1 goto read_dual_boot_bad
call :hash_of "%TB%" || goto read_dual_boot_bad
if /i "%HASH%"=="%BL_SHA%" set "BLK=dual"
if "%BLK%"=="dual" echo   bootloader at 0x2000: the dual-boot one (b57469cd), with the VOL+ key
if "%BLK%"=="other" echo   bootloader at 0x2000: not the dual-boot one (b57469cd)
exit /b 0
:read_dual_boot_bad
echo   esptool could not read 0x2000 on %PORT%:
type "%RB%"
exit /b 1

rem Over Linux's table: the bootloader at 0x2000 named by SHA-256 (BLK
rem be5b725, release or other), its first 256 bytes (HEADK dual, own or
rem erased: a write by these scripts cut short; else other), and 0x2000 to
rem 0x8FFF read into SLICE_NOW.  Reads only.
:read_linux_boot
set "BLK=other"
set "HEADK=other"
set "TB=%TEMP%\tiny386-boot-now.bin"
set "SLICE_NOW=%TEMP%\tiny386-2000-8fff-now.bin"
if exist "%TB%" del "%TB%"
call %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a no-reset read-flash 0x2000 256 "%TB%" > "%RB%" 2>&1
if errorlevel 1 goto read_linux_boot_bad
call :hash_of "%TB%" || goto read_linux_boot_bad
if /i "%HASH%"=="%HEAD_DUAL%" set "HEADK=dual"
if /i "%HASH%"=="%HEAD_OWN%" set "HEADK=own"
if /i "%HASH%"=="%HEAD_FF%" set "HEADK=erased"
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
if not "%BLK%"=="other" exit /b 0
if "%HEADK%"=="dual" echo   bootloader at 0x2000: the dual-boot one, or the start of it, in front of
if "%HEADK%"=="own" echo   bootloader at 0x2000: tiny386's own, or the start of it, in front of
if "%HEADK%"=="erased" echo   bootloader at 0x2000: none, the page erased (0xFF), in front of
if not "%HEADK%"=="other" echo   Linux's table: a write by these scripts was cut short there.
if "%HEADK%"=="other" echo   bootloader at 0x2000: none this folder knows (neither be5b725's nor
if "%HEADK%"=="other" echo   the release's).  The backup taken before writing is then the only
if "%HEADK%"=="other" echo   exact way back: keep it.
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
if "%TABLE%"=="linux" if /i "%MODE%"=="app" echo   Linux's partition table is at 0x8000: the dual-boot set is not in yet, so there is no tiny386 partition.  Run flash-dualboot.bat %PORT% first.
if "%TABLE%"=="linux" if /i "%MODE%"=="table" echo   Linux's partition table is at 0x8000: the dual-boot set is not in yet, so there is no table to change.  Put the whole set in with %DEF_NAME% the default:  flash-dualboot.bat %PORT% all %DEF%
if "%TABLE%"=="own" echo   tiny386's own partition table is at 0x8000.  Either this Korvo-1 has the standalone set, whose bootloader has overwritten the Linux loader, so Linux must go back first with back-to-linux.bat %PORT% full be5b725 (or whole, from a backup); or this is the DOS board, whose image is written as FLASH.txt section 6 says.
if "%TABLE%"=="other" echo   The partition table at 0x8000 is none this folder knows: upstream Linux v0.4.0 or later, another board, or a blank flash.  The dual-boot table would cut that Linux's partitions short.  Nothing here is for it.
exit /b 0

:explain_bootloader
echo   The bootloader at 0x2000 is not the dual-boot one, so the VOL+ key does
echo   nothing with it: with tiny386 the default, Linux could then not be
echo   started without flashing.  So the table is not written alone here.
echo   Write the whole set, bootloader included:
echo     flash-dualboot.bat %PORT% all %DEF%
echo   or go back to plain Linux:  back-to-linux.bat %PORT%
exit /b 0

:explain_bootloader_app
echo   The bootloader at 0x2000 is not the dual-boot one: the VOL+ key does
echo   nothing with it, so the table's default starts on every reset (or,
echo   after a write cut short, nothing starts).  tiny386 alone would not mend
echo   that.  Write the whole set, bootloader included (the default at reset
echo   stays as the table has it):
echo     flash-dualboot.bat %PORT% all
echo   or go back to plain Linux:  back-to-linux.bat %PORT%
exit /b 0

:explain_backup_of_cut
echo   The backup in %BACKUP_DIR% holds this same state, a write cut short
echo   (a script older than v3.9 took it): back-to-linux.bat would put the
echo   broken bootloader back from it.  Nothing is written.  Rename that
echo   folder (keep it).  If the backup taken before the cut write is in
echo   another folder, give that one this name instead and run this again: it
echo   then finishes the install.  Otherwise put a Linux bootloader back first:
echo     back-to-linux.bat %PORT% small            (be5b725's)
echo     back-to-linux.bat %PORT% small release    (the fork release's)
echo   then run this again, which takes a new backup of that.
exit /b 0

:explain_cut_none
echo   %BACKUP_DIR% holds no backup, and a backup of this cut state would
echo   later put a broken bootloader back, so none is taken and nothing is
echo   written.  If the backup taken before the cut write is in a folder that
echo   was renamed, give it its name again (or set BACKUP_DIR in
echo   flash-paths.cmd to it) and run this again: it then finishes the
echo   install.  With no such backup, put a Linux bootloader back first:
echo     back-to-linux.bat %PORT% small            (be5b725's)
echo     back-to-linux.bat %PORT% small release    (the fork release's)
echo   then run this again, which takes a new backup of that.
exit /b 0
