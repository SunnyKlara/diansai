#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sync_narratives.py —— 把 narratives/<year-problem>.md（人工编辑的源）
同步进 problem_briefs.json 的对应条目的 narrative 字段（网页实际读取的文件）。

设计：narratives/*.md 是"单一事实源"（人读友好、可 diff）；
problem_briefs.json 是网页消费的聚合文件，narrative 字段由本脚本从 .md 灌入，
其余结构化字段(oneline/why/tasks/metrics/traps/checklist)在 json 内维护。

用法：
  python sync_narratives.py            # 同步全部 narratives/*.md
  python sync_narratives.py 2026-B     # 只同步指定题
运行后网页(已设 no-store)刷新即可见。
"""
import json, io, sys, glob, os
sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
BRIEFS = os.path.join(DATA, "problem_briefs.json")
NARR_DIR = os.path.join(DATA, "narratives")

only = sys.argv[1] if len(sys.argv) > 1 else None

briefs = json.load(io.open(BRIEFS, encoding="utf-8"))

files = sorted(glob.glob(os.path.join(NARR_DIR, "*.md")))
synced, skipped = [], []
for f in files:
    key = os.path.splitext(os.path.basename(f))[0]   # 2026-B
    if only and key != only:
        continue
    text = io.open(f, encoding="utf-8").read()
    if key not in briefs:
        # 没有结构化条目就建一个最小壳（narrative 为主）
        briefs[key] = {"oneline": "", "narrative": "", "why": "",
                       "tasks": [], "metrics": [], "traps": [], "checklist": []}
        skipped.append(key + "(新建壳)")
    briefs[key]["narrative"] = text
    synced.append(f"{key} ({len(text)}字)")

json.dump(briefs, io.open(BRIEFS, "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)

print("已同步:", ", ".join(synced) if synced else "(无)")
if skipped:
    print("注意(新建条目壳，结构化字段需在json补全):", ", ".join(skipped))
print("problem_briefs 条目数:", len([k for k in briefs if k != "_doc"]))
