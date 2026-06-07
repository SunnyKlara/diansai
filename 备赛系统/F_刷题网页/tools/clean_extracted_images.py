"""
清理之前从 docx 抽取的图片（多为剪贴画/装饰图，非真正题图）。
现在每题已有官方 PDF（含原图），文字版的 assets/ 目录无需保留。
"""

import re
import shutil
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[3]
TARGET_ROOT = ROOT / "备赛系统" / "B_历年真题实战"

removed_assets = 0
modified_md = 0

for md in TARGET_ROOT.rglob("00_题目原件.md"):
    text = md.read_text(encoding="utf-8")
    new_text = re.sub(r"^!\[.*?\]\(assets/.*?\)\s*\n?", "", text, flags=re.MULTILINE)
    new_text = re.sub(r"\n{3,}", "\n\n", new_text)
    if new_text != text:
        md.write_text(new_text, encoding="utf-8")
        modified_md += 1
        print(f"  📝 清理图片引用 {md.relative_to(ROOT)}")

    assets = md.parent / "assets"
    if assets.exists() and assets.is_dir():
        shutil.rmtree(assets)
        removed_assets += 1
        print(f"  🗑️ 删除 {assets.relative_to(ROOT)}")

print(f"\n完成：清理 markdown {modified_md} 个，删除 assets 目录 {removed_assets} 个")
