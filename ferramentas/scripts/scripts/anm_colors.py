# Read-only: decode the embedded ARGB4444 texture of player00.anm and count distinct values.
import struct, sys, collections
path = sys.argv[1]
data = open(path, "rb").read()
textureOffset, = struct.unpack_from("<i", data, 0x30)
t = textureOffset
magic, colorDepth, imageType, fmt, w, h = struct.unpack_from("<6h", data, t)
assert fmt == 5, fmt
px = struct.unpack_from("<%dH" % (w * h), data, t + 0x10)
rgba = collections.Counter()
alpha = collections.Counter()
rgb = set()
for v in px:
    a = (v >> 12) & 15; r = (v >> 8) & 15; g = (v >> 4) & 15; b = v & 15
    rgba[(r, g, b, a)] += 1
    alpha[a] += 1
    rgb.add((r, g, b))
print(f"{w}x{h} pixels={len(px)}")
print(f"distinct RGBA4444 colors: {len(rgba)}")
print(f"distinct RGB (ignoring alpha): {len(rgb)}")
print("alpha levels used (0-15):", sorted(alpha.keys()))
print("fully transparent px:", alpha.get(0, 0), " opaque px (a=15):", alpha.get(15, 0), " partial px:", sum(c for a, c in alpha.items() if 0 < a < 15))
print("top 8 colors (r,g,b,a):count:", rgba.most_common(8))
