#!/usr/bin/env python3
"""
Easy Converter GUI - Unified PNG to C Array Converter

Two modes:
  - Convert Icon:    PNG -> 32x32 RGBA32 .c   (const unsigned char gItemIcon<Name>Tex[])
  - Convert Text:    PNG -> 128x16 IA4 .c      (const unsigned char g<Name>Tex[])

Select specific PNG files via tkinter file dialog.
Output .c files are saved next to each selected PNG.
"""

import sys
import tkinter as tk
from tkinter import filedialog, scrolledtext
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow"])
    from PIL import Image

SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
DEFAULT_ICON_DIR = PROJECT_DIR / "soh" / "mods" / "items" / "icons"
DEFAULT_TEXT_DIR = PROJECT_DIR / "soh" / "mods" / "items" / "names"


# ── Icon converter (RGBA32, 32x32) ──────────────────────────────────────────

def png_to_icon_c(png_path, output_path):
    img = Image.open(png_path).convert('RGBA')
    w, h = img.size
    resized = w != 32 or h != 32
    if resized:
        img = img.resize((32, 32), Image.Resampling.LANCZOS)
        w, h = 32, 32

    parts = png_path.stem.split('_')
    var_name = "gItemIcon" + ''.join(word.capitalize() for word in parts) + "Tex"

    pixels = img.load()
    lines = [
        f"// Generated from {png_path.name}",
        f"// Texture: {w}x{h} RGBA32 format",
        "",
        f"const unsigned char {var_name}[] = {{",
    ]
    byte_count = 0
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = pixels[x, y]
            row.extend([f"0x{r:02X}", f"0x{g:02X}", f"0x{b:02X}", f"0x{a:02X}"])
            byte_count += 4
        for i in range(0, len(row), 16):
            lines.append("    " + ", ".join(row[i:i+16]) + ",")

    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    lines += ["};", "", f"// Size: {byte_count} bytes ({w}x{h}, 32bpp)", ""]

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    return var_name, resized


# ── Text converter (IA4, 128x16) ────────────────────────────────────────────

def png_to_text_c(png_path, output_path):
    img = Image.open(png_path).convert('RGBA')
    w, h = img.size
    resized = w != 128 or h != 16
    if resized:
        img = img.resize((128, 16), Image.Resampling.LANCZOS)
        w, h = 128, 16

    parts = png_path.stem.split('_')
    var_name = "g" + ''.join(word.capitalize() for word in parts) + "Tex"

    pixels = img.load()
    lines = [
        f"// Generated from {png_path.name}",
        f"// Texture: {w}x{h} IA4 format",
        "",
        f"const unsigned char {var_name}[] = {{",
    ]
    for y in range(h):
        row_bytes = []
        for x in range(0, w, 2):
            r1, g1, b1, a1 = pixels[x, y]
            i1 = int(0.299 * r1 + 0.587 * g1 + 0.114 * b1)
            i1_3 = (i1 >> 5) & 0x7
            a1_1 = 1 if a1 >= 128 else 0

            r2, g2, b2, a2 = pixels[x + 1, y]
            i2 = int(0.299 * r2 + 0.587 * g2 + 0.114 * b2)
            i2_3 = (i2 >> 5) & 0x7
            a2_1 = 1 if a2 >= 128 else 0

            byte_val = ((i1_3 << 1 | a1_1) << 4) | (i2_3 << 1 | a2_1)
            row_bytes.append(f"0x{byte_val:02X}")
        lines.append("    " + ", ".join(row_bytes) + ",")

    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    total = w * h // 2
    lines += ["};", "", f"// Size: {total} bytes ({w}x{h}, 4bpp)", ""]

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    return var_name, resized


# ── GUI ──────────────────────────────────────────────────────────────────────

class ConverterApp:
    def __init__(self, root):
        self.root = root
        root.title("PNG to C Converter")
        root.geometry("720x480")
        root.resizable(True, True)

        # Buttons
        bf = tk.Frame(root)
        bf.pack(fill=tk.X, padx=10, pady=10)

        tk.Button(
            bf, text="Convert Icon  (32x32 RGBA32)",
            command=self.do_icon, width=28, height=2,
            bg="#4a90d9", fg="white", font=("Segoe UI", 11, "bold"),
        ).pack(side=tk.LEFT, padx=(0, 10))

        tk.Button(
            bf, text="Convert Text  (128x16 IA4)",
            command=self.do_text, width=28, height=2,
            bg="#d9534f", fg="white", font=("Segoe UI", 11, "bold"),
        ).pack(side=tk.LEFT)

        # Info
        tk.Label(
            root,
            text="Icon: gItemIcon<Name>Tex  |  Text: g<Name>Tex   —  .c saved next to PNG",
            font=("Segoe UI", 9), fg="#666",
        ).pack(padx=10)

        # Log
        self.log = scrolledtext.ScrolledText(
            root, wrap=tk.WORD, font=("Consolas", 10), height=18
        )
        self.log.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        self._print("Ready. Click a button to select PNG files.\n")

    # helpers
    def _print(self, msg):
        self.log.config(state=tk.NORMAL)
        self.log.insert(tk.END, msg + "\n")
        self.log.see(tk.END)
        self.log.config(state=tk.DISABLED)

    def _pick(self, default_dir):
        init = str(default_dir) if default_dir.exists() else str(PROJECT_DIR)
        files = filedialog.askopenfilenames(
            title="Select PNG files",
            initialdir=init,
            filetypes=[("PNG Images", "*.png"), ("All files", "*.*")],
        )
        return [Path(f) for f in files] if files else []

    # actions
    def do_icon(self):
        files = self._pick(DEFAULT_ICON_DIR)
        if not files:
            return
        self._print(f"\n--- ICON: {len(files)} file(s) ---")
        ok = 0
        for p in files:
            out = p.with_suffix('.c')
            try:
                var, resized = png_to_icon_c(p, out)
                kb = out.stat().st_size / 1024
                r = " (resized)" if resized else ""
                self._print(f"  [OK] {p.name} -> {out.name}  {kb:.1f}KB  {var}{r}")
                ok += 1
            except Exception as e:
                self._print(f"  [ERR] {p.name}: {e}")
        self._print(f"Done: {ok}/{len(files)}")

    def do_text(self):
        files = self._pick(DEFAULT_TEXT_DIR)
        if not files:
            return
        self._print(f"\n--- TEXT: {len(files)} file(s) ---")
        ok = 0
        for p in files:
            out = p.with_suffix('.c')
            try:
                var, resized = png_to_text_c(p, out)
                kb = out.stat().st_size / 1024
                r = " (resized)" if resized else ""
                self._print(f"  [OK] {p.name} -> {out.name}  {kb:.1f}KB  {var}{r}")
                ok += 1
            except Exception as e:
                self._print(f"  [ERR] {p.name}: {e}")
        self._print(f"Done: {ok}/{len(files)}")


if __name__ == "__main__":
    root = tk.Tk()
    ConverterApp(root)
    root.mainloop()
