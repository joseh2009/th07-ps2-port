# Read-only inspector for TH07 ANM v2 files, following src/AnmManager.hpp layouts.
import struct, sys

FMT = {1: ("ARGB8888", 4), 2: ("ARGB1555", 2), 3: ("RGB565", 2), 4: ("BGR24", 3), 5: ("ARGB4444", 2)}

def cstr(data, off):
    if off <= 0 or off >= len(data):
        return None
    end = data.find(b"\0", off)
    return data[off:end if end >= 0 else off + 64].decode("latin-1", "replace")

def main(path):
    data = open(path, "rb").read()
    print("file size:", len(data))
    off = 0
    idx = 0
    while True:
        if off + 0x40 > len(data):
            print("truncated header at", off); break
        (numSprites, numScripts, textureIdx, width, height, fmt, colorKey,
         nameOffset, spriteIdxOffset, mipmapNameOffset, version, priority,
         textureOffset) = struct.unpack_from("<13i", data, off)
        hasData, ownsMemory = data[off + 0x34], data[off + 0x35]
        nextOffset, = struct.unpack_from("<i", data, off + 0x38)
        name = cstr(data, off + nameOffset) if nameOffset else None
        print(f"\n== entry {idx} @0x{off:x} ==")
        print(f" version={version} numSprites={numSprites} numScripts={numScripts}")
        print(f" width={width} height={height} format={fmt} colorKey=0x{colorKey & 0xffffffff:08x}")
        print(f" name={name!r} hasData={hasData} mipmapNameOffset={mipmapNameOffset}")
        print(f" nameOffset={nameOffset} textureOffset={textureOffset} nextOffset={nextOffset}")
        if hasData and textureOffset:
            t = off + textureOffset
            magic, colorDepth, imageType, imgFmt, w, h, unused = struct.unpack_from("<6hi", data, t)[0:0] or (None,)*7
            magic, colorDepth, imageType, imgFmt, w, h = struct.unpack_from("<6h", data, t)
            unused, = struct.unpack_from("<i", data, t + 12)
            bpp = FMT.get(imgFmt, ("?", 0))
            payload = w * h * bpp[1]
            avail = len(data) - (t + 0x10)
            print(f" embedded: magic=0x{magic & 0xffff:04x} colorDepth={colorDepth} imageType={imageType}"
                  f" format={imgFmt}({bpp[0]}) {w}x{h} unused_c=0x{unused & 0xffffffff:08x}")
            print(f" embedded payload: need {payload} bytes, file has {avail} after header")
        if nextOffset == 0:
            break
        off += nextOffset
        idx += 1
    print("\nentries:", idx + 1)

if __name__ == "__main__":
    main(sys.argv[1])
