import importlib.util
import os
import subprocess
import sys
import time

S = "/tmp/claude-0/-home-claude/5179d6f6-deee-580a-9fa0-a95ac7dec80d/scratchpad"
V = os.path.join(S, "verify_lzss")
DAT = os.path.join(S, "pbg4", "th07_orig.dat")

spec = importlib.util.spec_from_file_location("reducer", os.path.join(S, "tools", "th07_dat_reducer.py"))
reducer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reducer)

entries = reducer.read_archive(DAT)
print("entries:", len(entries))

# Dump each compressed stream and its expected decompressed size.
manifest = []
for i, e in enumerate(entries):
    cpath = os.path.join(V, "comp", f"{i}.bin")
    with open(cpath, "wb") as f:
        f.write(e["comp"])
    manifest.append((cpath, e["dsize"], e["name"]))

def run(harness, outdir):
    t0 = time.time()
    bad = []
    for i, (cpath, dsize, name) in enumerate(manifest):
        out = os.path.join(outdir, f"{i}.out")
        r = subprocess.run([harness, cpath, str(dsize), out], capture_output=True)
        if r.returncode != 0:
            bad.append((name, "exit %d %s" % (r.returncode, r.stderr.decode(errors="replace").strip())))
    return time.time() - t0, bad

t_old, bad_old = run(os.path.join(V, "h_old"), os.path.join(V, "out_old"))
t_new, bad_new = run(os.path.join(V, "h_new"), os.path.join(V, "out_new"))
print("old harness: %.2f s, failures %d" % (t_old, len(bad_old)))
print("new harness: %.2f s, failures %d" % (t_new, len(bad_new)))

mismatch = 0
for i, (cpath, dsize, name) in enumerate(manifest):
    a = open(os.path.join(V, "out_old", f"{i}.out"), "rb").read()
    b = open(os.path.join(V, "out_new", f"{i}.out"), "rb").read()
    if a != b or len(a) != dsize:
        mismatch += 1
        if mismatch <= 5:
            print("MISMATCH", name, len(a), len(b), dsize)
print("byte-identical entries: %d / %d, mismatches: %d" % (len(manifest) - mismatch, len(manifest), mismatch))
