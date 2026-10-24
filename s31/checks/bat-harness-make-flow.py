# Main-flow harnesses from the v3 .bat files, as the integration step and
# the dual-boot work made them: everything up to and including the Y/N
# check, verbatim (so the file checks and the board read run, against a stub
# esptool), then "echo WOULD WRITE" in place of the writes, then the
# scripts' own labels and subroutines.  The write calls are cut out, so
# nothing can reach a port even without the stub.
#
#   python3 make_flow.py OUTDIR      (in WSL)
import os, sys, shutil

BS = chr(92)
V3_WIN = "D:" + BS + "Repos" + BS + "esp32-s31-linux-release" + BS + "tiny386-idf" + BS + "v3"
V3 = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/"
BE = "/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash/"
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
CRLF = chr(13) + chr(10)

for name in ["flash-dualboot.bat", "flash-standalone.bat", "back-to-linux.bat"]:
    t = open(V3 + name, "rb").read().decode("ascii")
    stop = 'if /i not "%ANS%"=="Y" goto cancelled' + CRLF
    i = t.index(stop) + len(stop)
    j = t.index(":cancelled" + CRLF)
    head, cut, tail = t[:i], t[i:j], t[j:]
    assert "call %CMD1%" in cut, name
    assert "call %CMD" not in head, name
    for part in (head, tail):
        for line in part.split(CRLF):
            if line.strip().startswith("call %ESPTOOL%"):
                assert "write-flash" not in line and "erase" not in line, (name, line)
    head2 = head.replace('set "HERE=%~dp0"', 'set "HERE=' + V3_WIN + BS + '"')
    assert head2 != head, name
    body = (head2 + "echo WOULD WRITE: %CMD1%" + CRLF +
            'if /i "%MODE%"=="all" echo WOULD WRITE: %CMD2%' + CRLF +
            "exit /b 0" + CRLF + tail)
    open(os.path.join(OUT, "flow-" + name.replace(".bat", ".cmd")), "wb").write(body.encode("ascii"))

# the partition tables a board can show at 0x8000
shutil.copy(BE + "partition-table.bin", os.path.join(OUT, "table-linux.bin"))
shutil.copy(V3 + "images/korvo1-dualboot/partition-table.bin", os.path.join(OUT, "table-dual.bin"))
shutil.copy(V3 + "images/korvo1-standalone/partition-table.bin", os.path.join(OUT, "table-own.bin"))
b = bytearray(open(BE + "partition-table.bin", "rb").read())
b[0x40] ^= 0x01                         # another Linux's table, say
open(os.path.join(OUT, "table-other.bin"), "wb").write(bytes(b))
# be5b725's flash.bin holds its table at 0x8000: the same bytes as its file
fb = open(BE + "flash.bin", "rb").read()
print("be5b725 flash.bin at 0x8000 to 0x8BFF equals its partition-table.bin:",
      fb[0x8000:0x8C00] == open(BE + "partition-table.bin", "rb").read())

fake = [
    "@echo off",
    "rem stub for esptool: never touches a port.  FAKE_TABLE names the table the",
    "rem board holds (linux, dual, own, other); FAKE_BOARD=none: no board.",
    "rem (shift moves %0 too: this folder is kept first)",
    'set "STUB=%~dp0"',
    ":scan",
    'if "%~1"=="" goto other',
    'if "%~1"=="version" goto ver',
    'if "%~1"=="image-info" goto ii',
    'if "%~1"=="read-mac" goto mac',
    'if "%~1"=="read-flash" goto rf',
    'if "%~1"=="write-flash" goto wf',
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
    "echo Chip type:          ESP32-S31 (stub)",
    "echo MAC:                7c:2c:67:00:00:01",
    "exit /b 0",
    ":rf",
    'if "%FAKE_BOARD%"=="none" goto noboard',
    'copy /y "%STUB%table-%FAKE_TABLE%.bin" "%~4" >nul',
    "echo Read 3072 bytes from 0x8000 (stub)",
    "exit /b 0",
    ":noboard",
    "echo A fatal error occurred: Could not open the port (stub: no board)",
    "exit /b 2",
    ":wf",
    "echo STUB: write-flash was called, which the harness never does",
    "exit /b 3",
    ":other",
    "echo STUB: unknown command",
    "exit /b 4",
    "",
]
open(os.path.join(OUT, "fake-esptool.cmd"), "wb").write(CRLF.join(fake).encode("ascii"))

cases = [
  ("dualboot all, Linux in flash, Y", "Y", "flow-flash-dualboot.cmd", "COM10", "linux", "", ""),
  ("dualboot all, dual boot in flash already, Y", "Y", "flow-flash-dualboot.cmd", "COM10", "dual", "", ""),
  ("dualboot all, the standalone set (or the DOS board) in flash", "Y", "flow-flash-dualboot.cmd", "COM10", "own", "", ""),
  ("dualboot all, a table nobody knows (upstream v0.4.0, say)", "Y", "flow-flash-dualboot.cmd", "COM10", "other", "", ""),
  ("dualboot app, Linux in flash (no tiny386 partition yet)", "Y", "flow-flash-dualboot.cmd", "app COM10", "linux", "", ""),
  ("dualboot app COM7, dual boot in flash, answer N", "N", "flow-flash-dualboot.cmd", "app COM7", "dual", "", ""),
  ("dualboot app COM7, dual boot in flash, Y", "Y", "flow-flash-dualboot.cmd", "app COM7", "dual", "", ""),
  ("dualboot with no port", "Y", "flow-flash-dualboot.cmd", "", "linux", "", ""),
  ("dualboot, no board on the port", "Y", "flow-flash-dualboot.cmd", "COM10", "linux", "none", ""),
  ("dualboot, a tiny386.bin that is not there", "Y", "flow-flash-dualboot.cmd", "COM10", "linux", "", "TINY386_BIN"),
  ("standalone, Linux in flash, Y", "Y", "flow-flash-standalone.cmd", "COM10", "linux", "", ""),
  ("standalone, its own table in flash already, Y", "Y", "flow-flash-standalone.cmd", "COM10", "own", "", ""),
  ("standalone, a table nobody knows", "Y", "flow-flash-standalone.cmd", "COM10", "other", "", ""),
  ("standalone, SA_TABLE set to the dual-boot table", "Y", "flow-flash-standalone.cmd", "COM10", "linux", "", "SA_TABLE"),
  ("standalone, image-info says invalid", "Y", "flow-flash-standalone.cmd", "COM10", "linux", "", "INVALID"),
  ("standalone with no port", "Y", "flow-flash-standalone.cmd", "", "linux", "", ""),
  ("back-to-linux COM10 (small be5b725), dual boot in flash, Y", "Y", "flow-back-to-linux.cmd", "COM10", "dual", "", ""),
  ("back-to-linux COM10 (small), the standalone set in flash", "Y", "flow-back-to-linux.cmd", "COM10", "own", "", ""),
  ("back-to-linux COM10 full, the standalone set in flash, Y", "Y", "flow-back-to-linux.cmd", "COM10 full", "own", "", ""),
  ("back-to-linux COM10 release (small release), dual boot in flash, Y", "Y", "flow-back-to-linux.cmd", "COM10 release", "dual", "", ""),
  ("back-to-linux full release COM10, Linux in flash, y", "y", "flow-back-to-linux.cmd", "full release COM10", "linux", "", ""),
  ("back-to-linux COM10, a table nobody knows", "Y", "flow-back-to-linux.cmd", "COM10", "other", "", ""),
  ("back-to-linux with no port", "Y", "flow-back-to-linux.cmd", "", "dual", "", ""),
]
run = ["@echo off", 'set "ESPTOOL=%~dp0fake-esptool.cmd"',
       "rem the scripts' own temporary files here, not in the user's TEMP",
       'if not exist "%~dp0tmp" mkdir "%~dp0tmp"', 'set "TEMP=%~dp0tmp"']
for k, (title, ans, flow, args, table, board, special) in enumerate(cases, 1):
    run.append("echo ######## %d %s" % (k, title))
    run.append('set "FAKE_TABLE=%s"' % table)
    run.append('set "FAKE_BOARD=%s"' % board)
    run.append('set "FAKE_II=good"')
    if special == "TINY386_BIN":
        run.append('set "TINY386_BIN=%~dp0nothing-here.bin"')
    if special == "SA_TABLE":
        run.append('set "SA_TABLE=%~dp0table-dual.bin"')
    if special == "INVALID":
        run.append('set "FAKE_II=invalid"')
    run.append("echo " + ans + '| cmd /c "%~dp0' + flow + '" ' + args)
    run.append("echo exit %errorlevel%")
    if special == "TINY386_BIN":
        run.append('set "TINY386_BIN="')
    if special == "SA_TABLE":
        run.append('set "SA_TABLE="')
open(os.path.join(OUT, "runflow.cmd"), "wb").write(CRLF.join(run + [""]).encode("ascii"))
print("written to", OUT, len(cases), "cases")
