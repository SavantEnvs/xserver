#!/usr/bin/env python3
"""Print compile / link flags of upstream's own unit-test executable (meson-generated), so harnesses
are compiled and linked exactly like it, without hardcoding the server's include dirs and static libs."""
import json, shlex, subprocess, sys

build = sys.argv[2]
mode = sys.argv[1]

if mode == "compile":
    for e in json.load(open(f"{build}/compile_commands.json")):
        if e["file"].endswith("test/misc.c"):
            toks = shlex.split(e["command"])[1:]
            out, i = [], 0
            while i < len(toks):
                t = toks[i]
                if t in ("-MD", "-DXORG_TESTS", "-DRES_TESTS", "-DLDWRAP_TESTS"):
                    i += 1
                elif t in ("-MQ", "-MF", "-o", "-c"):
                    i += 2
                else:
                    out.append(t)
                    i += 1
            print(shlex.join(out))
            break
    else:
        sys.exit("no test/misc.c compile command")
elif mode == "link":
    cmds = subprocess.check_output(["ninja", "-C", build, "-t", "commands", "test/tests"], text=True).splitlines()
    toks = shlex.split(cmds[-1])
    start = toks.index("-Wl,--as-needed")
    print(shlex.join(t for t in toks[start:] if not t.startswith("-Wl,-wrap,")))
else:
    sys.exit("usage: mesonflags.py compile|link <builddir>")
