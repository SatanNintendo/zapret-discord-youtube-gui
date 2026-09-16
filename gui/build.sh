#!/usr/bin/env bash
# Zapret GUI — cross-build script (Linux -> Windows x64)
# Requires: mingw-w64 (g++ / windres targeting x86_64-w64-mingw32)
set -e

cd "$(dirname "$0")"

OUT=build
mkdir -p "$OUT"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"

echo "[1/3] compiling resources..."
"$WINDRES" res/app.rc -O coff -o "$OUT/app_res.o"

echo "[2/3] compiling sources..."
"$CXX" -municode -mwindows -O2 -Wall -Wextra -std=c++17 \
    src/main.cpp \
    src/controls.cpp \
    src/uidraw.cpp \
    src/theme.cpp \
    src/lang.cpp \
    src/zapret.cpp \
    src/netops.cpp \
    src/applog.cpp \
    "$OUT/app_res.o" \
    -o "$OUT/ZapretGUI.exe" \
    -static -s \
    -lgdiplus -lwinhttp -lshell32 -lgdi32 -luser32 -lcomctl32 -ladvapi32 -lole32

echo "[3/3] done: $OUT/ZapretGUI.exe"
