@echo off
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
