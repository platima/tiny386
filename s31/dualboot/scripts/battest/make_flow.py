# Builds main-flow harnesses from the v3 .bat files: everything up to and
# including the Y/N check, verbatim, then "echo WOULD WRITE" in place of the
# esptool write, then the scripts' own labels and subroutines.  The write
# command is cut out, so nothing can reach a port even without the stub.
import os

V3 = r"D:\Repos\esp32-s31-linux-release\tiny386-idf\v3"
B = os.path.dirname(os.path.abspath(__file__))
for name in ["flash-dualboot.bat", "flash-standalone.bat", "back-to-linux.bat"]:
    t = open(os.path.join(V3, name), "rb").read().decode("ascii")
    stop = 'if /i not "%ANS%"=="Y" goto cancelled\r\n'
    i = t.index(stop) + len(stop)
    j = t.index(":cancelled\r\n")
    cut = t[i:j]
    assert "write-flash" in cut and cut.count("call %ESPTOOL%") == 1, name
    head = t[:i].replace('set "HERE=%~dp0"', 'set "HERE=' + V3 + '\\"')
    body = (head + "echo WOULD WRITE: %ESPTOOL% -c esp32s31 -p %PORT% -b 921600 -a hard-reset "
            "write-flash -fm keep -ff keep -fs keep %WRITES%\r\nexit /b 0\r\n" + t[j:])
    assert body.count("call %ESPTOOL% -c esp32s31 -p") == 0
    open(os.path.join(B, "flow-" + name.replace(".bat", ".cmd")), "wb").write(body.encode("ascii"))
print("flow harnesses written")
