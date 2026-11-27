import struct, sys
from PIL import Image
sys.path.insert(0, sys.argv[4])
from anm_downscale import decode_px
def load(path):
    data = open(path, "rb").read()
    t = struct.unpack_from("<i", data, 0x30)[0]
    fmt = struct.unpack_from("<h", data, t + 6)[0]
    w, h = struct.unpack_from("<2h", data, t + 8)[0], struct.unpack_from("<2h", data, t + 10)[0]
    img = Image.new("RGBA", (w, h))
    img.putdata(decode_px(fmt, data[t+0x10: t+0x10+w*h*2]))
    return img
a = load(sys.argv[1])                       # 256x256 original
b = load(sys.argv[2])                       # 128x128 stored
b_up = b.resize((256, 256), Image.BILINEAR) # what the GPU shows at the same on-screen size
bg = Image.new("RGBA", (256*2+8, 256), (90, 90, 90, 255))
bg.alpha_composite(a, (0, 0)); bg.alpha_composite(b_up, (264, 0))
bg.resize((bg.width*2, bg.height*2), Image.NEAREST).convert("RGB").save(sys.argv[3])
print("saved", sys.argv[3], "stored size", b.size)
