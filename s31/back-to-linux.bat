@echo off
setlocal EnableExtensions
rem Back to plain Linux on the ESP32-S31-Korvo-1.
rem
rem   back-to-linux.bat COMport [backup, small, full or whole] [be5b725 or release]
rem
rem   backup   (the default when BACKUP_DIR holds one) 0x2000 to 0x8FFF, the
rem            Linux bootloader and partition table, from the backup that
rem            flash-dualboot.bat took before its first write: exactly what
rem            was there, whichever Linux it was.  Undoes flash-dualboot.bat,
rem            which changed nothing else of Linux.
rem   small    (the default with no backup) only the Linux bootloader (0x2000)
rem            and partition table (0x8000) from the files named below.  With
rem            be5b725 this gives back exactly what was in 0x0 to 0xC1FFFF if
rem            the flash held be5b725's flash.bin; with the fork release in
rem            flash it puts be5b725's bootloader in front of the release's
rem            loader, which should start it but has not been tried.
rem   full     the whole Linux flash set again.  Needed after
rem            flash-standalone.bat, which overwrites the Linux loader.
rem            be5b725: be5b725's flash.bin at 0x0, as its FLASH.txt says.
rem            release: the fork release v2026.09.20, the files its own
rem            flash.bat writes, plus 0xFF over 0x9000 to 0x1FFFF (the gap
rem            after the table, nvs, otadata and phy_init, all blank in a
rem            fresh Linux flash) so nothing of a standalone tiny386 is left
rem            there.
rem   whole    the whole 16 MB from the backup (korvo1-before.bin) at 0x0:
rem            the flash exactly as it was before the first write, a tiny386
rem            at 0xE20000 included (it goes too).  Takes several minutes.
rem   be5b725  (the default) files from
rem            ..\..\main-be5b725\esp32s31-linux-main-be5b725-flash, the Linux
rem            this Korvo-1 runs (its card is laid out for it).
rem   release  files from esp32-s31-linux-flash-v2026.09.20.zip, unpacked in
rem            dualboot\linux-v2026.09.20.  Only if you mean to change the
rem            Linux in flash: the release kernel has not been tried with the
rem            be5b725 card.
rem
rem None of backup, small and full reach 0xE20000, so a tiny386 written there
rem by flash-dualboot.bat stays in flash, unused, until the dual-boot pair is
rem written again.
rem
rem First, in tiny386: shut the guest down (or in Linux, "poweroff").  The
rem port must be given (COM10 on Plat's PC).  Before anything is written the
rem script reads the board, which writes nothing: its MAC (which must be
rem KORVO1_MAC in flash-paths.cmd, if that is set, and for backup or whole
rem the MAC the backup was taken from) and the partition table at 0x8000.
rem "backup" and "small" need a dual-boot table there (Linux or tiny386 the
rem default, flash-dualboot.bat's "table") or Linux's; "full" and "whole"
rem also take tiny386's own.  The script checks every file against
rem its SHA-256, prints what it will write and writes only after you type Y.
rem See FLASH.txt and dualboot\DUALBOOT.md.

set "HERE=%~dp0"
call "%HERE%flash-paths.cmd"
if not defined ESPTOOL set "ESPTOOL=esptool"
if not defined BACKUP_DIR set "BACKUP_DIR=%HERE%backup"
set "REL=%HERE%dualboot\linux-v2026.09.20"
if not defined BE5B725_DIR set "BE5B725_DIR=%HERE%..\..\main-be5b725\esp32s31-linux-main-be5b725-flash"
set "BK_FULL=%BACKUP_DIR%\korvo1-before.bin"
set "BK_SLICE=%BACKUP_DIR%\korvo1-2000-8fff-before.bin"

set "PORT="
set "SCOPE="
set "LSET=be5b725"
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="full" (set "SCOPE=full") else if /i "%~1"=="small" (set "SCOPE=small") else if /i "%~1"=="backup" (set "SCOPE=backup") else if /i "%~1"=="whole" (set "SCOPE=whole") else if /i "%~1"=="release" (set "LSET=release") else if /i "%~1"=="be5b725" (set "LSET=be5b725") else (set "PORT=%~1")
shift
goto args
:argsdone
if defined PORT goto port_given
echo Usage: back-to-linux.bat COMport [backup, small, full or whole] [be5b725 or release]
echo The port must be given, for example:  back-to-linux.bat COM10
echo (that is backup if BACKUP_DIR holds one, else small be5b725)
exit /b 1
:port_given
if defined SCOPE goto scope_given
set "SCOPE=small"
if exist "%BK_SLICE%" set "SCOPE=backup"
:scope_given
set "WHAT=%SCOPE%, %LSET% files"
if /i "%SCOPE%"=="backup" set "WHAT=backup, from %BACKUP_DIR%"
if /i "%SCOPE%"=="whole" set "WHAT=whole, from %BACKUP_DIR%"

rem the tables the board may hold at 0x8000 (0xC00 bytes)
set "PT_LINUX=520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da"
set "PT_DUAL=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346"
set "PT_DUAL386=5b90d4166afc39d72a1d6c7b16f3a2e4a5cc0193a3e0eb50990a4dbb9da58226"
set "PT_OWN=afbe6410c2040c5ec6e447e94b90d91e172a3c0ed3203464b65bee8b1cbc1a26"
set "BOARD_TOUCHED="

echo.
echo Back to Linux on the Korvo-1: %WHAT%, port %PORT%
echo.
echo Checking the files ...
call :need_esptool || goto failed
if /i "%SCOPE%"=="backup" goto files_backup
if /i "%SCOPE%"=="whole" goto files_whole
call :set_%LSET%_%SCOPE% || goto failed
goto files_done
:files_backup
call :set_backup || goto failed
goto files_done
:files_whole
call :set_whole || goto failed
:files_done

echo.
echo Reading the board on %PORT% (this writes nothing) ...
set "BOARD_TOUCHED=1"
call :read_board || goto failed
if /i "%SCOPE%"=="backup" call :check_backup_mac || goto failed
if /i "%SCOPE%"=="whole" call :check_backup_mac || goto failed
if "%TABLE%"=="dual" goto board_dual
if "%TABLE%"=="dual386" goto board_dual386
if "%TABLE%"=="linux" goto board_linux
if "%TABLE%"=="own" if /i "%SCOPE%"=="full" goto board_own
if "%TABLE%"=="own" if /i "%SCOPE%"=="whole" goto board_own
call :explain_table
goto failed
:board_dual
echo   The dual-boot table: this puts Linux's own bootloader and table back.
goto board_ok
:board_dual386
echo   The dual-boot table with tiny386 the default: this puts Linux's own
echo   bootloader and table back, and Linux is the default again.
goto board_ok
:board_linux
echo   Linux's partition table is in already: this writes the Linux files again.
goto board_ok
:board_own
echo   tiny386's own partition table: a Korvo-1 with the standalone set, or the
echo   DOS board.  Check that the MAC above is the Korvo-1's before you go on.
goto board_ok
:board_ok

set "CMD1=%ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep %WRITES%"

echo.
echo Will write to the board on %PORT%:
if /i "%SCOPE%"=="backup" echo   0x2000 to 0x8FFF as they were before the first write: the Linux bootloader
if /i "%SCOPE%"=="backup" echo   and partition table, from %BK_SLICE%
if /i "%SCOPE%"=="backup" echo   This undoes flash-dualboot.bat only.  After flash-standalone.bat use whole.
if /i "%SCOPE%"=="small" echo   the %LSET% Linux bootloader at 0x2000 and the Linux partition table at 0x8000
if /i "%SCOPE%"=="small" echo   This undoes flash-dualboot.bat only.  After flash-standalone.bat use whole or full.
if /i "%LSET%_%SCOPE%"=="release_full" echo   the release v2026.09.20 set: bootloader, table, loader, OpenSBI, kernel, initramfs
if /i "%LSET%_%SCOPE%"=="release_full" echo   and 0xFF over 0x9000 to 0x1FFFF: nvs, otadata and phy_init left blank
if /i "%LSET%_%SCOPE%"=="be5b725_full" echo   be5b725's flash.bin over 0x0 to 0xC1FFFF, about 12.7 MB
if /i "%SCOPE%"=="whole" echo   the whole flash, 16 MB, from %BK_FULL%:
if /i "%SCOPE%"=="whole" echo   all of it as it was before the first write; a tiny386 at 0xE20000 goes too.
if /i "%SCOPE%"=="whole" echo   This takes several minutes; do not unplug the board while it runs.
if /i "%LSET%_%SCOPE%"=="release_small" echo   The release Linux has not been tried with this board's be5b725 card.
if /i "%LSET%_%SCOPE%"=="release_full" echo   The release Linux has not been tried with this board's be5b725 card.
if /i not "%SCOPE%"=="whole" echo Nothing at or above 0xC20000 is erased or written: a tiny386 at 0xE20000 stays.
echo.
echo Command:
echo   %CMD1%
echo.
set "ANS="
set /p "ANS=Type Y and press Enter to write, anything else stops: "
if /i not "%ANS%"=="Y" goto cancelled

call %CMD1%
if errorlevel 1 goto write_failed

echo.
echo Written.  Linux starts on every reset again; the key no longer does anything.
exit /b 0

:cancelled
echo Stopped.  Nothing was written.
call :reset_note
exit /b 1

:write_failed
echo.
echo esptool reported an error (see above).  Run this again, or with
echo "whole" or "full be5b725".  Download mode (hold BOOT, press RESET) is
echo always there for a retry.
exit /b 1

:failed
echo.
echo Stopped before writing anything.
if defined BOARD_TOUCHED call :reset_note
exit /b 1

rem Subroutines
:reset_note
echo The board may be waiting in download mode: press RESET to start it again
echo as it was.
exit /b 0

:set_backup
call :check_backup_files || exit /b 1
set WRITES=0x2000 "%BK_SLICE%"
exit /b 0

:set_whole
if exist "%BK_FULL%" goto set_whole_have
echo   no whole-flash backup at %BK_FULL%
echo   (flash-dualboot.bat or flash-standalone.bat takes one before its first
echo   write over Linux).  Use "full be5b725" instead.
exit /b 1
:set_whole_have
call :size_is "%BK_FULL%" 16777216 || exit /b 1
call :hash_of "%BK_FULL%" || exit /b 1
set "BKH="
if exist "%BK_FULL%.sha256" set /p BKH=<"%BK_FULL%.sha256"
if /i not "%HASH%"=="%BKH%" goto set_whole_bad
echo   korvo1-before.bin: SHA-256 as recorded when it was read (%BKH%)
set WRITES=0x0 "%BK_FULL%"
exit /b 0
:set_whole_bad
echo   %BK_FULL% is not the bytes recorded beside it when it was read
echo   (%BK_FULL%.sha256): it is not written.
exit /b 1

:set_release_small
call :check_hash "%REL%\bootloader\bootloader\bootloader.bin" 41d5a68752936c7b65dadb3081e4ebefc649af76cc1af450bda508e3b3455d1b || exit /b 1
call :check_hash "%REL%\bootloader\partition_table\partition-table.bin" 520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da || exit /b 1
set WRITES=0x2000 "%REL%\bootloader\bootloader\bootloader.bin" 0x8000 "%REL%\bootloader\partition_table\partition-table.bin"
exit /b 0

:set_release_full
call :check_hash "%REL%\bootloader\bootloader\bootloader.bin" 41d5a68752936c7b65dadb3081e4ebefc649af76cc1af450bda508e3b3455d1b || exit /b 1
call :check_hash "%REL%\bootloader\partition_table\partition-table.bin" 520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da || exit /b 1
call :check_hash "%HERE%dualboot\blank-0x9000-0x1ffff.bin" 667072b19b8057c9dbb4f99047f9ff683cca37b8b3893bb900cf150ae049be04 || exit /b 1
call :check_hash "%REL%\bootloader\s31-linux-loader.bin" a064c58f8c2b40f289cb27f3ed288834955442e7ca7a301e9e1a10a3d50d7603 || exit /b 1
call :check_hash "%REL%\opensbi.bin" c8c51fdf3de3668ccedb630ca0815065badd6d7cb425043ce534d60d6353b42f || exit /b 1
call :check_hash "%REL%\Image" d04e9f631b9cfeabd9ec4be21196c5edc181bd4bb857591557189832d3b64f52 || exit /b 1
call :check_hash "%REL%\linux.size" 0e0a90b415952475af61c9a76184c69d7b8f805be7dde960b933b8ee0b926a4c || exit /b 1
call :check_hash "%REL%\initramfs.cpio" 9426b1aaf1ee2746afcf193a52ad12d4c6daab1e7256d691241de5d1eb0cf7dd || exit /b 1
set WRITES=0x2000 "%REL%\bootloader\bootloader\bootloader.bin" 0x8000 "%REL%\bootloader\partition_table\partition-table.bin" 0x9000 "%HERE%dualboot\blank-0x9000-0x1ffff.bin" 0x20000 "%REL%\bootloader\s31-linux-loader.bin" 0x220000 "%REL%\opensbi.bin" 0x2a0000 "%REL%\Image" 0xa1fff4 "%REL%\linux.size" 0xa20000 "%REL%\initramfs.cpio"
exit /b 0

:set_be5b725_small
call :check_hash "%BE5B725_DIR%\bootloader.bin" 547c6a5b30551de678c14fa979ed6d50821d346988b111d531b09610b157a6fe || exit /b 1
call :check_hash "%BE5B725_DIR%\partition-table.bin" 520538d3a006f75c6f02a5a2f13ef1287c9903b118acaa82a74b774862bf16da || exit /b 1
set WRITES=0x2000 "%BE5B725_DIR%\bootloader.bin" 0x8000 "%BE5B725_DIR%\partition-table.bin"
exit /b 0

:set_be5b725_full
call :check_hash "%BE5B725_DIR%\flash.bin" f48b83f750dc96f635bf222afa6bbd60114d4d14bbbc7533bebff864ae5fb1a1 || exit /b 1
set WRITES=0x0 "%BE5B725_DIR%\flash.bin"
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
echo   %~nx1: SHA-256 matches
exit /b 0
:check_hash_bad
echo   %~1 is not the expected file:
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

:size_is
if "%~z1"=="%~2" exit /b 0
echo   %~1 is %~z1 bytes, not %~2.
exit /b 1

rem The backup's 0x2000 to 0x8FFF: there, 28672 bytes, the bytes recorded
:check_backup_files
if exist "%BK_SLICE%" goto check_backup_have
echo   no backup at %BK_SLICE%
echo   Use "small be5b725" (or "small release") instead.
exit /b 1
:check_backup_have
call :size_is "%BK_SLICE%" 28672 || exit /b 1
call :hash_of "%BK_SLICE%" || exit /b 1
set "BKH="
if exist "%BK_SLICE%.sha256" set /p BKH=<"%BK_SLICE%.sha256"
if /i not "%HASH%"=="%BKH%" goto check_backup_bad
if not exist "%BACKUP_DIR%\korvo1-before-mac.txt" goto check_backup_bad
echo   korvo1-2000-8fff-before.bin: SHA-256 as recorded when it was read (%BKH%)
exit /b 0
:check_backup_bad
echo   The backup in %BACKUP_DIR% is not whole (a file missing, or not the
echo   bytes recorded beside it when it was read): it is not written.
echo   Use "small be5b725" instead, or "full be5b725".
exit /b 1

rem The board's MAC against the one the backup was taken from
:check_backup_mac
if not exist "%BACKUP_DIR%\korvo1-before-mac.txt" goto check_backup_mac_bad
if not defined MACLINE goto check_backup_mac_bad
findstr /c:"%MACLINE%" "%BACKUP_DIR%\korvo1-before-mac.txt" >nul && exit /b 0
:check_backup_mac_bad
echo   The backup in %BACKUP_DIR% was taken from a board whose MAC is not
echo   this one's: it is not written to this board.
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

:explain_table
if "%TABLE%"=="own" if /i "%SCOPE%"=="backup" echo   tiny386's own partition table is at 0x8000: the standalone set has overwritten the Linux loader, so the backup's 0x2000 to 0x8FFF alone cannot bring Linux back.  Use:  back-to-linux.bat %PORT% whole   (or full be5b725)
if "%TABLE%"=="own" if /i "%SCOPE%"=="small" echo   tiny386's own partition table is at 0x8000: the standalone set has overwritten the Linux loader, so "small" cannot bring Linux back.  Use:  back-to-linux.bat %PORT% whole   (or full be5b725)
if "%TABLE%"=="other" echo   The partition table at 0x8000 is none this folder knows: upstream Linux v0.4.0 or later, another board, or a blank flash.  This script stops; FLASH.txt section 4 has the commands by hand.
exit /b 0
