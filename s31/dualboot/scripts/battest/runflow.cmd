@echo off
set "ESPTOOL=%~dp0fake-esptool.cmd"
set "TINY386_BIN=D:\Repos\esp32-s31-linux-release\tiny386-idf\v2\images\tiny386.bin"
set "FAKE_II=good"
echo ######## dualboot all, answer Y
echo Y| cmd /c "%~dp0flow-flash-dualboot.cmd"
echo exit %errorlevel%
echo ######## dualboot app COM7, answer N
echo N| cmd /c "%~dp0flow-flash-dualboot.cmd" app COM7
echo exit %errorlevel%
echo ######## standalone, answer Y
echo Y| cmd /c "%~dp0flow-flash-standalone.cmd"
echo exit %errorlevel%
echo ######## back-to-linux default, answer Y
echo Y| cmd /c "%~dp0flow-back-to-linux.cmd"
echo exit %errorlevel%
echo ######## back-to-linux small be5b725, answer y
echo y| cmd /c "%~dp0flow-back-to-linux.cmd" small be5b725
echo exit %errorlevel%
echo ######## back-to-linux full be5b725 COM4, answer Y
echo Y| cmd /c "%~dp0flow-back-to-linux.cmd" be5b725 COM4
echo exit %errorlevel%
echo ######## back-to-linux release small, empty answer
echo.| cmd /c "%~dp0flow-back-to-linux.cmd" small
echo exit %errorlevel%
echo ######## dualboot with TINY386_BIN unset (default path v3\images)
set "TINY386_BIN="
echo Y| cmd /c "%~dp0flow-flash-dualboot.cmd"
echo exit %errorlevel%
