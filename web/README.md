# 电赛刷题网页（离线版）

> 一个把历年真题审题叙事(narratives)转化成"可交互复习/刷题"的纯静态网页。
> 注:2026-07 仓库清理后,题目源码已归档;网页以 `data/narratives` 审题叙事 + 抽认卡/题库为主。
> 零依赖、零构建、双击 `start.bat` 即开即用。

## 快速开始

### 方式一（推荐）：双击 `start.bat`
- 自动用 Python 本地 HTTP 服务启动（端口 8765）
- 自动打开浏览器到 http://localhost:8765
- 关闭命令行窗口即停止服务

### 方式二：任意 HTTP 服务器
项目是纯静态文件，任何静态服务都行：
```bash
# Python 3
python -m http.server 8765

# Node (npx)
npx serve -l 8765 .
```
然后浏览器打开 http://localhost:8765

> ⚠️ 不要直接双击 `index.html` 用 `file://` 打开 —— `fetch` 加载 JSON 在
> 大部分浏览器下会被 CORS 拦截。

### 手机访问（局域网，Phase 1）

让手机和电脑连**同一个 WiFi**，然后双击 `start_mobile.bat`（或 `python web/tools/mobile_serve.py`）：

- 终端会自动探测本机内网 IP，打印手机可直接访问的地址，如 `http://192.168.x.x:8765/web/`
- 同时在终端画出**二维码**，手机扫码即开（需先 `pip install qrcode`，未安装则降级为纯文字地址）
- 手机连不上时，多半是 **Windows 防火墙**拦了：首次运行会弹"是否允许 Python 访问网络"，勾选"专用网络"放行即可

> 换网络 / 不在同一 WiFi 时（Phase 2）：可用内网穿透临时拿公网 HTTPS 地址，
> 例如 `cloudflared tunnel --url http://localhost:8765`，指向本机无需改代码。

## 目录结构

```
F_刷题网页/
├── index.html               # 单页应用入口
├── start.bat                # Windows 一键启动
├── assets/
│   ├── style.css            # 全部样式
│   └── app.js               # 全部逻辑（路由/状态/渲染）
├── data/
│   ├── questions.json       # 题库（80+ 题，覆盖 7 大方向）
│   ├── flashcards.json      # 知识卡片（关键技术点速记）
│   ├── problems.json        # 24 道真题索引
│   └── topics.json          # 方向/标签分类
└── README.md
```

## 功能一览

| 模块 | 路由 | 说明 |
|---|---|---|
| 首页仪表盘 | `#/` | 总进度、方向覆盖、错题数 |
| 刷题模式 | `#/quiz` | 顺序/随机/错题本，多种题型 |
| 知识卡片 | `#/cards` | Flashcard 翻面记忆 |
| 真题速查 | `#/problems` | 24 道真题 + 关键技术 + 路径 |
| 方向树 | `#/topics` | 按方向 / 标签浏览题目 |
| 错题本 | `#/wrong` | 自动收集答错题 |
| 收藏夹 | `#/star` | 收藏的难题 |
| 搜索 | `#/search` | 全文搜索 |

## 数据扩充

新增题目：在 `data/questions.json` 末尾追加一条记录，字段如下：

```json
{
  "id": "q-2024H-99",
  "year": 2024,
  "problem": "H",
  "topic": "控制",
  "subtopic": "PID 调参",
  "difficulty": 2,
  "type": "single",
  "question": "题面...",
  "options": ["选项A", "选项B", "选项C", "选项D"],
  "answer": [1],
  "explanation": "解析...",
  "tags": ["PID", "循迹"],
  "ref": "B_历年真题实战/2024/H_自动行驶小车/04_调试记录/独到经验总结.md"
}
```

字段说明：
- `id` 全局唯一，建议格式 `q-<年>-<题号>-<序号>` 或自定义前缀
- `type` 取值：`single`（单选）/ `multi`（多选）/ `judge`（判断）/ `fill`（填空）
- `answer`：
  - 单选：`[索引]`，如 `[1]`
  - 多选：`[索引1, 索引2]`
  - 判断：`[true]` 或 `[false]`
  - 填空：字符串数组 `["关键词1", "关键词2"]`，命中任一即对
- `difficulty` 1~3：1 入门 / 2 进阶 / 3 困难

## 进度同步

所有进度存储在浏览器 localStorage（key 前缀 `eddc:`）：
- `eddc:done` 已答题 ID 集合
- `eddc:wrong` 错题 ID 集合
- `eddc:star` 收藏 ID 集合
- `eddc:streak` 连续打卡天数

清空进度：浏览器 F12 → Application → Local Storage → 删除 `eddc:*`。

## 路线图

- [x] v1.0 题库 + 刷题 + 错题 + 卡片 + 真题索引
- [ ] v1.1 spaced repetition（艾宾浩斯曲线推送）
- [ ] v1.2 markdown 全文检索（fuse.js）
- [ ] v1.3 题目导入工具（从已有 .md 自动抽题）
