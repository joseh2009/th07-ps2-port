#!/usr/bin/env python3
"""
Touhou 7 - reduzir texturas do th07.dat (PS2 port).

Lê o SEU th07.dat, diminui as texturas grandes dos arquivos ANM e grava um novo arquivo com o
mesmo formato. O arquivo do jogo NÃO vem com esta ferramenta: você usa a sua cópia.

Modo janela (sem argumentos):
    python3 th07_dat_reducer.py

Modo linha de comando:
    python3 th07_dat_reducer.py th07.dat th07_reduzido.dat
    python3 th07_dat_reducer.py th07.dat th07_reduzido.dat --factor 0.5 --min-side 512

Requisitos: Python 3 e Pillow (pip install pillow).
A janela precisa do tkinter (já vem com o Python no Windows e no macOS; no Ubuntu/Debian:
sudo apt install python3-tk).
"""

import argparse
import os
import queue
import struct
import sys
import threading

RING = 8192  # LZSS_DICTSIZE
MASK = RING - 1

# ---------------------------------------------------------------------------------------------
# PBG4 (th07.dat): mesmo formato lido por src/pbg4/Pbg4Archive.cpp e src/pbg4/Lzss.cpp
#   "PBG4" | u32 count | u32 headerSize | u32 indexSize | [fluxos LZSS a partir de 16] | [índice LZSS até o fim]
#   índice (indexSize bytes ao descomprimir): por entrada: nome\0 | u32 dataOffset | u32 tamanho | u32 magic
# ---------------------------------------------------------------------------------------------


def lzss_decode(src, out_size):
    """Espelha Lzss::Decompress (dicionário começa zerado; índice 0 do anel é o marcador de fim)."""
    dic = bytearray(RING)
    out = bytearray()
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
                raise ValueError("LZSS: saída maior que o esperado")
            out.append(c)
            dic[head] = c
            head = (head + 1) & MASK
        else:
            off = rbits(13)
            if off == 0:
                break
            ln = rbits(4) + 3
            for i in range(ln):
                c = dic[(off + i) & MASK]
                if len(out) >= out_size:
                    raise ValueError("LZSS: saída maior que o esperado")
                out.append(c)
                dic[head] = c
                head = (head + 1) & MASK
    return bytes(out)


def lzss_encode(data):
    """LZSS guloso compatível com o decodificador acima (cópias de 3 a 18 bytes, distância 1 a 8191)."""
    bitlist = []

    def put(v, n):
        for i in range(n - 1, -1, -1):
            bitlist.append((v >> i) & 1)

    n_total = len(data)
    table = {}  # chave de 3 bytes -> posições recentes

    def insert(p):
        if p + 3 <= n_total:
            lst = table.setdefault(data[p:p + 3], [])
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
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != b"PBG4":
        raise ValueError("não é um arquivo PBG4 (th07.dat)")
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
    bounds = sorted([e["offset"] for e in entries] + [header_size])
    for e in entries:
        i = bounds.index(e["offset"])
        e["comp"] = raw[e["offset"]:bounds[i + 1]]
    return entries


def write_archive(path, entries):
    """entries: nome, magic, e 'comp' (fluxo reaproveitado) ou 'data' (bytes a comprimir)."""
    body = bytearray()
    index = bytearray()
    cur = 16
    for e in entries:
        comp = e["comp"] if "comp" in e else lzss_encode(e["data"])
        dsize = e["dsize"] if "dsize" in e else len(e["data"])
        body += comp
        index += e["name"].encode("latin-1") + b"\0" + struct.pack("<III", cur, dsize, e["magic"])
        cur += len(comp)
    header_size = cur
    index_comp = lzss_encode(bytes(index))
    out = b"PBG4" + struct.pack("<III", len(entries), header_size, len(index)) + bytes(body) + index_comp
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


# ---------------------------------------------------------------------------------------------
# ANM v2: reduz as texturas embutidas (mesmo algoritmo do anm_downscale.py validado)
# ---------------------------------------------------------------------------------------------

FMT_BPP = {1: 4, 2: 2, 3: 2, 4: 3, 5: 2}


def pot(x):
    n = 1
    while n < x:
        n <<= 1
    return n


def decode_px(fmt, raw):
    out = []
    if fmt == 1:  # B,G,R,A
        for i in range(0, len(raw), 4):
            out.append((raw[i + 2], raw[i + 1], raw[i], raw[i + 3]))
    elif fmt == 2:  # ARGB1555
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i + 1] << 8)
            r = ((v >> 10) & 31) * 255 // 31
            g = ((v >> 5) & 31) * 255 // 31
            b = (v & 31) * 255 // 31
            out.append((r, g, b, 255 if v & 0x8000 else 0))
    elif fmt == 3:  # RGB565
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i + 1] << 8)
            r = ((v >> 11) & 31) * 255 // 31
            g = ((v >> 5) & 63) * 255 // 63
            b = (v & 31) * 255 // 31
            out.append((r, g, b, 255))
    elif fmt == 4:  # BGR24
        for i in range(0, len(raw), 3):
            out.append((raw[i + 2], raw[i + 1], raw[i], 255))
    elif fmt == 5:  # ARGB4444
        for i in range(0, len(raw), 2):
            v = raw[i] | (raw[i + 1] << 8)
            a = (v >> 12) & 15
            r = (v >> 8) & 15
            g = (v >> 4) & 15
            b = v & 15
            out.append((r * 17, g * 17, b * 17, a * 17))
    else:
        raise ValueError(f"formato de textura desconhecido: {fmt}")
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


def resample(w, h, px, nw, nh):
    from PIL import Image  # Pillow

    # filtro de caixa com alfa pré-multiplicado: pixels transparentes não escurecem os vizinhos
    img = Image.new("RGBA", (w, h))
    img.putdata([(r * a // 255, g * a // 255, b * a // 255, a) for r, g, b, a in px])
    small = img.resize((nw, nh), Image.BOX)
    data = small.get_flattened_data() if hasattr(small, "get_flattened_data") else small.getdata()
    out = []
    for r, g, b, a in data:
        if a:
            out.append((min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a))
        else:
            out.append((0, 0, 0, 0))
    return out


def convert_anm(data, factor, min_side):
    """Devolve (bytes novos, relatório). Só reescreve texturas que são o final da entrada."""
    data = bytearray(data)
    report = []
    out = bytearray()
    off = 0
    while True:
        if struct.unpack_from("<i", data, off + 0x28)[0] != 2:
            raise ValueError("não é ANM versão 2")
        next_offset = struct.unpack_from("<i", data, off + 0x38)[0]
        end = off + next_offset if next_offset else len(data)
        entry = bytearray(data[off:end])
        has_data = entry[0x34]
        t_off = struct.unpack_from("<i", entry, 0x30)[0]
        new_entry = entry
        note = None
        if has_data and t_off:
            fmt = struct.unpack_from("<h", entry, t_off + 6)[0]
            w, h = struct.unpack_from("<2h", entry, t_off + 8)[0], struct.unpack_from("<2h", entry, t_off + 10)[0]
            blk = 0x10 + w * h * FMT_BPP[fmt]
            if t_off + blk != len(entry):
                note = "pulada: bloco de textura não é o fim da entrada"
            elif max(w, h) < min_side:
                note = f"mantida (pequena {w}x{h})"
            elif pot(max(1, round(w * factor))) * pot(max(1, round(h * factor))) >= pot(w) * pot(h):
                note = f"mantida ({w}x{h}: a alocação em potência de dois não diminuiria)"
            else:
                nw, nh = max(1, round(w * factor)), max(1, round(h * factor))
                raw = bytes(entry[t_off + 0x10:t_off + blk])
                new_px = resample(w, h, decode_px(fmt, raw), nw, nh)
                payload = encode_px(fmt, new_px)
                head = bytearray(entry[t_off:t_off + 0x10])
                struct.pack_into("<2h", head, 8, nw, nh)
                new_entry = bytearray(entry[:t_off]) + head + payload
                note = f"reduzida {w}x{h} -> {nw}x{nh} (formato {fmt})"
        if next_offset:
            struct.pack_into("<i", new_entry, 0x38, len(new_entry))
        out += new_entry
        if note:
            report.append(note)
        if not next_offset:
            break
        off = end
    return bytes(out), report


# ---------------------------------------------------------------------------------------------
# Fluxo principal
# ---------------------------------------------------------------------------------------------


def reduce_dat(src, dst, factor=0.5, min_side=512, log=print, progress=None):
    """Lê src, reduz as texturas dos ANM e grava dst. Devolve (arquivos alterados, bytes antes, bytes depois)."""
    if os.path.abspath(src) == os.path.abspath(dst):
        raise ValueError("o arquivo de saída não pode ser o mesmo que o de entrada")
    if not (0.0 < factor <= 1.0):
        raise ValueError("o fator precisa estar entre 0 e 1")

    entries = read_archive(src)
    total = len(entries)
    changed = 0
    for i, e in enumerate(entries):
        if e["name"].lower().endswith(".anm"):
            data = lzss_decode(e["comp"], e["dsize"])
            try:
                new, report = convert_anm(data, factor, min_side)
            except Exception as ex:  # arquivo não é ANM v2: fica como está
                log(f"  {e['name']}: copiado sem alteração ({ex})")
                report, new = [], data
            if new != data:
                e.pop("comp")
                e["data"] = new
                e["dsize"] = len(new)
                changed += 1
                log(f"  {e['name']}: " + "; ".join(report))
        if progress:
            progress(i + 1, total, e["name"])

    if progress:
        progress(total, total, "gravando o arquivo")
    before = os.path.getsize(src)
    after = write_archive(dst, entries)
    log(f"Pronto: {changed} arquivos ANM alterados. {before} -> {after} bytes.")
    return changed, before, after


def run_cli(args):
    try:
        reduce_dat(args.input, args.output, args.factor, args.min_side,
                   progress=lambda i, n, name: print(f"[{i}/{n}] {name}", flush=True))
    except Exception as ex:
        print(f"Erro: {ex}", file=sys.stderr)
        return 1
    return 0


def run_gui():
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError:
        print("O tkinter não está instalado. Use a linha de comando:")
        print("  python3 th07_dat_reducer.py th07.dat th07_reduzido.dat")
        return 1

    root = tk.Tk()
    root.title("Touhou 7 - reduzir th07.dat")
    root.minsize(620, 470)
    frm = ttk.Frame(root, padding=12)
    frm.pack(fill="both", expand=True)
    frm.columnconfigure(1, weight=1)

    in_var = tk.StringVar()
    out_var = tk.StringVar()
    factor_var = tk.StringVar(value="0.5")
    side_var = tk.StringVar(value="512")

    def browse_in():
        path = filedialog.askopenfilename(title="Escolha o seu th07.dat",
                                          filetypes=[("Arquivo DAT", "*.dat"), ("Todos", "*.*")])
        if path:
            in_var.set(path)
            if not out_var.get() or out_var.get() == "":
                out_var.set(os.path.join(os.path.dirname(path), "th07_reduzido.dat"))

    def browse_out():
        path = filedialog.asksaveasfilename(title="Onde salvar o arquivo reduzido", defaultextension=".dat",
                                            initialfile="th07_reduzido.dat",
                                            filetypes=[("Arquivo DAT", "*.dat")])
        if path:
            out_var.set(path)

    ttk.Label(frm, text="th07.dat original (o seu):").grid(row=0, column=0, sticky="w", pady=4)
    ttk.Entry(frm, textvariable=in_var).grid(row=0, column=1, sticky="ew", padx=6)
    ttk.Button(frm, text="Procurar...", command=browse_in).grid(row=0, column=2)

    ttk.Label(frm, text="Salvar como:").grid(row=1, column=0, sticky="w", pady=4)
    ttk.Entry(frm, textvariable=out_var).grid(row=1, column=1, sticky="ew", padx=6)
    ttk.Button(frm, text="Procurar...", command=browse_out).grid(row=1, column=2)

    ttk.Label(frm, text="Tamanho da redução:").grid(row=2, column=0, sticky="w", pady=4)
    ttk.Combobox(frm, textvariable=factor_var, values=["0.5", "0.75"], width=8, state="readonly").grid(
        row=2, column=1, sticky="w", padx=6)
    ttk.Label(frm, text="0.5 = metade do tamanho (recomendado). 0.75 quase não economiza memória.").grid(
        row=3, column=1, columnspan=2, sticky="w", padx=6)

    ttk.Label(frm, text="Só texturas com lado maior ou igual a:").grid(row=4, column=0, sticky="w", pady=4)
    ttk.Combobox(frm, textvariable=side_var, values=["512", "256", "128"], width=8, state="readonly").grid(
        row=4, column=1, sticky="w", padx=6)

    bar = ttk.Progressbar(frm, mode="determinate")
    bar.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(10, 4))

    log_box = tk.Text(frm, height=14, wrap="word", state="disabled")
    log_box.grid(row=6, column=0, columnspan=3, sticky="nsew")
    frm.rowconfigure(6, weight=1)

    start_btn = ttk.Button(frm, text="Reduzir")
    start_btn.grid(row=7, column=0, columnspan=3, pady=(8, 0))

    msgs = queue.Queue()

    def log(text):
        msgs.put(("log", text))

    def worker(src, dst, factor, min_side):
        try:
            reduce_dat(src, dst, factor, min_side, log=log,
                       progress=lambda i, n, name: msgs.put(("progress", (i, n, name))))
            msgs.put(("done", None))
        except Exception as ex:
            msgs.put(("error", str(ex)))

    def start():
        src, dst = in_var.get().strip(), out_var.get().strip()
        if not src or not os.path.isfile(src):
            messagebox.showerror("Erro", "Escolha o seu th07.dat.")
            return
        if not dst:
            messagebox.showerror("Erro", "Escolha onde salvar o arquivo.")
            return
        if os.path.abspath(src) == os.path.abspath(dst):
            messagebox.showerror("Erro", "O arquivo de saída não pode ser o mesmo do original.")
            return
        start_btn.state(["disabled"])
        log_box.configure(state="normal")
        log_box.delete("1.0", "end")
        log_box.configure(state="disabled")
        threading.Thread(target=worker, args=(src, dst, float(factor_var.get()), int(side_var.get())),
                         daemon=True).start()
        root.after(100, poll)

    def poll():
        try:
            while True:
                kind, val = msgs.get_nowait()
                if kind == "log":
                    log_box.configure(state="normal")
                    log_box.insert("end", val + "\n")
                    log_box.see("end")
                    log_box.configure(state="disabled")
                elif kind == "progress":
                    i, n, name = val
                    bar["maximum"] = n
                    bar["value"] = i
                elif kind == "done":
                    start_btn.state(["!disabled"])
                    messagebox.showinfo("Pronto", "Arquivo reduzido salvo em:\n" + out_var.get())
                    return
                elif kind == "error":
                    start_btn.state(["!disabled"])
                    messagebox.showerror("Erro", val)
                    return
        except queue.Empty:
            pass
        root.after(100, poll)

    start_btn.configure(command=start)
    root.mainloop()
    return 0


def main(argv):
    if not argv:
        return run_gui()
    parser = argparse.ArgumentParser(description="Reduz as texturas grandes do th07.dat (use o seu arquivo).")
    parser.add_argument("input", nargs="?", help="o seu th07.dat original")
    parser.add_argument("output", nargs="?", help="arquivo de saída (ex.: th07_reduzido.dat)")
    parser.add_argument("--factor", type=float, default=0.5, help="fator de redução (padrão 0.5)")
    parser.add_argument("--min-side", type=int, default=512,
                        help="só reduz texturas com lado maior ou igual a este valor (padrão 512)")
    parser.add_argument("--gui", action="store_true", help="abre a janela")
    args = parser.parse_args(argv)
    if args.gui or not (args.input and args.output):
        if args.gui:
            return run_gui()
        parser.error("informe o arquivo de entrada e o de saída, ou use --gui")
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Falta o Pillow. Instale com: pip install pillow", file=sys.stderr)
        return 1
    return run_cli(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
