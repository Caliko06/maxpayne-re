#!/bin/sh
# Pod setup for REA (Ghidra 12 at /root/ghidra, Java 21). Run once per fresh pod: sh tools/rea/setup.sh
set -e
cd "$(dirname "$0")"
[ -d node_modules/rea-agents ] || npm i --silent
mkdir -p /root/rea-tmp
