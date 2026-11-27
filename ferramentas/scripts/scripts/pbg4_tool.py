# PBG4 (th07.dat) reader/writer, matching src/pbg4/Pbg4Archive.cpp and src/pbg4/Lzss.cpp.
#   file: "PBG4" | u32 count | u32 headerSize | u32 indexSize | [entry streams from offset 16] | [index LZSS stream to EOF]
#   index blob (indexSize bytes, LZSS-decoded): per entry: name\0 | u32 dataOffset | u32 decompressedSize | u32 magic
#   entry stream i: LZSS bytes at [dataOffset_i, dataOffset_{i+1}) (the sentinel dataOffset is headerSize)
import struct

RING = 8192          # LZSS_DICTSIZE
MASK = RING - 1

def lzss_decode(src, out_size):
    """Mirror of Lzss::Decompress (dictionary starts zeroed; ring index 0 is the end marker)."""
    dic = bytearray(RING)
    out = bytearray()
    bits = []  # generator of bits, MSB-first per byte
    pos = 0
    def rbit():
        nonlocal pos
        byte = src[pos >> 3] if (pos >> 3) < len(src) else 0
        b = (byte >> (7 - (pos & 7))) & 1
        pos += 1
        return b
    def rbits(n):
        v = 0
        for _ in range(n):
            v = (v << 1) | rbit()
        return v
    head = 1
    while True:
        if rbit():
            c = rbits(8)
            if len(out) >= out_size:
                raise ValueError("overflow")
            out.append(c); dic[head] = c; head = (head + 1) & MASK
        else:
            off = rbits(13)
            if off == 0:
                break
            ln = rbits(4) + 3
            for i in range(ln):
                c = dic[(off + i) & MASK]
                if len(out) >= out_size:
                    raise ValueError("overflow")
                out.append(c); dic[head] = c; head = (head + 1) & MASK
    return bytes(out)

def lzss_encode(data):
    """Greedy LZSS matching the decoder above (matches of 3..18 bytes, distance 1..8191, source ring index != 0)."""
    bitlist = []
    def put(v, n):
        for i in range(n - 1, -1, -1):
            bitlist.append((v >> i) & 1)
    n_total = len(data)
    table = {}          # 3-byte key -> list of stream positions (recent last)
    def insert(p):
        if p + 3 <= n_total:
            k = data[p:p + 3]
            lst = table.setdefault(k, [])
            lst.append(p)
            if len(lst) > 48:
                del lst[0]
    n = 0
    while n < n_total:
        best_len, best_src = 0, -1
        if n + 3 <= n_total:
            for s in reversed(table.get(data[n:n + 3], [])):
                d = n - s
                if d < 1 or d > MASK or ((s + 1) & MASK) == 0:
                    continue
                ln = 0
                limit = min(18, n_total - n)
                while ln < limit and data[s + ln] == data[n + ln]:
                    ln += 1
                if ln > best_len:
                    best_len, best_src = ln, s
                    if ln == 18:
                        break
        if best_len >= 3:
            put(0, 1)
            put((best_src + 1) & MASK, 13)
            put(best_len - 3, 4)
            for p in range(n, n + best_len):
                insert(p)
            n += best_len
        else:
            put(1, 1)
            put(data[n], 8)
            insert(n)
            n += 1
    put(0, 1)
    put(0, 13)
    while len(bitlist) % 8:
        bitlist.append(0)
    out = bytearray()
    for i in range(0, len(bitlist), 8):
        b = 0
        for bit in bitlist[i:i + 8]:
            b = (b << 1) | bit
        out.append(b)
    return bytes(out)

def read_archive(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"PBG4", "not a PBG4 archive"
    count, header_size, index_size = struct.unpack_from("<III", raw, 4)
    index = lzss_decode(raw[header_size:], index_size)
    entries = []
    p = 0
    for _ in range(count):
        end = index.index(b"\0", p)
        name = index[p:end].decode("latin-1")
        p = end + 1
        off, dsize, magic = struct.unpack_from("<III", index, p)
        p += 12
        entries.append({"name": name, "offset": off, "dsize": dsize, "magic": magic})
    entries_sorted = sorted(entries, key=lambda e: e["offset"])
    bounds = [e["offset"] for e in entries_sorted] + [header_size]
    for e in entries:
        i = bounds.index(e["offset"])
        e["comp"] = raw[e["offset"]:bounds[i + 1]]
    return entries

def write_archive(path, entries):
    """entries: list of dicts with name, magic, and either 'comp' (reused stream) or 'data' (raw bytes to compress)."""
    body = bytearray()
    index = bytearray()
    cur = 16
    for e in entries:
        comp = e["comp"] if "comp" in e else lzss_encode(e["data"])
        dsize = e["dsize"] if "dsize" in e else len(e["data"])
        e["_off"] = cur
        body += comp
        cur += len(comp)
        index += e["name"].encode("latin-1") + b"\0" + struct.pack("<III", cur - len(comp), dsize, e["magic"])
    header_size = cur
    index_comp = lzss_encode(bytes(index))
    out = b"PBG4" + struct.pack("<III", len(entries), header_size, len(index)) + bytes(body) + index_comp
    open(path, "wb").write(out)
    return out

if __name__ == "__main__":
    import random, sys
    random.seed(1)
    samples = [b"", b"a", b"abc", b"abcabcabcabcabc" * 50, bytes(random.getrandbits(8) for _ in range(5000)),
               bytes(random.choice(b"ab\x00\xff") for _ in range(20000))]
    for s in samples:
        c = lzss_encode(s)
        assert lzss_decode(c, len(s)) == s, "python round-trip failed"
    print("python round-trip ok on", len(samples), "samples")
