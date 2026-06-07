"""
v6: 含图片抽取
- 顺序遍历 docx body：段落 / 表格 / 图片 三种块
- 图片按出现顺序写到对应题目的 assets/ 目录
- markdown 里 ![](assets/imgN.png) 插入
- 评分表格保留为 markdown 表格
- 参赛注意事项、电路文字标注、单格图标签自动剔除
"""

import json
import re
import sys
import zipfile
import shutil
from pathlib import Path
from xml.etree import ElementTree as ET

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[3]
DOCX = ROOT / "赛题" / "000_2017-2025_全国大学生电子设计竞赛真题汇总(1).docx"
PROBLEMS_JSON = ROOT / "备赛系统" / "F_刷题网页" / "data" / "problems.json"

NS = {
    "w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
    "pic": "http://schemas.openxmlformats.org/drawingml/2006/picture",
}
W = "{" + NS["w"] + "}"
R = "{" + NS["r"] + "}"


def load_relationships(z):
    """rId -> Target（如 rId7 -> media/image1.png）"""
    rels = {}
    rels_xml = z.read("word/_rels/document.xml.rels")
    rels_root = ET.fromstring(rels_xml)
    for rel in rels_root:
        rid = rel.get("Id")
        target = rel.get("Target")
        rels[rid] = target
    return rels


def para_text(p):
    return "".join((t.text or "") for t in p.iter(W + "t")).strip()


def para_image_rids(p):
    """返回段落里出现的所有 r:embed 引用 ID（按顺序）"""
    rids = []
    # 命名空间通用：先用全 element 搜索 blip
    for blip in p.iter("{" + NS["a"] + "}blip"):
        rid = blip.get(R + "embed") or blip.get("{" + NS["r"] + "}embed")
        if rid:
            rids.append(rid)
    return rids


def table_to_markdown(tbl):
    rows = []
    for tr in tbl.findall(W + "tr"):
        cells = []
        for tc in tr.findall(W + "tc"):
            text = " ".join(
                "".join((t.text or "") for t in p.iter(W + "t")).strip()
                for p in tc.findall(W + "p")
            ).strip()
            text = re.sub(r"\s+", " ", text)
            cells.append(text or " ")
        rows.append(cells)
    if not rows:
        return ""
    ncol = max(len(r) for r in rows)
    rows = [r + [" "] * (ncol - len(r)) for r in rows]
    md = []
    md.append("| " + " | ".join(rows[0]) + " |")
    md.append("|" + "|".join([" --- "] * ncol) + "|")
    for r in rows[1:]:
        md.append("| " + " | ".join(r) + " |")
    return "\n".join(md)


def docx_blocks(path: Path):
    """按顺序产出 (kind, payload):
    - ('p', text, rids)         段落+图片引用列表
    - ('tbl', md_string)        表格
    """
    z = zipfile.ZipFile(path)
    rels = load_relationships(z)
    root = ET.fromstring(z.read("word/document.xml"))
    body = root.find(W + "body")
    if body is None:
        return [], rels, z
    blocks = []
    for child in body:
        if child.tag == W + "p":
            text = para_text(child)
            rids = para_image_rids(child)
            if text or rids:
                blocks.append(("p", text, rids))
        elif child.tag == W + "tbl":
            md = table_to_markdown(child)
            if md:
                blocks.append(("tbl", md, []))
    return blocks, rels, z


# ============ 题目识别 ============
TITLE_RE_BRACKET = re.compile(r"^(.+?)[（(]\s*([A-Z])\s*题[)）]")
TITLE_RE_PREFIX = re.compile(r"^([A-Z])\s*题\s*[：:\-]?\s*(.+)$")
YEAR_RE = re.compile(r"(20\d{2})\s*年")

NOTICE_HEAD_RE = re.compile(r"参赛注意事项|参赛队须知|参赛说明")
NOTICE_END_RE = re.compile(r"^(评分标准|评 分 标 准|一、任务|二、要求|三、说明)")

SCHEMATIC_TOKENS = re.compile(
    r"^(Id|Iq|U\d?d?|UAB|U[a-z]\d?|R[Ll]?\d?|"
    r"变流器\d*|连接单元|直流电源|交流电源|示波器|电流表|电压表|开关电源|"
    r"DC-AC|AC-DC|AC-AC|DC-DC|"
    r"[a-zA-Z]\d?|"
    r"[↓→←↑]+|"
    r"\d+(\.\d+)?(Hz|V|A|W|Ω|μF|mH|kHz|MHz|ms|μs|kg|cm|mm|m|s)$|"
    r"图\s*\d+(-\d+)?|"
    r")$"
)


def is_schematic_label(text):
    if len(text) > 18:
        return False
    if SCHEMATIC_TOKENS.match(text):
        return True
    if len(text) <= 18 and re.match(r"^[\sA-Za-z0-9→←↑↓\.\-_/]+$", text):
        return True
    if re.match(r"^[\s→←↑↓\.\-_/、，]+$", text):
        return True
    return False


SCHEM_KEYWORDS = [
    "变流器", "DC-AC", "AC-DC", "DC-DC", "AC-AC",
    "连接单元", "直流电源", "交流电源", "示波器", "电流表", "电压表",
    "受试电路", "被测电路", "信号源", "放大器", "滤波器", "整流器",
    "逆变器", "辅助电源",
]


def strip_schematic_prefix(text):
    if len(text) < 30:
        return text
    m = re.search(
        r"(设计并|设计制作|要求设计|本题|按图|参照|参考|根据|首先|"
        r"针对|题目|本装置|装置应|电路应)",
        text,
    )
    if not m:
        return text
    prefix = text[: m.start()]
    rest = text[m.start():]
    if not (5 <= len(prefix) <= 200):
        return text
    ascii_chars = sum(1 for c in prefix if (c.isascii() and not c.isspace()) or c in "→←↑↓")
    if ascii_chars / max(len(prefix), 1) < 0.3:
        return text
    has_kw = any(kw in prefix for kw in SCHEM_KEYWORDS)
    has_arrow = any(c in prefix for c in "→←↑↓")
    if not (has_kw or has_arrow):
        return text
    return rest


def is_pure_schematic_text(text):
    if len(text) > 60:
        return False
    if re.match(r"^图\s*\d", text):
        return False
    if re.search(r"[，。；：、？！,;\?!（）()：:]", text):
        return False
    has_kw = any(kw in text for kw in SCHEM_KEYWORDS)
    has_arrow = any(c in text for c in "→←↑↓↔")
    if not (has_kw or has_arrow):
        return False
    return True


def is_label_table(md_table):
    rows = [
        r for r in md_table.split("\n")
        if r.startswith("|") and not re.match(r"^\|[\s\-:|]+\|$", r)
    ]
    if len(rows) > 3:
        return False
    cells = []
    for r in rows:
        cells.extend(c.strip() for c in r.strip("|").split("|"))
    cells = [c for c in cells if c]
    if not cells:
        return True
    if all(len(c) <= 8 for c in cells) and len(cells) <= 6:
        return True
    return False


def extract_title(text):
    if not text or len(text) > 80:
        return None
    m = TITLE_RE_BRACKET.match(text)
    if m:
        title = m.group(1).strip()
        pid = m.group(2)
        if title and 2 <= len(title) <= 50:
            return pid, title
    m = TITLE_RE_PREFIX.match(text)
    if m:
        pid = m.group(1)
        title = m.group(2).strip()
        title = re.sub(r"[（(].{1,8}[)）]\s*$", "", title).strip()
        if title and 2 <= len(title) <= 50:
            return pid, title
    return None


def main():
    if not DOCX.exists():
        print(f"docx not found: {DOCX}", file=sys.stderr)
        sys.exit(1)

    problems = json.loads(PROBLEMS_JSON.read_text(encoding="utf-8"))

    blocks, rels, z = docx_blocks(DOCX)
    print(f"读取 {len(blocks)} 个块（段落+表格）")

    # 标记年份
    block_year = [None] * len(blocks)
    current_year = None
    for i, blk in enumerate(blocks):
        kind = blk[0]
        if kind == "p":
            text = blk[1]
            if "电子设计竞赛" in text or "TI 杯" in text:
                m = YEAR_RE.search(text)
                if m:
                    y = int(m.group(1))
                    if 2017 <= y <= 2025:
                        current_year = y
        block_year[i] = current_year

    # 找题目起点
    starts = {}
    for i, blk in enumerate(blocks):
        if blk[0] != "p":
            continue
        text = blk[1]
        result = extract_title(text)
        if result and block_year[i]:
            pid, title = result
            key = (block_year[i], pid)
            if key not in starts:
                starts[key] = (i, title)

    print(f"识别到 {len(starts)} 道题")
    sorted_starts = sorted(
        [(idx, y, pid, title) for (y, pid), (idx, title) in starts.items()]
    )
    ranges = []
    for k, (idx, y, pid, title) in enumerate(sorted_starts):
        end = sorted_starts[k + 1][0] if k + 1 < len(sorted_starts) else len(blocks)
        ranges.append((idx, end, y, pid, title))

    # 写入
    by_key = {(p["year"], p["problem"]): p for p in problems}
    FORCE_REGENERATE = True
    written, skipped = [], []

    for start_idx, end_idx, y, pid, title in ranges:
        p = by_key.get((y, pid))
        if not p:
            continue
        target_dir = ROOT / p["path"].rstrip("/")
        target_dir.mkdir(parents=True, exist_ok=True)
        target = target_dir / "00_题目原件.md"
        assets_dir = target_dir / "assets"

        if target.exists() and not FORCE_REGENERATE:
            skipped.append((y, pid))
            p["origin"] = str(target.relative_to(ROOT)).replace("\\", "/")
            continue

        # 先清理旧 assets
        if assets_dir.exists():
            shutil.rmtree(assets_dir)
        assets_dir.mkdir(parents=True, exist_ok=True)

        # 处理本题块
        out_lines = []
        out_lines.append(f"# {y} 年 {pid} 题：{p['title']}")
        out_lines.append("")
        out_lines.append("> 摘自《2017-2025 全国大学生电子设计竞赛真题汇总》")
        out_lines.append("")

        in_notice = False
        seen_total_score = False
        schematic_buffer = []
        img_counter = 0

        def flush_schematic():
            nonlocal schematic_buffer
            schematic_buffer = []

        def add_image(rid):
            """把 rid 对应的图片复制到 assets/，返回 markdown 引用"""
            nonlocal img_counter
            target_rel = rels.get(rid)
            if not target_rel:
                return None
            # target 形如 'media/image123.png'
            src_in_zip = "word/" + target_rel
            if src_in_zip not in z.namelist():
                return None
            ext = Path(target_rel).suffix or ".png"
            img_counter += 1
            fname = f"img{img_counter}{ext}"
            data = z.read(src_in_zip)
            (assets_dir / fname).write_bytes(data)
            return f"![图 {img_counter}](assets/{fname})"

        for i in range(start_idx, end_idx):
            blk = blocks[i]
            kind = blk[0]

            if kind == "p":
                text = blk[1]
                rids = blk[2]

                # 跳过年份页眉
                if YEAR_RE.search(text) and ("电子设计竞赛" in text or "TI 杯" in text) and len(text) < 60:
                    # 但仍可能含图片，保留图片
                    for rid in rids:
                        ref = add_image(rid)
                        if ref:
                            out_lines.append(ref)
                            out_lines.append("")
                    continue

                if NOTICE_HEAD_RE.search(text):
                    in_notice = True
                    flush_schematic()
                    continue
                if in_notice:
                    if NOTICE_END_RE.match(text):
                        in_notice = False
                    elif re.match(r"^[一二三四五六七八九]\s*[、.]\s*", text):
                        in_notice = False
                    else:
                        continue

                if seen_total_score:
                    if re.match(r"^[一二三四五六七八九]\s*[、.]\s*", text) and len(text) < 30:
                        seen_total_score = False
                    else:
                        if rids:
                            # 评分后基本不会再有图，但保险起见也插入
                            for rid in rids:
                                ref = add_image(rid)
                                if ref:
                                    out_lines.append(ref)
                                    out_lines.append("")
                        continue

                # 段落里有图：先把图片插入
                images_md = []
                for rid in rids:
                    ref = add_image(rid)
                    if ref:
                        images_md.append(ref)

                # 文字处理
                if text:
                    if is_schematic_label(text):
                        schematic_buffer.append(text)
                    elif is_pure_schematic_text(text):
                        flush_schematic()
                    else:
                        flush_schematic()
                        cleaned = strip_schematic_prefix(text)
                        out_lines.append(cleaned)
                        out_lines.append("")

                # 图片放到段落正文之后
                for img_ref in images_md:
                    out_lines.append(img_ref)
                    out_lines.append("")

            elif kind == "tbl":
                flush_schematic()
                if in_notice or seen_total_score:
                    continue
                if is_label_table(blk[1]):
                    continue
                out_lines.append(blk[1])
                out_lines.append("")
                if re.search(r"总\s*分|合\s*计.*\d{2,3}", blk[1]):
                    seen_total_score = True

        # 如果什么图都没抽出来，就清掉空 assets 目录
        if img_counter == 0:
            shutil.rmtree(assets_dir, ignore_errors=True)

        target.write_text("\n".join(out_lines), encoding="utf-8")
        p["origin"] = str(target.relative_to(ROOT)).replace("\\", "/")
        written.append((y, pid, img_counter))

    PROBLEMS_JSON.write_text(
        json.dumps(problems, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    print(f"\n========== 完成 ==========")
    total_imgs = sum(c for _, _, c in written)
    print(f"重新生成: {len(written)} 题，共抽出图片 {total_imgs} 张")
    for y, pid, cnt in written:
        if cnt > 0:
            print(f"  + {y} {pid}: {cnt} 张图")
        else:
            print(f"  ⊝ {y} {pid}: 无图（可能纯文字题）")


if __name__ == "__main__":
    main()
