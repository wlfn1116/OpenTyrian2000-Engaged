#!/bin/bash
# Build the Switch NRO with devkitPro bash. Pass `clean` to clean.
# Set PATH explicitly because non-login MSYS2 shells start nearly empty.

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH="/usr/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

cd "$(dirname "$0")" || exit 1

make "$@" 2>&1 | tee build.log
exit ${PIPESTATUS[0]}
