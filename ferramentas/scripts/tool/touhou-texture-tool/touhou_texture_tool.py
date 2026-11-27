#!/usr/bin/env python3
"""Touhou Texture Tool - GUI wrapper for THTK + PS2 Image Optimizer.

Requires THTK binaries (thdat, thanm) on PATH and Pillow.
"""
from __future__ import annotations
import os, shutil, subprocess, threading, tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from datetime import datetime
import ps2imgopt
import ps2_texture_encoder

GAME_ID = "7"
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"}

class ToolError(RuntimeError): pass

def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise ToolError(f"Não encontrei '{name}' no PATH. Instale/compile o THTK e tente novamente.")
    return path

def run_tool(args: list[str], cwd: Path | None = None) -> str:
    p = subprocess.run(args, cwd=str(cwd) if cwd else None, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode:
        raise ToolError(f"Comando falhou ({p.returncode}): {' '.join(args)}\n\n{p.stdout}")
    return p.stdout

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Touhou Texture PS2 Tool 2.0 — Touhou 7")
        self.geometry("850x650")
        self.minsize(760, 560)
        self.game_dir = tk.StringVar()
        self.work_dir = tk.StringVar(value=str(Path.home() / "TouhouTextureTool"))
        self.quality = tk.IntVar(value=90)
        self._build()

    def _build(self):
        root = ttk.Frame(self, padding=14); root.pack(fill="both", expand=True)
        ttk.Label(root, text="Touhou Texture PS2 Tool 2.0", font=("TkDefaultFont", 18, "bold")).pack(anchor="w")
        ttk.Label(root, text="Extração, otimização e reinserção de recursos do Touhou 7", foreground="#666").pack(anchor="w", pady=(0,12))
        paths = ttk.LabelFrame(root, text="Pastas", padding=10); paths.pack(fill="x")
        self._path_row(paths, "Pasta do jogo:", self.game_dir, self._choose_game)
        self._path_row(paths, "Pasta de trabalho:", self.work_dir, self._choose_work)
        opts = ttk.Frame(root); opts.pack(fill="x", pady=10)
        ttk.Label(opts, text="Qualidade JPEG (Auto):").pack(side="left")
        ttk.Spinbox(opts, from_=1, to=100, width=5, textvariable=self.quality).pack(side="left", padx=8)
        ttk.Label(opts, text="Transparência: preservada no PNG nesta versão", foreground="#666").pack(side="left", padx=12)

        actions = ttk.LabelFrame(root, text="Operações", padding=10); actions.pack(fill="x")
        grid = ttk.Frame(actions); grid.pack(fill="x")
        self._action(grid, "1. Extrair DAT", self.extract_dat, 0, 0)
        self._action(grid, "2. Extrair ANM e imagens", self.extract_anm, 0, 1)
        self._action(grid, "3. Otimizar imagens (PNG/JPEG)", self.optimize_images, 1, 0)
        self._action(grid, "4. Analisar formatos de textura PS2", self.analyze_ps2_formats, 1, 1)
        ttk.Button(grid, text="5. Reinserir uma textura", command=self.choose_replace_texture).grid(row=2, column=0, sticky="ew", padx=4, pady=4, ipady=7)
        self._action(grid, "6. Reconstruir DAT", self.repack_dat, 3, 0, colspan=2)

        info = ttk.LabelFrame(root, text="Como funciona", padding=10); info.pack(fill="x", pady=10)
        ttk.Label(info, text=("THTK faz a leitura/reconstrução dos arquivos DAT e ANM. O PS2 Image Optimizer "
            "é chamado para escolher JPEG ou PNG no modo Auto. A reinserção ANM usa o nome exato do membro "
            "informado pelo thanm -l. O original é copiado para backup antes de qualquer alteração."),
            wraplength=790, justify="left").pack(anchor="w")
        logframe = ttk.LabelFrame(root, text="Log", padding=8); logframe.pack(fill="both", expand=True)
        self.logbox = tk.Text(logframe, height=12, wrap="word", state="disabled")
        self.logbox.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(logframe, command=self.logbox.yview); scroll.pack(side="right", fill="y")
        self.logbox.configure(yscrollcommand=scroll.set)

    def _path_row(self, parent, label, var, command):
        row = ttk.Frame(parent); row.pack(fill="x", pady=3)
        ttk.Label(row, text=label, width=18).pack(side="left")
        ttk.Entry(row, textvariable=var).pack(side="left", fill="x", expand=True, padx=5)
        ttk.Button(row, text="Procurar...", command=command).pack(side="left")

    def _action(self, parent, text, fn, r, c, colspan=1):
        b = ttk.Button(parent, text=text, command=lambda: self._background(fn))
        b.grid(row=r, column=c, columnspan=colspan, sticky="ew", padx=4, pady=4, ipady=7)
        for i in range(2): parent.columnconfigure(i, weight=1)

    def _choose_game(self):
        p = filedialog.askdirectory(title="Selecione a pasta do Touhou 7")
        if p: self.game_dir.set(p)
    def _choose_work(self):
        p = filedialog.askdirectory(title="Selecione a pasta de trabalho")
        if p: self.work_dir.set(p)
    def log(self, text):
        self.after(0, self._append_log, text)
    def _append_log(self, text):
        self.logbox.configure(state="normal"); self.logbox.insert("end", text.rstrip()+"\n")
        self.logbox.see("end"); self.logbox.configure(state="disabled")
    def _background(self, fn):
        self.quality_value = int(self.quality.get())
        threading.Thread(target=self._run_safe, args=(fn,), daemon=True).start()
    def _run_safe(self, fn):
        try: fn()
        except Exception as e:
            self.log("[ERRO] " + str(e)); self.after(0, lambda: messagebox.showerror("Touhou Texture PS2 Tool", str(e)))
    def dirs(self):
        game = Path(self.game_dir.get()).expanduser()
        work = Path(self.work_dir.get()).expanduser()
        if not game.is_dir(): raise ToolError("Selecione uma pasta válida do jogo.")
        work.mkdir(parents=True, exist_ok=True)
        return game, work

    def extract_dat(self):
        require_tool("thdat"); game, work = self.dirs()
        dats = sorted(game.glob("*.dat"))
        if not dats: raise ToolError("Não encontrei arquivos .dat diretamente na pasta selecionada.")
        outroot = work / "extracted_dat"; outroot.mkdir(parents=True, exist_ok=True)
        extracted = 0
        skipped = 0
        for archive in dats:
            # Alguns jogos guardam score/configuração em arquivos .dat que não são
            # arquivos de recursos THTK. Valide o formato antes de tentar extrair.
            if archive.name.lower() == "score.dat":
                self.log(f"[IGNORADO] {archive.name}: arquivo de pontuação, não é um pacote de recursos.")
                skipped += 1
                continue
            self.log(f"[DAT] Verificando {archive.name}...")
            try:
                run_tool(["thdat", "-l", GAME_ID, str(archive.resolve())])
            except ToolError as e:
                self.log(f"[IGNORADO] {archive.name}: THTK não reconheceu como arquivo DAT do Touhou 7.")
                self.log(f"[DETALHE] {str(e).splitlines()[-1] if str(e).splitlines() else e}")
                skipped += 1
                continue
            dest = outroot / archive.stem
            dest.mkdir(parents=True, exist_ok=True)
            self.log(f"[DAT] Extraindo {archive.name}...")
            run_tool(["thdat", "-x", GAME_ID, str(archive.resolve())], cwd=dest)
            extracted += 1
        if extracted == 0:
            raise ToolError(f"Nenhum DAT válido foi extraído. Arquivos ignorados: {skipped}. Verifique se selecionou a pasta correta do Touhou 7.")
        self.log(f"[OK] DAT válidos extraídos: {extracted}; ignorados: {skipped}. Pasta: {outroot}")

    def extract_anm(self):
        require_tool("thanm"); _, work = self.dirs()
        roots = [work / "extracted_dat"]
        anms = [p for root in roots if root.exists() for p in root.rglob("*.anm")]
        if not anms: raise ToolError("Nenhum .anm encontrado. Primeiro execute 'Extrair DAT'.")
        outroot = work / "extracted_anm"; outroot.mkdir(parents=True, exist_ok=True)
        for archive in anms:
            dest = outroot / (archive.parent.name + "__" + archive.stem)
            dest.mkdir(parents=True, exist_ok=True)
            self.log(f"[ANM] Listando e extraindo {archive.name}...")
            listing = run_tool(["thanm", "-l", GAME_ID, str(archive.resolve())])
            (dest / "manifest.txt").write_text(listing, encoding="utf-8", errors="replace")
            run_tool(["thanm", "-x", GAME_ID, str(archive.resolve())], cwd=dest)
            (dest / "source_archive.txt").write_text(str(archive.resolve()), encoding="utf-8")
        self.log(f"[OK] Imagens/manifestos ANM em: {outroot}")

    def optimize_images(self):
        _, work = self.dirs()
        source = work / "extracted_anm"
        if not source.exists(): raise ToolError("Primeiro extraia os DAT e os ANM.")
        images = [p for p in source.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_EXTS]
        if not images: raise ToolError("Nenhuma imagem extraída encontrada.")
        out = work / "optimized_images"; out.mkdir(parents=True, exist_ok=True)
        for src in images:
            rel = src.relative_to(source); target_dir = out / rel.parent
            try:
                row = ps2imgopt.optimize_one(src, target_dir, self.quality_value, None, None, None)
                self.log(f"[OTIMIZADA] {rel} -> {Path(row['output']).name} ({row['format']})")
            except Exception as e: self.log(f"[AVISO] {rel}: {e}")
        self.log(f"[OK] Otimizações em: {out}")

    def analyze_ps2_formats(self):
        _, work = self.dirs()
        source = work / "extracted_anm"
        if not source.exists():
            raise ToolError("Primeiro extraia os DAT e os ANM.")
        report = work / "reports" / "ps2_texture_analysis.csv"
        rows = ps2_texture_encoder.analyze_tree(source, report)
        counts = {}
        for row in rows:
            fmt = row["recommended_format"]
            counts[fmt] = counts.get(fmt, 0) + 1
        self.log(f"[ANÁLISE PS2] {len(rows)} imagens analisadas. Relatório: {report}")
        for fmt, count in sorted(counts.items()):
            self.log(f"[RECOMENDAÇÃO] {fmt}: {count} textura(s)")
        self.log("[IMPORTANTE] São estimativas de VRAM; ainda não são arquivos GS prontos nem são reinseridos no ANM.")

    def choose_replace_texture(self):
        # File dialogs must run on Tk's main thread; the actual THTK call runs in background.
        _, work = self.dirs()
        archive = filedialog.askopenfilename(title="Selecione o .anm extraído", initialdir=str(work / "extracted_dat"), filetypes=[("ANM", "*.anm")])
        if not archive: return
        member = simple_prompt(self, "Membro ANM", "Digite o nome EXATO do membro conforme a listagem 'thanm -l':")
        if not member: return
        image = filedialog.askopenfilename(title="Selecione a textura otimizada", initialdir=str(work / "optimized_images"), filetypes=[("Imagens", "*.png *.jpg *.jpeg")])
        if not image: return
        threading.Thread(target=self._run_safe, args=(lambda: self._replace_texture_worker(archive, member, image),), daemon=True).start()

    def _replace_texture_worker(self, archive, member, image):
        require_tool("thanm")
        _, work = self.dirs()
        anm = Path(archive); backup_dir = work / "backups"; backup_dir.mkdir(parents=True, exist_ok=True)
        backup = backup_dir / f"{anm.name}.{datetime.now().strftime('%Y%m%d-%H%M%S')}.bak"
        shutil.copy2(anm, backup)
        run_tool(["thanm", "-r", GAME_ID, str(anm.resolve()), member, str(Path(image).resolve())])
        self.log(f"[OK] Textura reinserida em {anm}. Backup: {backup}")

    def repack_dat(self):
        require_tool("thdat"); _, work = self.dirs()
        root = work / "extracted_dat"
        if not root.exists(): raise ToolError("Primeiro extraia os DAT.")
        outroot = work / "rebuilt_dat"; outroot.mkdir(parents=True, exist_ok=True)
        for folder in sorted(p for p in root.iterdir() if p.is_dir()):
            files = sorted(p for p in folder.rglob("*") if p.is_file())
            if not files: continue
            rels = [str(p.relative_to(folder)) for p in files]
            out = outroot / (folder.name + ".dat")
            self.log(f"[DAT] Reconstruindo {out.name} ({len(rels)} arquivos)...")
            run_tool(["thdat", "-c", GAME_ID, str(out.resolve()), *rels], cwd=folder)
        self.log(f"[OK] DAT reconstruídos em: {outroot}. Teste cópias em uma instalação separada do jogo.")

def simple_prompt(parent, title, prompt):
    win = tk.Toplevel(parent); win.title(title); win.transient(parent); win.grab_set()
    ttk.Label(win, text=prompt, wraplength=420).pack(padx=12, pady=10)
    value = tk.StringVar(); entry = ttk.Entry(win, textvariable=value, width=60); entry.pack(padx=12, pady=5); entry.focus_set()
    result = {"value": None}
    def ok(): result["value"] = value.get().strip(); win.destroy()
    def cancel(): win.destroy()
    row = ttk.Frame(win); row.pack(pady=10)
    ttk.Button(row, text="Cancelar", command=cancel).pack(side="left", padx=5)
    ttk.Button(row, text="OK", command=ok).pack(side="left", padx=5)
    parent.wait_window(win)
    return result["value"]

if __name__ == "__main__":
    App().mainloop()
