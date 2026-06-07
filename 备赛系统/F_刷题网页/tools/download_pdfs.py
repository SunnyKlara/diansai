"""
v2: 改用 git 浅克隆 + ls-tree 拿到精确文件名（避开 GitHub API 限速）

工作流：
1. 浅克隆 https://github.com/MDLZCOOL/nwpu-nuedc 到临时目录（已存在则复用）
2. git ls-tree 列出 历年赛题/国赛/<2021-2025>/*.pdf 的真实文件名
3. 通过题号字母匹配 problems.json 登记的题
4. 用 git --output 拉取 blob 写到 赛题/原件/<年>/<题号>_<题名>.pdf
5. 更新 problems.json 的 pdf 字段

资源源：https://github.com/MDLZCOOL/nwpu-nuedc （MIT License，仅个人备赛用）
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[3]
PROBLEMS_JSON = ROOT / "备赛系统" / "F_刷题网页" / "data" / "problems.json"
PDF_DIR = ROOT / "赛题" / "原件"
REPO_URL = "https://github.com/MDLZCOOL/nwpu-nuedc.git"
CLONE_DIR = Path(tempfile.gettempdir()) / "nwpu-nuedc-clone"

YEARS = (2021, 2022, 2023, 2024, 2025)


def run(cmd, cwd=None, env=None, capture=True):
    proc = subprocess.run(
        cmd, cwd=cwd, env=env, capture_output=capture, text=False
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed: {' '.join(map(str, cmd))}\n"
            f"stderr: {(proc.stderr or b'').decode('utf-8', 'replace')}"
        )
    return proc.stdout


def ensure_clone():
    """浅克隆 + sparse checkout 只下需要的目录"""
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    if (CLONE_DIR / ".git").exists():
        print(f"复用已有克隆 {CLONE_DIR}")
        return
    if CLONE_DIR.exists():
        shutil.rmtree(CLONE_DIR, ignore_errors=True)
    CLONE_DIR.mkdir(parents=True, exist_ok=True)

    print(f"克隆 {REPO_URL} -> {CLONE_DIR}")
    run(["git", "init"], cwd=CLONE_DIR, env=env)
    run(["git", "remote", "add", "origin", REPO_URL], cwd=CLONE_DIR, env=env)
    run(["git", "config", "core.quotepath", "false"], cwd=CLONE_DIR, env=env)
    run(["git", "config", "core.sparseCheckout", "true"], cwd=CLONE_DIR, env=env)
    sparse_file = CLONE_DIR / ".git" / "info" / "sparse-checkout"
    sparse_file.parent.mkdir(parents=True, exist_ok=True)
    sparse_file.write_text(
        "\n".join([f"历年赛题/国赛/{y}/" for y in YEARS]) + "\n",
        encoding="utf-8",
    )
    run(["git", "fetch", "--depth=1", "origin", "main"], cwd=CLONE_DIR, env=env)
    run(["git", "checkout", "FETCH_HEAD"], cwd=CLONE_DIR, env=env)
    print("克隆完成")


def list_pdfs():
    """返回 {year: [filename, ...]}（filename 是 docx 真实文件名，含中文）"""
    result = {}
    for y in YEARS:
        d = CLONE_DIR / "历年赛题" / "国赛" / str(y)
        if not d.exists():
            print(f"  ⚠️ {y} 目录不存在: {d}")
            continue
        pdfs = sorted(p.name for p in d.iterdir() if p.suffix.lower() == ".pdf")
        result[y] = pdfs
    return result


def detect_problem_id(filename):
    m = re.match(r"^([A-Z])\s*题", filename)
    if m:
        return m.group(1)
    m = re.match(r"^([A-Z])\s*[_\-．\.]", filename)
    if m:
        return m.group(1)
    return None


def main():
    ensure_clone()

    problems = json.loads(PROBLEMS_JSON.read_text(encoding="utf-8"))
    by_key = {(p["year"], p["problem"]): p for p in problems}

    files_per_year = list_pdfs()

    ok, skip, no_target = 0, 0, 0
    for year in YEARS:
        files = files_per_year.get(year, [])
        if not files:
            continue
        print(f"\n=== {year} 年 ({len(files)} 个 PDF) ===")
        for fn in files:
            pid = detect_problem_id(fn)
            if not pid:
                print(f"  ⚠️ 无法识别题号: {fn}")
                continue
            p = by_key.get((year, pid))
            if not p:
                print(f"  ⊘ 未登记 {year} {pid}: {fn}")
                no_target += 1
                continue

            local_name = f"{pid}_{p['title']}.pdf"
            dest = PDF_DIR / str(year) / local_name
            src = CLONE_DIR / "历年赛题" / "国赛" / str(year) / fn
            dest.parent.mkdir(parents=True, exist_ok=True)

            if dest.exists() and dest.stat().st_size > 0:
                p["pdf"] = str(dest.relative_to(ROOT)).replace("\\", "/")
                print(f"  ⏭ {pid} 已存在 {dest.relative_to(ROOT)}")
                skip += 1
                continue

            shutil.copy2(src, dest)
            size = dest.stat().st_size
            p["pdf"] = str(dest.relative_to(ROOT)).replace("\\", "/")
            print(f"  ✅ {pid} -> {dest.relative_to(ROOT)} ({size / 1024:.1f} KB)")
            ok += 1

    PROBLEMS_JSON.write_text(
        json.dumps(problems, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    no_pdf = [(p["year"], p["problem"], p["title"]) for p in problems if not p.get("pdf")]
    print(f"\n========== 完成 ==========")
    print(f"新拷贝 {ok}，已存在跳过 {skip}，超出登记范围 {no_target}")
    print(f"PDF 保存到: {PDF_DIR.relative_to(ROOT)}/")
    if no_pdf:
        print(f"\n⚠️ 仍有 {len(no_pdf)} 题未配 PDF：")
        for y, pid, t in no_pdf:
            print(f"  - {y} {pid}: {t}")


if __name__ == "__main__":
    main()
