#!/bin/bash
# Build the Vita VPK at vita/build/OpenTyrian2000.vpk.
#
#   bash vita/build.sh          # build
#   bash vita/build.sh clean    # remove the build dir
#
# The native Windows toolchain requires Windows paths, so this MSYS wrapper
# delegates to build.ps1. See vita/README.md for prerequisites.
set -e
cd "$(dirname "$0")"

PS_ARGS=""
[ "$1" = "clean" ] && PS_ARGS="-Clean"

# Absolute Windows path to build.ps1 so PowerShell finds it regardless of how bash was launched.
PS1_WIN=$(cygpath -w "$PWD/build.ps1" 2>/dev/null || echo "build.ps1")

exec powershell.exe -ExecutionPolicy Bypass -File "$PS1_WIN" $PS_ARGS
