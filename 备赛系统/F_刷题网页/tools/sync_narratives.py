# -*- coding: utf-8 -*-
"""
narratives 同步工具

用法：
    python tools/sync_narratives.py            # 把 data/narratives/*.md 同步到 problem_briefs.json
    python tools/sync_narratives.py --extract  # 反向：把 JSON 里现有的 narrative 拆成 .md（首次迁移）
    python tools/sync_narratives.py --check    # 只检查 JSON 合法性，不写

设计目的：
    避免一次性写超长 narrative 字段时网络断开导致整篇丢失。
    每题 narrative 拆成独立 .md，写作时按章节 append，最后一键同步回 JSON。
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
NARR_DIR = DATA / "narratives"
JSON_PATH = DATA / "problem_briefs.json"


def load_json():
    return json.loads(JSON_PATH.read_text(encoding="utf-8"))


def save_json(d):
    JSON_PATH.write_text(
        json.dumps(d, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def cmd_check():
    d = load_json()
    print(f"[check] {JSON_PATH.name}: ok, {len(d) - 1} 题")
    for k, v in d.items():
        if k.startswith("_"):
            continue
        nlen = len(v.get("narrative", ""))
        print(f"  {k}: narrative {nlen} 字")


def cmd_extract():
    """把 JSON 里现有 narrative 拆成 .md，作为后续编辑源"""
    d = load_json()
    NARR_DIR.mkdir(parents=True, exist_ok=True)
    for k, v in d.items():
        if k.startswith("_"):
            continue
        narr = v.get("narrative", "")
        if not narr.strip():
            continue
        path = NARR_DIR / f"{k}.md"
        if path.exists():
            print(f"  [skip] {path.name}（已存在，避免覆盖）")
            continue
        path.write_text(narr, encoding="utf-8")
        print(f"  [extract] {path.name} ← {len(narr)} 字")


def cmd_sync():
    """把 narratives/*.md 内容写回 JSON。键不存在时自动创建空骨架。"""
    d = load_json()
    if not NARR_DIR.exists():
        print(f"[warn] {NARR_DIR} 不存在")
        return
    changed = 0
    created = 0
    for md in sorted(NARR_DIR.glob("*.md")):
        key = md.stem
        if key not in d:
            d[key] = {
                "oneline": "",
                "narrative": "",
                "why": "",
                "tasks": [],
                "metrics": [],
                "traps": [],
                "checklist": [],
            }
            created += 1
            print(f"  [new]  {key}: 创建空条目")
        new = md.read_text(encoding="utf-8")
        old = d[key].get("narrative", "")
        if new != old:
            d[key]["narrative"] = new
            changed += 1
            print(f"  [sync] {key}: {len(old)} → {len(new)} 字")
        else:
            print(f"  [same] {key}: {len(new)} 字")
    if changed or created:
        save_json(d)
        print(f"\n已更新 {changed} 题（新建 {created} 条）到 {JSON_PATH.name}")
    else:
        print("\n无变化")


def main():
    args = sys.argv[1:]
    if "--check" in args:
        cmd_check()
    elif "--extract" in args:
        cmd_extract()
    else:
        cmd_sync()


if __name__ == "__main__":
    main()
