# Builds test harnesses from the subroutine sections and the argument loops of
# the v3 .bat files, copied verbatim, with a stub in place of esptool.  The
# .bat files themselves are not run.
import os

V3 = r"D:\Repos\esp32-s31-linux-release\tiny386-idf\v3"
B = os.path.dirname(os.path.abspath(__file__))
TINY = r"D:\Repos\esp32-s31-linux-release\tiny386-idf\v2\images\tiny386.bin"
V2BL = r"D:\Repos\esp32-s31-linux-release\tiny386-idf\v2\images\bootloader\bootloader.bin"
V2PT = r"D:\Repos\esp32-s31-linux-release\tiny386-idf\v2\images\partition_table\partition-table.bin"
BL = V3 + r"\dualboot\bootloader.bin"
PT = V3 + r"\dualboot\partition-table.bin"
MISSING = r"D:\nonexistent\x.bin"


def read(name):
    return open(os.path.join(V3, name), "rb").read().decode("ascii")


def subs(name):
    t = read(name)
    return t[t.index("rem Subroutines"):]


def argblock(name):
    t = read(name)
    if ":args\r\n" not in t:
        return ":argsdone"
    i = t.index(":args\r\n")
    j = t.index(":argsdone\r\n")
    return t[i:j + len(":argsdone\r\n")]


FAKE = r"""@echo off
rem stub for esptool: never touches a port
if "%1"=="version" goto ver
if "%FAKE_II%"=="error" goto err
if "%FAKE_II%"=="invalid" goto inv
type "%~dp0ii-good.txt"
exit /b 0
:inv
type "%~dp0ii-invalid.txt"
exit /b 0
:err
echo A fatal error occurred: This is not a valid image, invalid magic number 0xaa.
exit /b 2
:ver
if "%FAKE_VER%"=="none" exit /b 9009
if not defined FAKE_VER set "FAKE_VER=5.4.0"
echo esptool v%FAKE_VER%
echo %FAKE_VER%
exit /b 0
"""


def harness(fname, src, tests, argtests):
    h = ["@echo off", "setlocal EnableExtensions",
         'set "ESPTOOL=%~dp0fake-esptool.cmd"']
    for title, cmd, pre in tests:
        h.append("echo == " + title)
        h.extend(pre)
        h.append(cmd + " && echo RESULT ok || echo RESULT fail")
    for a in argtests:
        h.append("echo == args: [" + a + "]")
        h.append("call :argtest " + a)
    h.append("exit /b 0")
    h += [":argtest", 'set "PORT=COM10"', 'set "MODE=all"', 'set "SCOPE=full"',
          'set "LSET=release"']
    h.append(argblock(src).rstrip("\r\n"))
    h.append("echo PORT=[%PORT%] MODE=[%MODE%] SCOPE=[%SCOPE%] LSET=[%LSET%]")
    h.append("exit /b 0")
    body = "\r\n".join(h) + "\r\n" + subs(src)
    open(os.path.join(B, fname), "wb").write(body.encode("ascii"))


open(os.path.join(B, "fake-esptool.cmd"), "wb").write(FAKE.replace("\n", "\r\n").encode())

harness("t-dualboot.cmd", "flash-dualboot.bat", [
    ("esptool 5.4.0", "call :need_esptool", ['set "FAKE_VER=5.4.0"']),
    ("esptool 6.0.1", "call :need_esptool", ['set "FAKE_VER=6.0.1"']),
    ("esptool 5.10.0", "call :need_esptool", ['set "FAKE_VER=5.10.0"']),
    ("esptool 5.3.2, too old", "call :need_esptool", ['set "FAKE_VER=5.3.2"']),
    ("esptool 4.8.1, too old", "call :need_esptool", ['set "FAKE_VER=4.8.1"']),
    ("esptool missing", "call :need_esptool", ['set "FAKE_VER=none"']),
    ("hash, dual-boot bootloader",
     'call :check_hash "%s" b57469cd1542fe83a4f468f93a7e8e9bfa5af138f15a38e6867a0a54fccff29f' % BL, []),
    ("hash, dual-boot table",
     'call :check_hash "%s" d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346' % PT, []),
    ("hash, wrong file",
     'call :check_hash "%s" b57469cd1542fe83a4f468f93a7e8e9bfa5af138f15a38e6867a0a54fccff29f' % PT, []),
    ("hash, missing file", 'call :check_hash "%s" 00' % MISSING, []),
    ("image, good", 'call :check_image "%s" 1966080' % TINY, ['set "FAKE_II=good"']),
    ("image, invalid hash", 'call :check_image "%s" 1966080' % TINY, ['set "FAKE_II=invalid"']),
    ("image, not an image", 'call :check_image "%s" 1966080' % TINY, ['set "FAKE_II=error"']),
    ("image, too big", 'call :check_image "%s" 1000000' % TINY, ['set "FAKE_II=good"']),
    ("image, missing", 'call :check_image "%s" 1966080' % MISSING, ['set "FAKE_II=good"']),
], ["", "COM7", "app", "COM7 app", "app COM3", "ALL com4"])

harness("t-standalone.cmd", "flash-standalone.bat", [
    ("bootloader, v2", 'call :check_bootloader "%s"' % V2BL, ['set "FAKE_II=good"']),
    ("bootloader, too big", 'call :check_bootloader "%s"' % TINY, ['set "FAKE_II=good"']),
    ("bootloader, invalid", 'call :check_bootloader "%s"' % V2BL, ['set "FAKE_II=invalid"']),
    ("table, v2", 'call :check_table "%s"' % V2PT, []),
    ("table, wrong size", 'call :check_table "%s"' % BL, []),
    ("image, good, standalone size", 'call :check_image "%s" 1835008' % TINY, ['set "FAKE_II=good"']),
], [])

harness("t-back.cmd", "back-to-linux.bat", [
    ("release bootloader",
     'call :check_hash "%s\\dualboot\\linux-v2026.09.20\\bootloader\\bootloader\\bootloader.bin" '
     '41d5a68752936c7b65dadb3081e4ebefc649af76cc1af450bda508e3b3455d1b' % V3, []),
    ("release Image",
     'call :check_hash "%s\\dualboot\\linux-v2026.09.20\\Image" '
     'd04e9f631b9cfeabd9ec4be21196c5edc181bd4bb857591557189832d3b64f52' % V3, []),
    ("blank",
     'call :check_hash "%s\\dualboot\\blank-0x9000-0x1ffff.bin" '
     '667072b19b8057c9dbb4f99047f9ff683cca37b8b3893bb900cf150ae049be04' % V3, []),
    ("be5b725 flash.bin by the relative path",
     'call :check_hash "%s\\..\\..\\main-be5b725\\esp32s31-linux-main-be5b725-flash\\flash.bin" '
     'f48b83f750dc96f635bf222afa6bbd60114d4d14bbbc7533bebff864ae5fb1a1' % V3, []),
], ["", "small", "COM5 small be5b725", "be5b725", "full release COM9"])
print("harnesses written")
