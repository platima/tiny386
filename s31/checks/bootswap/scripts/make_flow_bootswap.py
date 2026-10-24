# Flow harnesses for the v3 .bat files with tiny386 as a possible default at
# reset (flash-dualboot.bat's "table" mode and the tiny386-default table,
# dual386).  Made from bat-harness-make-flow-v3.7.py: the same 39 cases first,
# unchanged, then the new ones.  Each script is copied whole, with HERE
# pointed at the v3 folder (so the images are the package's) and only its
# write calls ("call %CMD1%", "call %CMD2%", the only lines that run
# esptool's write-flash) replaced by "echo WOULD WRITE".  So the file
# checks, the board reads, the backup and its checks all run, against a stub
# esptool that never touches a port and logs every call it gets.
#
#   python3 make_flow_bootswap.py SRCDIR OUTDIR II_GOOD
#     SRCDIR  the .bat files to test (CRLF)
#     OUTDIR  where the harness goes (on the Windows side)
#     II_GOOD esptool image-info's output for images\korvo1-dualboot\tiny386.bin
import os, sys, shutil, hashlib

BS = chr(92)
CRLF = chr(13) + chr(10)
V3_WIN = "D:" + BS + "Repos" + BS + "esp32-s31-linux-release" + BS + "tiny386-idf" + BS + "v3"
V3 = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/"
BE = "/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash/"
REL = V3 + "dualboot/linux-v2026.09.20/bootloader/"
SRC, OUT, II_GOOD = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)

for name in ["flash-dualboot.bat", "flash-standalone.bat", "back-to-linux.bat"]:
    t = open(os.path.join(SRC, name), "rb").read().decode("ascii")
    assert "\n" not in t.replace(CRLF, ""), (name, "a line that does not end in CRLF")
    lines = t.split(CRLF)
    out = []
    writes = 0
    for line in lines:
        s = line.strip()
        if s.startswith("call %ESPTOOL%"):
            assert "write-flash" not in s and "erase" not in s, (name, line)
        if s in ("call %CMD1%", "call %CMD2%"):
            out.append(line.replace("call %CMD", "echo WOULD WRITE: %CMD"))
            writes += 1
            continue
        out.append(line)
    assert writes >= 1, name
    body = CRLF.join(out)
    body2 = body.replace('set "HERE=%~dp0"', 'set "HERE=' + V3_WIN + BS + '"')
    assert body2 != body, name
    for line in body2.split(CRLF):
        assert not line.strip().startswith("call %CMD"), (name, line)
    open(os.path.join(OUT, "flow-" + name.replace(".bat", ".cmd")), "wb").write(body2.encode("ascii"))
    print(name, ":", writes, "write calls cut out")

def pad(b, n):
    assert len(b) <= n
    return b + b"\xff" * (n - len(b))

bl_be = open(BE + "bootloader.bin", "rb").read()
bl_rel = open(REL + "bootloader/bootloader.bin", "rb").read()
bl_dual = open(V3 + "images/korvo1-dualboot/bootloader.bin", "rb").read()
bl_own = open(V3 + "images/korvo1-standalone/bootloader.bin", "rb").read()
pt_linux = open(BE + "partition-table.bin", "rb").read()
pt_dual = open(V3 + "images/korvo1-dualboot/partition-table.bin", "rb").read()
pt_dual386 = open(V3 + "images/korvo1-dualboot-tiny386-default/partition-table.bin", "rb").read()
pt_own = open(V3 + "images/korvo1-standalone/partition-table.bin", "rb").read()
bl_odd = bytearray(bl_be); bl_odd[100] ^= 1; bl_odd = bytes(bl_odd)
pt_other = bytearray(pt_linux); pt_other[0x40] ^= 1; pt_other = bytes(pt_other)
assert hashlib.sha256(bl_be).hexdigest().startswith("547c6a5b")
assert hashlib.sha256(bl_rel).hexdigest().startswith("41d5a687")
assert hashlib.sha256(bl_dual).hexdigest().startswith("b57469cd") and len(bl_dual) == 22352
assert hashlib.sha256(pt_linux).hexdigest().startswith("520538d3")
assert hashlib.sha256(pt_dual).hexdigest().startswith("d41cba3c")
assert hashlib.sha256(pt_dual386).hexdigest().startswith("5b90d416")
fb = open(BE + "flash.bin", "rb").read()
assert fb[0x2000:0x2000 + len(bl_be)] == bl_be and fb[0x8000:0x8C00] == pt_linux[:0xC00]

states = {
    "linux-be5b725": (bl_be, pt_linux),
    "linux-release": (bl_rel, pt_linux),
    "linux-otherbl": (bl_odd, pt_linux),
    "dual": (bl_dual, pt_dual),
    "own": (bl_own, pt_own),
    "other": (bl_be, pt_other),
    # new: the dual boot with tiny386 the default, and two cut-off states
    # (be5b725's bootloader in front of either dual-boot table)
    "dual386": (bl_dual, pt_dual386),
    "dual386-be5b725bl": (bl_be, pt_dual386),
    "dual-be5b725bl": (bl_be, pt_dual),
}
for st, (bl, pt) in states.items():
    region = pad(bl, 0x6000) + pad(pt, 0x1000)          # 0x2000 to 0x8FFF
    assert len(region) == 0x7000
    def put(addr, size, data):
        open(os.path.join(OUT, "rf-%s-%s-%s.bin" % (st, addr, size)), "wb").write(data)
    put("0x8000", "0xC00", region[0x6000:0x6C00])
    put("0x2000", "22000", region[:22000])
    put("0x2000", "23024", region[:23024])
    put("0x2000", "22352", region[:22352])
    put("0x2000", "0x7000", region)
open(os.path.join(OUT, "rf-full.bin"), "wb").write(pad(fb, 16 << 20))

shutil.copy(II_GOOD, os.path.join(OUT, "ii-good.txt"))
ii = open(os.path.join(OUT, "ii-good.txt"), "rb").read().replace(b"(valid)", b"(invalid)")
open(os.path.join(OUT, "ii-invalid.txt"), "wb").write(ii)

fake = [
    "@echo off",
    "rem stub for esptool: never touches a port.  FAKE_FLASH names the flash the",
    "rem board holds (linux-be5b725, linux-release, linux-otherbl, dual, own,",
    "rem other, dual386, dual386-be5b725bl, dual-be5b725bl); FAKE_BOARD=none: no",
    "rem board; FAKE_MAC: its MAC; FAKE_FAIL=full: the 16 MB read fails.  Every",
    "rem call is logged in calls.log.",
    'set "STUB=%~dp0"',
    '>>"%STUB%calls.log" echo esptool %*',
    ":scan",
    'if "%~1"=="" goto other',
    'if "%~1"=="version" goto ver',
    'if "%~1"=="image-info" goto ii',
    'if "%~1"=="read-mac" goto mac',
    'if "%~1"=="read-flash" goto rf',
    'if "%~1"=="write-flash" goto wf',
    'if "%~1"=="erase-flash" goto wf',
    'if "%~1"=="erase-region" goto wf',
    "shift",
    "goto scan",
    ":ver",
    "echo esptool v5.4.0",
    "echo 5.4.0",
    "exit /b 0",
    ":ii",
    'if "%FAKE_II%"=="invalid" goto ii_bad',
    'type "%STUB%ii-good.txt"',
    "exit /b 0",
    ":ii_bad",
    'type "%STUB%ii-invalid.txt"',
    "exit /b 0",
    ":mac",
    'if "%FAKE_BOARD%"=="none" goto noboard',
    'set "M=%FAKE_MAC%"',
    'if not defined M set "M=7c:2c:67:00:00:01"',
    "echo Chip type:          ESP32-S31 (stub)",
    "echo MAC:                %M%",
    "exit /b 0",
    ":rf",
    'if "%FAKE_BOARD%"=="none" goto noboard',
    'if "%~2_%~3"=="0x0_0x1000000" goto rf_full',
    'if not exist "%STUB%rf-%FAKE_FLASH%-%~2-%~3.bin" goto rf_none',
    'copy /y "%STUB%rf-%FAKE_FLASH%-%~2-%~3.bin" "%~4" >nul',
    "echo Read %~3 bytes at %~2 (stub)",
    "exit /b 0",
    ":rf_full",
    'if "%FAKE_FAIL%"=="full" goto rf_fail',
    'copy /y "%STUB%rf-full.bin" "%~4" >nul',
    "echo Read 16777216 bytes at 0x0 (stub)",
    "exit /b 0",
    ":rf_fail",
    "echo A fatal error occurred: the read stopped part way (stub)",
    "exit /b 2",
    ":rf_none",
    "echo STUB: no data for read-flash %~2 %~3",
    "exit /b 5",
    ":noboard",
    "echo A fatal error occurred: Could not open the port (stub: no board)",
    "exit /b 2",
    ":wf",
    "echo STUB: a write or erase was called, which the harness never does",
    "exit /b 3",
    ":other",
    "echo STUB: unknown command",
    "exit /b 4",
    "",
]
open(os.path.join(OUT, "fake-esptool.cmd"), "wb").write(CRLF.join(fake).encode("ascii"))

D, S, B = "flow-flash-dualboot.cmd", "flow-flash-standalone.cmd", "flow-back-to-linux.cmd"
SHOWBK = ['if exist "%BACKUP_DIR%" dir /b "%BACKUP_DIR%"',
          'if exist "%BACKUP_DIR%\\korvo1-before.txt" type "%BACKUP_DIR%\\korvo1-before.txt"']
cases = [
  # 1 to 39: bat-harness-make-flow-v3.7.py's cases, unchanged
  ("dualboot all, Linux be5b725 in flash, no backup yet, Y: the backup is taken first", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, ['if exist "%~dp0bk-a" rmdir /s /q "%~dp0bk-a"'], SHOWBK),
  ("dualboot all, Linux be5b725, the same backup there already, Y: kept, no new one", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], SHOWBK),
  ("dualboot all, Linux with the release's bootloader, a backup of the be5b725 state there: stops", "Y", D, "COM10", "linux-release", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot all, Linux with the release's bootloader, no backup yet, Y", "Y", D, "COM10", "linux-release", {"BACKUP_DIR": "bk-b"}, ['if exist "%~dp0bk-b" rmdir /s /q "%~dp0bk-b"'], SHOWBK),
  ("dualboot all, Linux with a bootloader nobody knows, no backup yet, Y", "Y", D, "COM10", "linux-otherbl", {"BACKUP_DIR": "bk-c"}, ['if exist "%~dp0bk-c" rmdir /s /q "%~dp0bk-c"'], SHOWBK),
  ("dualboot all, Linux, no backup yet, the 16 MB read fails, Y: nothing written", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-d", "FAKE_FAIL": "full"}, ['if exist "%~dp0bk-d" rmdir /s /q "%~dp0bk-d"'], SHOWBK),
  ("dualboot all, the dual boot in flash already, Y: no backup needed", "Y", D, "COM10", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot app COM7, the dual boot in flash, Y", "Y", D, "app COM7", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot app COM7, the dual boot in flash, answer N", "N", D, "app COM7", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot all, the standalone set (or the DOS board) in flash", "Y", D, "COM10", "own", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot all, a table nobody knows (upstream v0.4.0, say)", "Y", D, "COM10", "other", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot app, Linux in flash (no tiny386 partition yet)", "Y", D, "app COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot with no port", "Y", D, "", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], []),
  ("dualboot, no board on the port", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "FAKE_BOARD": "none"}, [], []),
  ("dualboot, a tiny386.bin that is not there", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "TINY386_BIN": "%~dp0nothing-here.bin"}, [], []),
  ("dualboot, KORVO1_MAC set and matching, Linux, the backup there, Y", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "KORVO1_MAC": "7C:2C:67:00:00:01"}, [], []),
  ("dualboot, KORVO1_MAC set, another board on the port", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "KORVO1_MAC": "7c:2c:67:00:00:01", "FAKE_MAC": "7c:2c:67:00:00:99"}, [], []),
  ("dualboot all, Linux, a backup there from a board with another MAC", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "FAKE_MAC": "7c:2c:67:00:00:99"}, [], []),
  ("dualboot all, Linux, a backup there whose slice is not the bytes recorded", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-t"}, ['if exist "%~dp0bk-t" rmdir /s /q "%~dp0bk-t"', 'xcopy /e /i /q "%~dp0bk-a" "%~dp0bk-t" >nul', '>>"%~dp0bk-t\\korvo1-2000-8fff-before.bin" echo x'], []),
  ("standalone, Linux in flash, no backup yet, Y: the backup is taken first", "Y", S, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-e"}, ['if exist "%~dp0bk-e" rmdir /s /q "%~dp0bk-e"'], SHOWBK),
  ("standalone, its own table in flash already, Y", "Y", S, "COM10", "own", {"BACKUP_DIR": "bk-e"}, [], []),
  ("standalone, a table nobody knows", "Y", S, "COM10", "other", {"BACKUP_DIR": "bk-e"}, [], []),
  ("standalone, SA_TABLE set to the dual-boot table", "Y", S, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-e", "SA_TABLE": V3_WIN + BS + "images" + BS + "korvo1-dualboot" + BS + "partition-table.bin"}, [], []),
  ("standalone, image-info says invalid", "Y", S, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-e", "FAKE_II": "invalid"}, [], []),
  ("standalone with no port", "Y", S, "", "linux-be5b725", {"BACKUP_DIR": "bk-e"}, [], []),
  ("back-to-linux COM10 with a backup, the dual boot in flash, Y: the backup's 0x2000 to 0x8FFF", "Y", B, "COM10", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux COM10 with no backup, the dual boot in flash, Y: small be5b725", "Y", B, "COM10", "dual", {"BACKUP_DIR": "bk-none"}, [], []),
  ("back-to-linux COM10 small, with a backup there, Y: small be5b725 as asked", "Y", B, "COM10 small", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux COM10 whole, the standalone set in flash, Y", "Y", B, "COM10 whole", "own", {"BACKUP_DIR": "bk-e"}, [], []),
  ("back-to-linux COM10 (backup), the standalone set in flash: stops, names whole", "Y", B, "COM10", "own", {"BACKUP_DIR": "bk-e"}, [], []),
  ("back-to-linux COM10 backup, a slice that is not the bytes recorded", "Y", B, "COM10 backup", "dual", {"BACKUP_DIR": "bk-t"}, [], []),
  ("back-to-linux COM10 backup, a board with another MAC", "Y", B, "COM10 backup", "dual", {"BACKUP_DIR": "bk-a", "FAKE_MAC": "7c:2c:67:00:00:99"}, [], []),
  ("back-to-linux COM10 whole with no backup", "Y", B, "COM10 whole", "own", {"BACKUP_DIR": "bk-none"}, [], []),
  ("back-to-linux COM10 full, the standalone set in flash, Y", "Y", B, "COM10 full", "own", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux COM10 release (small release), the dual boot in flash, Y", "Y", B, "COM10 release", "dual", {"BACKUP_DIR": "bk-none"}, [], []),
  ("back-to-linux full release COM10, Linux in flash, y", "y", B, "full release COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux COM10, a table nobody knows", "Y", B, "COM10", "other", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux COM10 with a backup, the dual boot in flash, answer N", "N", B, "COM10", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("back-to-linux with no port", "Y", B, "", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  # 40 on: tiny386 as the default (dual386), flash-dualboot.bat's table mode
  ("NEW dualboot COM10 table tiny386, the dual boot (Linux the default) in flash, Y: the table alone", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, the dual boot in flash, answer N", "N", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table linux, tiny386 the default in flash, Y: the table alone, back to Linux the default", "Y", D, "COM10 table linux", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, tiny386 the default already: nothing to write", "Y", D, "COM10 table tiny386", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table linux, Linux the default already: nothing to write", "Y", D, "COM10 table linux", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table, no default named: usage", "Y", D, "COM10 table", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, Linux's own table in flash: stops, names all tiny386", "Y", D, "COM10 table tiny386", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, the dual-boot table behind be5b725's bootloader (no key): stops", "Y", D, "COM10 table tiny386", "dual-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table linux, the tiny386-default table behind be5b725's bootloader: stops", "Y", D, "COM10 table linux", "dual386-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, the standalone set (or the DOS board) in flash: stops", "Y", D, "COM10 table tiny386", "own", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, a table nobody knows: stops", "Y", D, "COM10 table tiny386", "other", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 all tiny386, Linux be5b725 in flash, no backup yet, Y: backup, tiny386, bootloader and the tiny386-default table", "Y", D, "COM10 all tiny386", "linux-be5b725", {"BACKUP_DIR": "bk-f"}, ['if exist "%~dp0bk-f" rmdir /s /q "%~dp0bk-f"'], SHOWBK),
  ("NEW dualboot COM10 (all, no default named), tiny386 the default in flash, Y: keeps tiny386 the default", "Y", D, "COM10", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 all linux, tiny386 the default in flash, Y: the set again, Linux the default", "Y", D, "COM10 all linux", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 all tiny386, the dual boot (Linux the default) in flash, Y", "Y", D, "COM10 all tiny386", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 app, tiny386 the default in flash, Y: tiny386 alone, the default kept", "Y", D, "COM10 app", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 app tiny386: refused, app does not choose the default", "Y", D, "COM10 app tiny386", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, no board on the port", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a", "FAKE_BOARD": "none"}, [], []),
  ("NEW dualboot COM10 table tiny386, KORVO1_MAC set, another board on the port", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a", "KORVO1_MAC": "7c:2c:67:00:00:01", "FAKE_MAC": "7c:2c:67:00:00:99"}, [], []),
  ("NEW dualboot Tiny386 TABLE com10 (any order, any case), the dual boot in flash, Y", "Y", D, "Tiny386 TABLE com10", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW dualboot COM10 table tiny386, TINY386_BIN not there: table mode does not need it, Y", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a", "TINY386_BIN": "%~dp0nothing-here.bin"}, [], []),
  ("NEW back-to-linux COM10 with a backup, tiny386 the default in flash, Y: the backup's 0x2000 to 0x8FFF", "Y", B, "COM10", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW back-to-linux COM10 small, tiny386 the default in flash, Y", "Y", B, "COM10 small", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW back-to-linux COM10 with no backup, tiny386 the default in flash, Y: small be5b725", "Y", B, "COM10", "dual386", {"BACKUP_DIR": "bk-none"}, [], []),
  ("NEW back-to-linux COM10 full, tiny386 the default in flash, Y", "Y", B, "COM10 full", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW back-to-linux COM10 whole, tiny386 the default in flash, Y", "Y", B, "COM10 whole", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW back-to-linux COM10 small, the tiny386-default table behind be5b725's bootloader (a cut write), Y", "Y", B, "COM10 small", "dual386-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW back-to-linux COM10 backup, tiny386 the default in flash, a board with another MAC", "Y", B, "COM10 backup", "dual386", {"BACKUP_DIR": "bk-a", "FAKE_MAC": "7c:2c:67:00:00:99"}, [], []),
  ("NEW back-to-linux COM10 with a backup, tiny386 the default in flash, answer N", "N", B, "COM10", "dual386", {"BACKUP_DIR": "bk-a"}, [], []),
  ("NEW standalone COM10, tiny386 the default in flash, Y", "Y", S, "COM10", "dual386", {"BACKUP_DIR": "bk-e"}, [], []),
]
run = ["@echo off", 'set "ESPTOOL=%~dp0fake-esptool.cmd"',
       "rem the scripts' own temporary files here, not in the user's TEMP",
       'if not exist "%~dp0tmp" mkdir "%~dp0tmp"', 'set "TEMP=%~dp0tmp"',
       'if exist "%~dp0calls.log" del "%~dp0calls.log"']
keys = ["FAKE_FLASH", "FAKE_BOARD", "FAKE_MAC", "FAKE_FAIL", "FAKE_II", "BACKUP_DIR",
        "TINY386_BIN", "KORVO1_MAC", "SA_TABLE"]
for k, (title, ans, flow, args, flash, extra, setup, after) in enumerate(cases, 1):
    run.append("echo ######## %d %s" % (k, title))
    for key in keys:
        run.append('set "%s="' % key)
    run.append('set "FAKE_FLASH=%s"' % flash)
    for key, val in extra.items():
        if key == "BACKUP_DIR":
            val = "%~dp0" + val
        run.append('set "%s=%s"' % (key, val))
    run.extend(setup)
    run.append("echo " + ans + '| cmd /c "%~dp0' + flow + '" ' + args)
    run.append("echo exit %errorlevel%")
    run.append("echo -- the stub's calls:")
    run.append('if exist "%~dp0calls.log" type "%~dp0calls.log"')
    run.append('if exist "%~dp0calls.log" del "%~dp0calls.log"')
    if after:
        run.append("echo -- the backup folder after:")
        run.extend(after)
for key in keys:
    run.append('set "%s="' % key)
open(os.path.join(OUT, "runflow.cmd"), "wb").write(CRLF.join(run + [""]).encode("ascii"))
print("written to", OUT, len(cases), "cases")
