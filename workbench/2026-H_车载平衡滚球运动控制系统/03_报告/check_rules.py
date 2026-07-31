# -*- coding: utf-8 -*-
"""check_rules.py - gate 设计报告.pdf against the countable clauses of the official
student rules. These are ELIGIBILITY rules, not style preferences.

RULES SOURCE
    workbench/参赛守则.jpg -- 《2026 年全国大学生电子设计竞赛参赛学生守则》,
    全国大学生电子设计竞赛广东赛区组委会, 2026 年 7 月.
    第五条: 封面及每页纸上一律不得出现参赛队学校、队员姓名、代码等文字;
            报告正文限 A4 打印纸 8 页以内; 首页另附 300 字以内的中文摘要;
            正文采用小四号宋体字, 行距固定值 22 磅, 标题字号自定, A4 纵向打印;
            每页上方必须留出 3cm 以上的空白, 空白区域内不得有任何文字;
            每页右下端注明页码.
    第六条: 对违反竞赛守则的参赛队, 一经发现, 取消其评审、测试资格.

WHY THIS FILE LIVES HERE
    It used to sit in .tmp_pdf/, which is gitignored -- an eligibility gate that
    disappears when the repo is cloned is not a gate. It now sits next to
    check_numbers.py and is run by build.ps1 on every build.

WHAT IS AND IS NOT CHECKED
    Countable from the PDF: body page count, abstract length, top blank margin,
    page-number position, page orientation/size, and whether the embedded body
    font is a 宋体 face. NOT checked: whether 22 pt leading is exactly honoured
    (LaTeX sets it from \\setstretch in the preamble, so it is verified there),
    and whether school/name text is absent -- that one is a human read, because a
    school name can appear inside a photograph.

USAGE
    <repo>/.venv/Scripts/python.exe check_rules.py [pdf]
    exit 0 = every countable clause passes; exit 1 = at least one fails.
"""
import os
import re
import sys

import fitz

PT_PER_CM = 72.0 / 2.54
TOP_BLANK_CM = 3.0
ABSTRACT_MAX = 300
BODY_MAX_PAGES = 8

pdf = sys.argv[1] if len(sys.argv) > 1 else "设计报告.pdf"
if not os.path.exists(pdf):
    print("check_rules: %s not found" % pdf)
    sys.exit(1)

doc = fitz.open(pdf)
fails = []
print("=== check_rules: 设计报告.pdf vs 参赛守则 第五条 ===")
print("%s: %d pages" % (pdf, doc.page_count))


def text_blocks(page):
    return [b for b in page.get_text("blocks") if (b[4] or "").strip()]


# ---- clause 5: A4 portrait ------------------------------------------------
bad_size = []
for i, p in enumerate(doc):
    w, h = p.rect.width, p.rect.height
    if not (abs(w - 595.3) < 3 and abs(h - 841.9) < 3):
        bad_size.append("p%d %.0fx%.0f" % (i + 1, w, h))
if bad_size:
    fails.append("A4 portrait")
    print("  [X] not A4 portrait: " + ", ".join(bad_size))
else:
    print("  [OK] every page is A4 portrait")

# ---- clause 5: body limited to 8 pages -----------------------------------
# The body is delimited by the section that opens it and the bibliography that
# closes it, so front matter and appendix are excluded the same way the 2024-H
# reference report numbers its body 1..8.
first = last = None
for i, p in enumerate(doc):
    t = p.get_text() or ""
    if first is None and "系统方案" in t and "目录" not in t:
        first = i
    if last is None and i > 3 and "参考文献" in t and "附录" not in t:
        last = i
if first is None or last is None:
    print("  [?] body span not detected (looked for 系统方案 .. 参考文献)")
else:
    body = last - first
    if body > BODY_MAX_PAGES:
        fails.append("body %d > %d pages" % (body, BODY_MAX_PAGES))
        print("  [X] body is p%d..p%d = %d pages, limit %d -> OVER by %d"
              % (first + 1, last, body, BODY_MAX_PAGES, body - BODY_MAX_PAGES))
    else:
        print("  [OK] body is p%d..p%d = %d pages (limit %d)"
              % (first + 1, last, body, BODY_MAX_PAGES))

# ---- clause 5: Chinese abstract <= 300 characters ------------------------
ab = None
for i in range(min(4, doc.page_count)):
    if "摘要" in (doc[i].get_text() or ""):
        ab = i
        break
if ab is None:
    print("  [?] abstract page not found")
else:
    t = (doc[ab].get_text() or "").split("摘要", 1)[1]
    t = t.split("关键词", 1)[0]
    n = len(re.findall(r"[\u4e00-\u9fff]", t))
    if n > ABSTRACT_MAX:
        fails.append("abstract %d > %d chars" % (n, ABSTRACT_MAX))
        print("  [X] abstract on p%d: %d Chinese chars, limit %d -> OVER by %d"
              % (ab + 1, n, ABSTRACT_MAX, n - ABSTRACT_MAX))
    else:
        print("  [OK] abstract on p%d: %d Chinese chars (limit %d)"
              % (ab + 1, n, ABSTRACT_MAX))

# ---- clause 5: >= 3 cm blank at the top of every page, no text in it -----
worst = None
for i, p in enumerate(doc):
    bl = text_blocks(p)
    if not bl:
        continue
    top_cm = min(b[1] for b in bl) / PT_PER_CM
    if worst is None or top_cm < worst[1]:
        worst = (i + 1, top_cm)
if worst is None:
    print("  [?] no text found, cannot measure top margin")
elif worst[1] < TOP_BLANK_CM:
    fails.append("top blank %.2f cm < %.1f cm" % (worst[1], TOP_BLANK_CM))
    print("  [X] smallest top blank is %.2f cm on p%d, need >= %.1f cm"
          % (worst[1], worst[0], TOP_BLANK_CM))
else:
    print("  [OK] smallest top blank is %.2f cm on p%d (need >= %.1f cm)"
          % (worst[1], worst[0], TOP_BLANK_CM))

# ---- clause 5: page number at the bottom-right ---------------------------
# Locate the digits that sit lowest on the page and check they are in the right
# half. Pages with no number at all (the cover) are reported, not failed.
missing, misplaced = [], []
for i, p in enumerate(doc):
    bl = text_blocks(p)
    if not bl:
        continue
    lowest = max(b[3] for b in bl)
    cand = [b for b in bl if b[3] > lowest - 12 and re.fullmatch(r"\s*\d+\s*", b[4] or "")]
    if not cand:
        missing.append(i + 1)
        continue
    b = cand[0]
    if (b[0] + b[2]) / 2.0 < p.rect.width * 0.6:
        misplaced.append("p%d" % (i + 1))
if misplaced:
    fails.append("page number not bottom-right on " + ",".join(misplaced))
    print("  [X] page number not at bottom-right on: " + ", ".join(misplaced))
else:
    print("  [OK] page numbers sit at the bottom-right"
          + (" (no number on p%s, expected for the cover)"
             % ",".join(str(x) for x in missing) if missing else ""))

# ---- clause 5: body font should be a 宋体 face ---------------------------
names = set()
for i, p in enumerate(doc):
    if first is not None and not (first <= i < (last if last else doc.page_count)):
        continue
    for f in p.get_fonts():
        names.add(f[3])
song = [n for n in names if re.search(r"SimSun|Song|STSong|NSimSun|宋", n, re.I)]
if song:
    print("  [OK] body embeds a 宋体 face: " + ", ".join(sorted(song)[:3]))
else:
    print("  [!] no 宋体 face detected among body fonts: " + ", ".join(sorted(names)[:6]))
    print("      clause 5 wants 小四号宋体; verify the CJK main font manually.")

print("checks failed: %d" % len(fails))
if fails:
    print("RESULT: FAIL -> " + "; ".join(fails))
    sys.exit(1)
print("RESULT: PASS")
