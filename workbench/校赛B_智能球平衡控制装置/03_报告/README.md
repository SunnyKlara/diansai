# 设计报告（LaTeX）说明

本目录的设计报告以 **LaTeX（XeLaTeX + ctex）** 为权威源，可编译出 PDF，并转出可编辑的 Word。

## 文件

| 文件 | 说明 |
|---|---|
| `设计报告.tex` | 报告源文件（唯一真相源，改内容只动它）|
| `build.ps1` | 一键编译脚本：出 PDF + docx，并清理中间文件 |
| `设计报告.md` | 早期 Markdown 草稿（已被 .tex 取代，仅留作内容参照）|

## 编译

**一键**（推荐）：
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

**手动**：
```powershell
xelatex -interaction=nonstopmode 设计报告.tex   # 连编两遍
xelatex -interaction=nonstopmode 设计报告.tex
pandoc 设计报告.tex -o 设计报告.docx            # 转 Word（可选）
```

## 工具链（已安装）

- **MiKTeX**（XeLaTeX，按需自动补包）— winget 安装：`winget install MiKTeX.MiKTeX`
- **Pandoc**（LaTeX→docx）— winget 安装：`winget install JohnMacFarlane.Pandoc`

> 首次编译会在线自动下载 tikz/booktabs 等宏包，需联网、稍慢；之后走本地缓存秒级。

## 格式约定（已在 .tex 内实现）

- 章节编号对齐校赛范文：`一、` → `1、` → `（1）`（见导言区 `\ctexset`）。
- A4、1.5 倍行距、三线表（booktabs）、公式自动编号、图表自动编号与交叉引用。
- 系统框图/状态机用 **TikZ** 矢量绘制（PDF 中清晰；**Word 版不含这些图**，需手动插图）。
- 照片/原理图用占位框 `\figplaceholder`，到位后换成 `\includegraphics`。

## 待回填（随真机进展）

- 组别（封面）：本科组 / 高职组。
- 队员/学校/日期（封面）。
- 第二节误差预算实测值、$u_{hover}$ 标定值、PID 整定终值。
- 第四节三张数据表（测高/定高/动态跟踪）+ 实测照片与波形。

## Word 转换说明

pandoc 能正确转出文字、公式、三线表、参考文献，但 **TikZ 框图与代码框不会进 Word**。
若必须交 Word 且要含图：先编出 PDF，把 PDF 里的框图截图/导出后，手动插入 docx 对应位置。
优先以 **PDF 为最终提交版**。
