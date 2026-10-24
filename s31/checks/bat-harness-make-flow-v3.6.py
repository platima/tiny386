# Flow harnesses for the v3 .bat files (v3.6, as for v3.5, v3.4, v3.3 and the second review round): each script
# copied whole, with HERE pointed at the v3 folder and only its write calls
# ("call %CMD1%", "call %CMD2%", the only lines that run esptool's
# write-flash) replaced by "echo WOULD WRITE".  So the file checks, the
# board reads, the backup and its checks all run, against a stub esptool
# that never touches a port and logs every call it gets.
#
#   python3 make_flow6.py OUTDIR      (in WSL; OUTDIR on the Windows side)
import os, sys, shutil, hashlib

BS = chr(92)
CRLF = chr(13) + chr(10)
V3_WIN = "D:" + BS + "Repos" + BS + "esp32-s31-linux-release" + BS + "tiny386-idf" + BS + "v3"
V3 = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/"
BE = "/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash/"
REL = V3 + "dualboot/linux-v2026.09.20/bootloader/"
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

for name in ["flash-dualboot.bat", "flash-standalone.bat", "back-to-linux.bat"]:
    t = open(V3 + name, "rb").read().decode("ascii")
    lines = t.split(CRLF)
    out = []
    writes = 0
    for line in lines:
        s = line.strip()
        if s.startswith("call %ESPTOOL%"):
            assert "write-flash" not in s and "erase" not in s, (name, line)
        if s in ("call %CMD1%", "call %CMD2%"):
            out.append(line.replace("call %CMD", "echo WOULD WRITE: %CMD").replace("%CMD1%", "%CMD1%"))
            writes += 1
            continue
        if s.startswith('set "CMD') or s.startswith("set WRITES"):
            pass
        out.append(line)
    assert writes >= 1, name
    body = CRLF.join(out)
    body2 = body.replace('set "HERE=%~dp0"', 'set "HERE=' + V3_WIN + BS + '"')
    assert body2 != body, name
    # the WOULD WRITE lines must print the command, not run it
    for line in body2.split(CRLF):
        assert not line.strip().startswith("call %CMD"), (name, line)
    open(os.path.join(OUT, "flow-" + name.replace(".bat", ".cmd")), "wb").write(body2.encode("ascii"))
    print(name, ":", writes, "write calls cut out")

# what the stub's read-flash hands back, for each pretend flash
def pad(b, n):
    assert len(b) <= n
    return b + b"\xff" * (n - len(b))

bl_be = open(BE + "bootloader.bin", "rb").read()
bl_rel = open(REL + "bootloader/bootloader.bin", "rb").read()
bl_dual = open(V3 + "images/korvo1-dualboot/bootloader.bin", "rb").read()
bl_own = open(V3 + "images/korvo1-standalone/bootloader.bin", "rb").read()
pt_linux = open(BE + "partition-table.bin", "rb").read()
pt_dual = open(V3 + "images/korvo1-dualboot/partition-table.bin", "rb").read()
pt_own = open(V3 + "images/korvo1-standalone/partition-table.bin", "rb").read()
bl_odd = bytearray(bl_be); bl_odd[100] ^= 1; bl_odd = bytes(bl_odd)
pt_other = bytearray(pt_linux); pt_other[0x40] ^= 1; pt_other = bytes(pt_other)
assert hashlib.sha256(bl_be).hexdigest().startswith("547c6a5b")
assert hashlib.sha256(bl_rel).hexdigest().startswith("41d5a687")
assert hashlib.sha256(pt_linux).hexdigest().startswith("520538d3")
fb = open(BE + "flash.bin", "rb").read()
assert fb[0x2000:0x2000 + len(bl_be)] == bl_be and fb[0x8000:0x8C00] == pt_linux[:0xC00]

states = {
    "linux-be5b725": (bl_be, pt_linux),
    "linux-release": (bl_rel, pt_linux),
    "linux-otherbl": (bl_odd, pt_linux),
    "dual": (bl_dual, pt_dual),
    "own": (bl_own, pt_own),
    "other": (bl_be, pt_other),
}
for st, (bl, pt) in states.items():
    region = pad(bl, 0x6000) + pad(pt, 0x1000)          # 0x2000 to 0x8FFF
    assert len(region) == 0x7000
    def put(addr, size, data):
        open(os.path.join(OUT, "rf-%s-%s-%s.bin" % (st, addr, size)), "wb").write(data)
    put("0x8000", "0xC00", region[0x6000:0x6C00])
    put("0x2000", "22000", region[:22000])
    put("0x2000", "23024", region[:23024])
    put("0x2000", "0x7000", region)
# the whole flash: 16 MB, be5b725's flash.bin then 0xFF (its content does
# not matter to the scripts, only that it is whole and recorded)
open(os.path.join(OUT, "rf-full.bin"), "wb").write(pad(fb, 16 << 20))

# image-info as esptool prints it for the v3 images (made in WSL beforehand)
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "ii-good.txt"), os.path.join(OUT, "ii-good.txt"))
ii = open(os.path.join(OUT, "ii-good.txt"), "rb").read().replace(b"(valid)", b"(invalid)")
open(os.path.join(OUT, "ii-invalid.txt"), "wb").write(ii)

fake = [
    "@echo off",
    "rem stub for esptool: never touches a port.  FAKE_FLASH names the flash the",
    "rem board holds (linux-be5b725, linux-release, linux-otherbl, dual, own,",
    "rem other); FAKE_BOARD=none: no board; FAKE_MAC: its MAC; FAKE_FAIL=full:",
    "rem the 16 MB read fails.  Every call is logged in calls.log.",
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

# (title, answer, flow, args, flash, extra settings, setup commands, after commands)
D, S, B = "flow-flash-dualboot.cmd", "flow-flash-standalone.cmd", "flow-back-to-linux.cmd"
SHOWBK = ['if exist "%BACKUP_DIR%" dir /b "%BACKUP_DIR%"',
          'if exist "%BACKUP_DIR%\\korvo1-before.txt" type "%BACKUP_DIR%\\korvo1-before.txt"']
cases = [
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
