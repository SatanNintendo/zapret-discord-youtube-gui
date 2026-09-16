@echo off
rem Zapret GUI - build script (Windows, MinGW-w64 g++)
rem Requires: x86_64-w64-mingw32-g++ and windres in PATH
rem (MSYS2: pacman -S mingw-w64-x86_64-gcc  |  or use winlibs.com toolchain)

cd /d "%~dp0"
if not exist build mkdir build

echo [1/3] compiling resources...
windres res\app.rc -O coff -o build\app_res.o
if errorlevel 1 goto :err

echo [2/3] compiling sources...
g++ -municode -mwindows -O2 -Wall -Wextra -std=c++17 ^
    src\main.cpp ^
    src\controls.cpp ^
    src\uidraw.cpp ^
    src\theme.cpp ^
    src\lang.cpp ^
    src\zapret.cpp ^
    src\netops.cpp ^
    src\applog.cpp ^
    build\app_res.o ^
    -o build\ZapretGUI.exe ^
    -static -s ^
    -lgdiplus -lwinhttp -lshell32 -lgdi32 -luser32 -lcomctl32 -ladvapi32 -lole32
if errorlevel 1 goto :err

echo [3/3] done: build\ZapretGUI.exe
exit /b 0

:err
echo BUILD FAILED
exit /b 1
