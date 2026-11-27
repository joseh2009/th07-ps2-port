# Converts the embedded texture of a single-entry TH07 ANM from ARGB4444 (format 5) to ARGB1555 (format 2).
# Same byte size, same offsets. Alpha is thresholded to 1 bit; RGB is scaled 4->5 bits.
import struct, sys, shutil
src, dst, thr = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = bytearray(open(src, "rb").read())
textureOffset, = struct.unpack_from("<i", data, 0x30)
magic, colorDepth, imageType, fmt, w, h = struct.unpack_from("<6h", data, textureOffset)
assert fmt == 5 and struct.unpack_from("<i", data, 0x14)[0] == 5, "unexpected source format"
n = w * h
base = textureOffset + 0x10
assert base + 2 * n == len(data), "payload is not the file tail; layout assumption broken"
soft = 0; opaque = 0; cleared = 0; kept_partial = 0
for i in range(n):
    off = base + 2 * i
    v, = struct.unpack_from("<H", data, off)
    a4 = (v >> 12) & 15; r4 = (v >> 8) & 15; g4 = (v >> 4) & 15; b4 = v & 15
    r5 = round(r4 * 31 / 15); g5 = round(g4 * 31 / 15); b5 = round(b4 * 31 / 15)
    a1 = 1 if a4 >= thr else 0
    if 0 < a4 < 15:
        soft += 1
        if a1: kept_partial += 1
        else: cleared += 1
    if a4 == 15: opaque += 1
    out = (a1 << 15) | (r5 << 10) | (g5 << 5) | b5
    struct.pack_into("<H", data, off, out)
struct.pack_into("<i", data, 0x14, 2)          # AnmRawEntry.format
struct.pack_into("<h", data, textureOffset + 6, 2)  # ZunImageInfoEmbedded.format
open(dst, "wb").write(bytes(data))
print(f"threshold a4>={thr}: partial-alpha px={soft} (kept opaque={kept_partial}, made transparent={cleared}); opaque px={opaque}")
print("bytes written:", len(data), "source:", len(open(src,'rb').read()))
