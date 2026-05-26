"""
run_all_validation.py
=====================

一键回归测试：扫描 备赛系统/B_历年真题实战/ 下所有 algo_reference.py 并跑一遍。

用途：
  1. 修改了某题的算法 → 跑一次确认其他题没受影响
  2. CI / 提交前自检
  3. 检查所有题目算法层的健康度

退出码：
  0 = 全部通过
  N = 失败题目数

用法：
  python run_all_validation.py
  python run_all_validation.py --verbose       # 显示每题完整输出
  python run_all_validation.py --filter 2024   # 只跑包含 "2024" 的题目
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def find_repo_root() -> Path:
    """从脚本位置往上找 .git 或 .kiro 标记的仓库根。"""
    p = Path(__file__).resolve()
    for _ in range(10):
        p = p.parent
        if (p / ".git").exists() or (p / ".kiro").exists():
            return p
    return Path(__file__).resolve().parent.parent.parent.parent.parent


def discover_algo_refs(repo_root: Path) -> list:
    """扫描所有 algo_reference.py 和 snippets 下的 _example。"""
    base = repo_root / "备赛系统" / "B_历年真题实战"
    scripts = []
    if base.exists():
        scripts.extend(sorted(base.glob("**/01_代码/tests/algo_reference.py")))

    # 加 snippets 下的所有 .py（每个都有 _example）
    snippets = repo_root / "备赛系统" / "C_通用能力" / "算法层PC验证方法论" / "snippets"
    if snippets.exists():
        scripts.extend(sorted(snippets.glob("*.py")))

    return scripts


def run_one(script: Path, verbose: bool, repo_root: Path) -> tuple:
    """跑单个 algo_reference.py，返回 (ok, elapsed_s, summary)。"""
    rel = script.relative_to(repo_root)
    parts = rel.parts
    # 真题：备赛系统/B_历年真题实战/<年份>/<题目>/...
    # snippet：备赛系统/C_通用能力/算法层PC验证方法论/snippets/<文件>.py
    if len(parts) >= 4 and parts[1] == "B_历年真题实战":
        title = "/".join(parts[2:4])
    else:
        title = "snippet/" + script.stem

    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"

    t0 = time.time()
    try:
        result = subprocess.run(
            [sys.executable, str(script)],
            capture_output=True, text=True, encoding="utf-8",
            timeout=60, env=env,
        )
        elapsed = time.time() - t0
        ok = result.returncode == 0
        # 检查输出里是否有失败标记
        if ok and ("✗" in result.stdout or "FAIL" in result.stdout.upper()):
            ok = False

        if verbose:
            print(result.stdout)

        # 提取最后一行有意义的结论
        last_lines = [ln for ln in result.stdout.splitlines() if ln.strip()]
        summary = last_lines[-3] if len(last_lines) >= 3 else "(无输出)"
        return (ok, elapsed, title, summary, result.stderr if not ok else "")
    except subprocess.TimeoutExpired:
        return (False, time.time() - t0, title, "TIMEOUT", "")
    except Exception as e:
        return (False, time.time() - t0, title, f"EXCEPTION: {e}", "")


def main():
    ap = argparse.ArgumentParser(description="一键回归全部真题的算法层金标准")
    ap.add_argument("--verbose", action="store_true", help="显示每题完整输出")
    ap.add_argument("--filter", default="", help="只跑路径中包含此字符串的题目")
    args = ap.parse_args()

    repo_root = find_repo_root()
    scripts = discover_algo_refs(repo_root)
    if args.filter:
        scripts = [s for s in scripts if args.filter in str(s)]

    if not scripts:
        print(f"没找到 algo_reference.py。仓库根：{repo_root}")
        return 1

    print("=" * 78)
    print(f" 算法层金标准回归测试（{len(scripts)} 题）")
    print(f" 仓库根：{repo_root}")
    print("=" * 78)
    print()

    results = []
    total_t0 = time.time()
    for idx, script in enumerate(scripts, 1):
        rel = script.relative_to(repo_root)
        print(f"  [{idx}/{len(scripts)}] {rel}", end=" ... ", flush=True)
        ok, elapsed, title, summary, err = run_one(script, args.verbose, repo_root)
        results.append((ok, elapsed, title, summary, err))
        flag = "✓ PASS" if ok else "✗ FAIL"
        print(f"{flag} ({elapsed:.2f}s)")
        if not ok and not args.verbose:
            print(f"        last: {summary}")
            if err:
                print(f"        err : {err.splitlines()[-1] if err.splitlines() else err}")

    total_elapsed = time.time() - total_t0
    print()
    print("=" * 78)
    pass_n = sum(1 for r in results if r[0])
    fail_n = len(results) - pass_n
    flag = "✓ ALL PASSED" if fail_n == 0 else f"✗ {fail_n} FAILED"
    print(f" {flag}  ({pass_n}/{len(results)})  total {total_elapsed:.2f}s")
    print("=" * 78)
    print()

    # 打印失败详情
    for ok, _, title, summary, err in results:
        if not ok:
            print(f"  ✗ {title}")
            print(f"      {summary}")
            if err:
                print(f"      stderr: {err.strip()[:200]}")

    return fail_n


if __name__ == "__main__":
    sys.exit(main())
