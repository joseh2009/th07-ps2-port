#!/usr/bin/env python3
"""
PS2 Image Optimizer - v1
Creates ordinary JPEG/PNG files suitable for a demand-loaded PS2 application.

Requires: Pillow (pip install Pillow)
"""
from __future__ import annotations

import argparse
import csv
import io
import sys
from pathlib import Path
from PIL import Image, ImageOps

SUPPORTED = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"}


def decoded_bytes(width: int, height: int, bpp: int) -> int:
    return (width * height * bpp + 7) // 8


def human_size(n: int) -> str:
    return f"{n / 1024:.1f} KB" if n < 1024 * 1024 else f"{n / (1024 * 1024):.2f} MB"


def encode_png(img: Image.Image) -> bytes:
    out = io.BytesIO()
    # Preserve alpha and pixel values; lossless PNG optimization.
    img.save(out, format="PNG", optimize=True, compress_level=9)
    return out.getvalue()


def encode_jpeg(img: Image.Image, quality: int) -> bytes:
    rgb = img.convert("RGB")
    out = io.BytesIO()
    rgb.save(out, format="JPEG", quality=quality, optimize=True, progressive=False, subsampling=0)
    return out.getvalue()


def optimize_one(src: Path, out_dir: Path, quality: int, max_w: int | None,
                 max_h: int | None, force: str | None) -> dict:
    with Image.open(src) as opened:
        img = ImageOps.exif_transpose(opened)
        img.load()
        original_size = src.stat().st_size

        if max_w or max_h:
            bound_w = max_w or img.width
            bound_h = max_h or img.height
            if img.width > bound_w or img.height > bound_h:
                img.thumbnail((bound_w, bound_h), Image.Resampling.LANCZOS)

        has_alpha = ("A" in img.getbands()) or ("transparency" in img.info)
        if has_alpha:
            work = img.convert("RGBA")
        else:
            work = img.convert("RGB")

        png_data = encode_png(work)
        jpg_data = None if has_alpha else encode_jpeg(work, quality)

        if force == "png":
            chosen_ext, data = ".png", png_data
        elif force == "jpg":
            if has_alpha:
                raise ValueError("JPEG não suporta transparência. Use --format auto ou png.")
            chosen_ext, data = ".jpg", jpg_data
        elif has_alpha or jpg_data is None or len(png_data) <= len(jpg_data):
            chosen_ext, data = ".png", png_data
        else:
            chosen_ext, data = ".jpg", jpg_data

        out_dir.mkdir(parents=True, exist_ok=True)
        dest = out_dir / (src.stem + chosen_ext)
        # Avoid silently overwriting another source with the same stem.
        if dest.exists() and dest.resolve() != src.resolve():
            dest = out_dir / (src.stem + "_optimized" + chosen_ext)
        dest.write_bytes(data)

        return {
            "source": str(src),
            "output": str(dest),
            "input_bytes": original_size,
            "output_bytes": len(data),
            "width": work.width,
            "height": work.height,
            "format": chosen_ext[1:].upper(),
            "ram_rgb565_bytes": decoded_bytes(work.width, work.height, 16),
            "ram_rgba32_bytes": decoded_bytes(work.width, work.height, 32),
        }


def main() -> int:
    parser = argparse.ArgumentParser(description="Otimizador de imagens para projetos PlayStation 2.")
    parser.add_argument("input", type=Path, help="Imagem ou pasta de imagens")
    parser.add_argument("-o", "--output", type=Path, default=Path("ps2_optimized"), help="Pasta de saída")
    parser.add_argument("--quality", type=int, default=90, help="Qualidade JPEG (padrão: 90)")
    parser.add_argument("--max-width", type=int, default=None, help="Largura máxima opcional")
    parser.add_argument("--max-height", type=int, default=None, help="Altura máxima opcional")
    parser.add_argument("--format", choices=["auto", "jpg", "png"], default="auto",
                        help="Formato de saída; auto escolhe o menor entre PNG sem perdas e JPEG")
    args = parser.parse_args()

    if not 1 <= args.quality <= 100:
        parser.error("--quality deve estar entre 1 e 100")
    if args.max_width is not None and args.max_width < 1:
        parser.error("--max-width deve ser maior que zero")
    if args.max_height is not None and args.max_height < 1:
        parser.error("--max-height deve ser maior que zero")

    files = [args.input] if args.input.is_file() else sorted(
        p for p in args.input.rglob("*") if p.is_file() and p.suffix.lower() in SUPPORTED
    )
    if not files:
        print("Nenhuma imagem suportada encontrada.", file=sys.stderr)
        return 2

    rows = []
    for src in files:
        try:
            row = optimize_one(src, args.output, args.quality, args.max_width,
                               args.max_height, None if args.format == "auto" else args.format)
            rows.append(row)
            saved = 100 * (1 - row["output_bytes"] / row["input_bytes"]) if row["input_bytes"] else 0
            print(f"[OK] {src.name} -> {Path(row['output']).name} | "
                  f"{row['format']} | {human_size(row['input_bytes'])} -> "
                  f"{human_size(row['output_bytes'])} | redução {saved:.1f}%")
            print(f"     RAM estimada: RGB565 {human_size(row['ram_rgb565_bytes'])}; "
                  f"RGBA32 {human_size(row['ram_rgba32_bytes'])}")
        except Exception as exc:
            print(f"[ERRO] {src}: {exc}", file=sys.stderr)

    if rows:
        report = args.output / "report.csv"
        args.output.mkdir(parents=True, exist_ok=True)
        with report.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nRelatório salvo em: {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
