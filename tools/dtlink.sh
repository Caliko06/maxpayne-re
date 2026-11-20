#!/bin/sh
# Pod: link build/maxpayne_dt (host/difftrace.c + Unicorn) from the current build/gen chunks. Lift first with
# RN_LIFT_PROBES=<fn> RN_LIFT_TRACE=<image of fn>,<image of its caller> build.py lift; relift clean (build.py native) after.
set -e
cd /root/maxpayne-re
G=build/gen
for s in $G/*.c; do o=build/obj/$(basename $s .c).o; [ "$o" -nt "$s" ] && [ "$o" -nt host/rt.h ] || echo $s; done | xargs -r -P 48 -I{} sh -c "clang -O1 -w -Ihost -I$G -fno-strict-aliasing -c {} -o build/obj/\$(basename {} .c).o"
HOST=$(.venv/bin/python -c "import sys; sys.path.insert(0,\"tools/recomp\"); import build; print(\" \".join(build.HOST))")
clang -O1 -g -w -DRN_DIFFTRACE -Ihost -I$G $HOST host/difftrace.c $(ls $G | grep -E "_[0-9]{3}\.c$" | sed "s|^|build/obj/|;s|\.c$|.o|") build/obj/table.o build/obj/image_tables.o -lm -lpthread -lunicorn -o build/maxpayne_dt
echo linked
