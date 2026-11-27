import struct, sys
from PIL import Image
def decode(path):
    data = open(path, "rb").read()
    t, = struct.unpack_from("<i", data, 0x30)
    _, _, _, fmt, w, h = struct.unpack_from("<6h", data, t)
    px = struct.unpack_from("<%dH" % (w*h), data, t+0x10)
    img = Image.new("RGBA", (w, h))
    out = []
    for v in px:
        if fmt == 5:
            a = (v>>12)&15; r=(v>>8)&15; g=(v>>4)&15; b=v&15
            out.append((r*17, g*17, b*17, a*17))
        else:  # ARGB1555
            a = 255 if v & 0x8000 else 0
            r=(v>>10)&31; g=(v>>5)&31; b=v&31
            out.append(((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), a))
    img.putdata(out)
    return img
a = decode(sys.argv[1]); b = decode(sys.argv[2])
bg = Image.new("RGBA", (256*2+8, 256), (200, 200, 200, 255))
for y in range(0, 256, 16):
    for x in range(0, 256, 16):
        c = (255,255,255,255) if ((x+y)//16)%2 == 0 else (120,120,120,255)
        for ox in (0, 264):
            for yy in range(16):
                for xx in range(16):
                    bg.putpixel((ox+x+xx, y+yy), c)
bg.alpha_composite(a, (0, 0)); bg.alpha_composite(b, (264, 0))
bg = bg.resize((bg.width*2, bg.height*2), Image.NEAREST)
bg.convert("RGB").save(sys.argv[3])
print("saved", sys.argv[3])
