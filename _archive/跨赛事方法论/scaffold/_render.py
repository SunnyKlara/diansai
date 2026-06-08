#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
六件套脚手架渲染器（跨平台，UTF-8 安全）

被 init_repo.bat / init_repo.sh 调用：
    python _render.py --year 2024 --problem A_demo --mode hardware \
                      --root "备赛系统/B_历年真题实战" \
                      --templates "<scaffold>/_templates"
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path

# 模式 → 元数据
MODES = {
    "hardware": {
        "env_dir":      "02_硬件",
        "env_out":      "材料清单_BOM.md",
        "env_tpl":      "ENV_bom.md",
        "conf_out":     "config.h",
        "conf_tpl":     "config.h",
    },
    "software": {
        "env_dir":      "02_环境",
        "env_out":      "依赖清单.md",
        "env_tpl":      "ENV_dependencies.md",
        "conf_out":     "config.yaml",
        "conf_tpl":     "config.yaml",
    },
    "algo": {
        "env_dir":      "02_环境",
        "env_out":      "编译与评测说明.md",
        "env_tpl":      "ENV_compile_judge.md",
        "conf_out":     "template.cpp",
        "conf_tpl":     "template.cpp",
    },
}

# 输出文件名 → 模板名
RENDER_PLAN = [
    ("BRIEF_original.md", "00_题目原件.md"),
    ("BRIEF_analysis.md", "00_深度审题与方案论证.md"),
    ("CODE_README.md",    "01_代码/README.md"),
    ("REPORT_design.md",  "03_报告/设计报告.md"),
    ("LOG_checklist.md",  "04_调试记录/调试检查清单.md"),
    ("LOG_lessons.md",    "04_调试记录/独到经验总结.md"),
]


def render_one(src: Path, dst: Path, vars: dict[str, str]) -> None:
    if dst.exists():
        return
    if not src.exists():
        print(f"[WARN] template missing: {src}", file=sys.stderr)
        return
    text = src.read_text(encoding="utf-8")
    for k, v in vars.items():
        text = text.replace("{{" + k + "}}", v)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(text, encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--year",      required=True)
    ap.add_argument("--problem",   required=True)
    ap.add_argument("--mode",      default="hardware", choices=list(MODES))
    ap.add_argument("--root",      default="备赛系统/B_历年真题实战")
    ap.add_argument("--templates", required=True)
    args = ap.parse_args()

    meta = MODES[args.mode]
    base = Path(args.root) / args.year / args.problem
    tpl  = Path(args.templates)

    # 创建子目录
    for sub in [
        "01_代码/Core", "01_代码/Drivers", "01_代码/Algorithm",
        "03_报告", "04_调试记录", meta["env_dir"],
    ]:
        (base / sub).mkdir(parents=True, exist_ok=True)

    vars = {
        "YEAR":      args.year,
        "PROBLEM":   args.problem,
        "MODE":      args.mode,
        "ENV_DIR":   meta["env_dir"],
        "ENV_FILE":  meta["env_out"],
        "CONF_FILE": meta["conf_out"],
    }

    for src_name, rel_dst in RENDER_PLAN:
        render_one(tpl / src_name, base / rel_dst, vars)

    # 环境件 + 配置件（按模式选不同源/目标）
    render_one(tpl / meta["env_tpl"],  base / meta["env_dir"] / meta["env_out"], vars)
    render_one(tpl / meta["conf_tpl"], base / "01_代码" / meta["conf_out"],       vars)

    # 占位 .gitkeep
    for d in ("Core", "Drivers", "Algorithm"):
        kp = base / "01_代码" / d / ".gitkeep"
        if not kp.exists():
            kp.touch()

    print(f"[OK] Scaffold ready at {base}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
