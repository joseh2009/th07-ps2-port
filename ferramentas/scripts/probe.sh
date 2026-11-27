#!/bin/bash
export PS2DEV=/opt/ps2dev-x/ps2dev PS2SDK=/opt/ps2dev-x/ps2dev/ps2sdk GSKIT=/opt/ps2dev-x/ps2dev/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2SDK/bin
OUT=/tmp/claude-0/-home-claude/5179d6f6-deee-580a-9fa0-a95ac7dec80d/scratchpad/probe
cd /home/claude/gensokyoclub/th06
CXX=mips64r5900el-ps2-elf-g++
FLAGS="-std=gnu++20 -O1 -G0 -D_EE -D__PS2__ -DNDEBUG -fsyntax-only -Wno-psabi -I src -I $PS2SDK/ee/include -I $PS2SDK/common/include -I $PS2SDK/ports/include -I $PS2SDK/ports/include/SDL2 -I $GSKIT/include"
for f in "$@"; do
  n=$(echo $f | tr '/' '_')
  $CXX $FLAGS $f > $OUT/$n.log 2>&1 && echo "OK   $f" || echo "FAIL $f ($(grep -c 'error' $OUT/$n.log) errors)"
done
