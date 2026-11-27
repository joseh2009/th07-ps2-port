#!/bin/bash
export PS2DEV=/opt/ps2dev-x/ps2dev PS2SDK=/opt/ps2dev-x/ps2dev/ps2sdk GSKIT=/opt/ps2dev-x/ps2dev/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2SDK/bin
OUT=/tmp/claude-0/-home-claude/5179d6f6-deee-580a-9fa0-a95ac7dec80d/scratchpad/p7
cd /home/claude/some100/th07
f=$1; n=$(echo $f | tr '/' '_')
mips64r5900el-ps2-elf-g++ -std=gnu++17 -O1 -G0 -D_EE -D__PS2__ -DNDEBUG -fsyntax-only -Wno-psabi -I src -I src/pbg4 -I $PS2SDK/ee/include -I $PS2SDK/common/include -I $PS2SDK/ports/include -I $PS2SDK/ports/include/SDL2 -I $GSKIT/include $f > $OUT/$n.log 2>&1 && echo "OK   $f" || echo "FAIL $f ($(grep -c 'error' $OUT/$n.log) errors)"
