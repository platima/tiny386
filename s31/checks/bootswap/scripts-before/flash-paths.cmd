@rem Paths for flash-dualboot.bat, flash-standalone.bat and back-to-linux.bat.
@rem Edit here only.  A value already set in the environment wins, so this
@rem also works:
@rem   set TINY386_BIN=D:\somewhere\tiny386.bin
@rem   flash-dualboot.bat COM10 app
@rem The port is always given on the command line, never here.

@rem The dual-boot set (images\korvo1-dualboot): tiny386 for its test
@rem partition at 0xE20000.  The bootloader and table beside it are fixed;
@rem flash-dualboot.bat checks them by SHA-256.
@if not defined TINY386_BIN set "TINY386_BIN=%~dp0images\korvo1-dualboot\tiny386.bin"

@rem The standalone set (images\korvo1-standalone): tiny386's own bootloader,
@rem partition table and app, and the size of the factory app partition in
@rem that table (0x10000, 1792K) in bytes.  Its tiny386.bin is the same build
@rem as the dual-boot one but for the ELF hash it carries (BUILD-NOTES.md).
@rem flash-standalone.bat checks the bootloader and table by SHA-256, so
@rem other files here stop it until its pinned values are changed too.
@if not defined SA_BOOTLOADER set "SA_BOOTLOADER=%~dp0images\korvo1-standalone\bootloader.bin"
@if not defined SA_TABLE set "SA_TABLE=%~dp0images\korvo1-standalone\partition-table.bin"
@if not defined SA_TINY386_BIN set "SA_TINY386_BIN=%~dp0images\korvo1-standalone\tiny386.bin"
@if not defined SA_APP_SIZE set "SA_APP_SIZE=1835008"

@rem Where the first write over Linux keeps what it found: the whole flash
@rem (korvo1-before.bin, 16 MB) and 0x2000 to 0x8FFF (the bootloader and
@rem partition table) as they were, read before anything is written.
@rem back-to-linux.bat puts them back from here.  Keep this folder.
@if not defined BACKUP_DIR set "BACKUP_DIR=%~dp0backup"

@rem The Korvo-1's MAC as step 1 prints it, for example 7c:2c:67:aa:bb:cc.
@rem Empty: any board on the port is taken.  Once set, the three Korvo-1
@rem scripts stop on any other board, such as the DOS board, before writing.
@if not defined KORVO1_MAC set "KORVO1_MAC="

@rem esptool 5.4 or later.  "python -m esptool" also works here.
@if not defined ESPTOOL set "ESPTOOL=esptool"
