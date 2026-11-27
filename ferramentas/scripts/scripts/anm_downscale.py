# Downscale every embedded texture of TH07 ANM v2 files, keeping each pixel's original format.
# Header width/height and sprite rectangles stay in the original (logical) units: the port derives
# cols = stored_width / header_width (AnmManager.cpp LoadAnm) and scales sprite rects with it.
# Only textures whose embedded block is the last thing in its entry are rewritten; others are copied.
import struct, sys
from PIL import Image

FMT_BPP = {1: 4, 2: 2, 3: 2, 4: 3, 5: 2}

def pot(x):
    n = 1
    while n < x:
        n <<= 1
    return n

def decode_px(fmt, raw):
    out = []
    if fmt == 1:  # B,G,R,A bytes
        for i in range(0, len(raw), 4):
            b, g, r, a = raw[i], raw[i+1], raw[i+2], raw[i+3]
            out.append((r, g, b, a))
    elif fmt == 2:
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i+1] << 8)
            r = ((v >> 10) & 31) * 255 // 31; g = ((v >> 5) & 31) * 255 // 31; b = (v & 31) * 255 // 31
            out.append((r, g, b, 255 if v & 0x8000 else 0))
    elif fmt == 3:
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i+1] << 8)
            r = ((v >> 11) & 31) * 255 // 31; g = ((v >> 5) & 63) * 255 // 63; b = (v & 31) * 255 // 31
            out.append((r, g, b, 255))
    elif fmt == 4:  # B,G,R
        for i in range(0, len(raw), 3):
            out.append((raw[i+2], raw[i+1], raw[i], 255))
    elif fmt == 5:
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i+1] << 8)
            a = (v >> 12) & 15; r = (v >> 8) & 15; g = (v >> 4) & 15; b = v & 15
            out.append((r * 17, g * 17, b * 17, a * 17))
    return out

def encode_px(fmt, px):
    buf = bytearray()
    for r, g, b, a in px:
        if fmt == 1:
            buf += bytes((b, g, r, a))
        elif fmt == 2:
            v = ((1 if a >= 128 else 0) << 15) | (round(r * 31 / 255) << 10) | (round(g * 31 / 255) << 5) | round(b * 31 / 255)
            buf += struct.pack("<H", v)
        elif fmt == 3:
            v = (round(r * 31 / 255) << 11) | (round(g * 63 / 255) << 5) | round(b * 31 / 255)
            buf += struct.pack("<H", v)
        elif fmt == 4:
            buf += bytes((b, g, r))
        elif fmt == 5:
            v = (round(a / 17) << 12) | (round(r / 17) << 8) | (round(g / 17) << 4) | round(b / 17)
            buf += struct.pack("<H", v)
    return bytes(buf)

def resample(fmt, w, h, px, nw, nh):
    # premultiplied-alpha box filter, so transparent pixels do not darken their neighbours
    img = Image.new("RGBA", (w, h))
    prem = []
    for r, g, b, a in px:
        prem.append((r * a // 255, g * a // 255, b * a // 255, a))
    img.putdata(prem)
    small = img.resize((nw, nh), Image.BOX)
    out = []
    for r, g, b, a in small.get_flattened_data() if hasattr(small, "get_flattened_data") else small.getdata():
        if a:
            out.append((min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a))
        else:
            out.append((0, 0, 0, 0))
    return out

def convert(data, factor, min_side):
    data = bytearray(data)
    report = []
    out = bytearray()
    off = 0
    while True:
        assert struct.unpack_from("<i", data, off + 0x28)[0] == 2, "not ANM v2"
        nextOffset = struct.unpack_from("<i", data, off + 0x38)[0]
        end = off + nextOffset if nextOffset else len(data)
        entry = bytearray(data[off:end])
        hasData = entry[0x34]
        tOff = struct.unpack_from("<i", entry, 0x30)[0]
        new_entry = entry
        note = "copied"
        if hasData and tOff:
            fmt = struct.unpack_from("<h", entry, tOff + 6)[0]
            w, h = struct.unpack_from("<2h", entry, tOff + 8)[0], struct.unpack_from("<2h", entry, tOff + 10)[0]
            blk = 0x10 + w * h * FMT_BPP[fmt]
            if tOff + blk != len(entry):
                note = "skipped: texture block is not the entry tail"
            elif max(w, h) < min_side:
                note = f"kept (small {w}x{h})"
            elif pot(max(1, round(w * factor))) * pot(max(1, round(h * factor))) >= pot(w) * pot(h):
                note = f"kept ({w}x{h}: power-of-two allocation would not shrink)"
            else:
                nw, nh = max(1, round(w * factor)), max(1, round(h * factor))
                raw = bytes(entry[tOff + 0x10: tOff + blk])
                px = decode_px(fmt, raw)
                new_px = resample(fmt, w, h, px, nw, nh)
                payload = encode_px(fmt, new_px)
                head = bytearray(entry[tOff:tOff + 0x10])
                struct.pack_into("<2h", head, 8, nw, nh)
                new_entry = bytearray(entry[:tOff]) + head + payload
                note = f"downscaled {w}x{h} -> {nw}x{nh} (format {fmt}), texture bytes {len(raw)} -> {len(payload)}"
        if nextOffset:
            struct.pack_into("<i", new_entry, 0x38, len(new_entry))
        out += new_entry
        report.append(note)
        if not nextOffset:
            break
        off = end
    return bytes(out), report

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    factor = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    min_side = int(sys.argv[4]) if len(sys.argv) > 4 else 128
    data = open(src, "rb").read()
    new, report = convert(data, factor, min_side)
    open(dst, "wb").write(new)
    for r in report:
        print(" -", r)
    print(f"{src}: {len(data)} -> {len(new)} bytes")
