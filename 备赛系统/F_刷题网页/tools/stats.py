# -*- coding: utf-8 -*-
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NARR = ROOT / "data" / "narratives"

total = 0
files = sorted(NARR.glob("*.md"))
print(f"{'文件':<14} {'字数':>8}")
print("-" * 26)
for p in files:
    n = len(p.read_text(encoding="utf-8"))
    total += n
    print(f"{p.name:<14} {n:>8}")
print("-" * 26)
print(f"{'合计':<14} {total:>8}")
print(f"题数: {len(files)}")
