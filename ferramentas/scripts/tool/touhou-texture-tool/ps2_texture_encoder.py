#!/usr/bin/env python3
"""PS2 texture format analysis and conservative format recommendation.

This module deliberately does NOT emit GS-uploadable texture memory. It reports
pixel-storage estimates and chooses only lossless palette formats when the
source colour count permits it. GS swizzling, CLUT layout and ANM integration
must be validated against the target port before binary replacement is enabled.
"""
from __future__ import annotations

import csv
from pathlib import Path
from PIL import Image

SUPPORTED = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"}


def _pow2(value: int) -> int:
    n = 1
    while n < max(1, value):
        n <<= 1
    return n


def _has_partial_alpha(img: Image.Image) -> bool:
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    lo, hi = alpha.getextrema()
    if lo == hi == 255:
        return False
    # Binary alpha can be represented by RGBA5551; partial alpha cannot.
    hist = alpha.histogram()
    return any(hist[i] for i in range(1, 255))


def _alpha_present(img: Image.Image) -> bool:
    rgba = img.convert("RGBA")
    return rgba.getchannel("A").getextrema() != (255, 255)


def _unique_rgba(img: Image.Image, limit: int = 257) -> int:
    # Pillow's adaptive quantizer gives a bounded count without building an
    # unbounded Python set for very large images.
    rgba = img.convert("RGBA")
    return len(rgba.getcolors(maxcolors=limit) or []) or limit


def analyze_image(path: Path) -> dict:
    with Image.open(path) as opened:
        img = opened.convert("RGBA")
        w, h = img.size
        colors = _unique_rgba(img)
        alpha = _alpha_present(img)
        partial = _has_partial_alpha(img)

    # GS texture allocation in the current port rounds dimensions up to powers
    # of two. These are payload estimates; CLUT overhead is shown separately.
    aw, ah = _pow2(w), _pow2(h)
    pixels = aw * ah
    candidates = {
        "RGBA32": pixels * 4,
        "PSMCT16": pixels * 2,
        "PSMT8": pixels + 256 * 4,
        "PSMT4": (pixels + 1) // 2 + 16 * 4,
    }

    # Conservative automatic choice: only palette formats when <= palette
    # capacity. CT16 is recommended for opaque/binary-alpha imagery; full
    # alpha is retained in RGBA32. This is a recommendation, not conversion.
    if not partial and colors <= 16:
        recommended = "PSMT4"
    elif not partial and colors <= 256:
        recommended = "PSMT8"
    elif not partial and alpha:
        recommended = "PSMCT16 (RGBA5551)"
    elif not partial:
        recommended = "PSMCT16 (opaque RGB555)"
    else:
        recommended = "RGBA32"

    return {
        "source": str(path), "width": w, "height": h,
        "gs_alloc_width_estimate": aw, "gs_alloc_height_estimate": ah,
        "unique_colors_capped_257": colors,
        "has_transparency": alpha, "has_partial_alpha": partial,
        "recommended_format": recommended,
        "rgba32_est_bytes": candidates["RGBA32"],
        "psmct16_est_bytes": candidates["PSMCT16"],
        "psmt8_est_bytes_including_256x32_clut": candidates["PSMT8"],
        "psmt4_est_bytes_including_16x32_clut": candidates["PSMT4"],
        "note": "Estimativa; sem swizzle GS, CLUT validada ou insercao ANM.",
    }


def analyze_tree(source: Path, output_csv: Path) -> list[dict]:
    images = [source] if source.is_file() else sorted(
        p for p in source.rglob("*") if p.is_file() and p.suffix.lower() in SUPPORTED
    )
    if not images:
        raise ValueError(f"Nenhuma imagem suportada encontrada em: {source}")
    rows = []
    for image_path in images:
        rows.append(analyze_image(image_path))
    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    return rows
