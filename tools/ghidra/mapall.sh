#!/bin/sh
# Ghidra headless on the Mac: analyze the 6 game images and write rea/map/<image>.tsv + rea/c/<image>/ (ExportMap.java).
# Three lanes: the exe alone (1.3 M instructions), and the five DLLs in two lanes. Logs in rea/out/ghidra-<image>.log.
cd "$(dirname "$0")/../.."
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home; G=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless; P="$PWD/build/ghidra"; mkdir -p "$P" rea/out
one() { b=$(basename "$1"); GHIDRA_MAXMEM=4G "$G" "$P" "$b" -import "$1" -overwrite -scriptPath "$PWD/tools/ghidra" \
  -postScript ExportMap.java "$PWD" -log "$PWD/rea/out/ghidra-$b.log" > "rea/out/ghidra-$b.out" 2>&1; echo "done $b"; }
one build/img/MaxPayne.exe &
( one build/img/e2mfc.dll; one build/img/grphmfc.dll; one build/img/sndmfc.dll ) &
( one build/img/rlmfc.dll; one build/img/e2_d3d8_driver_mfc.dll ) &
wait; echo ALLDONE
