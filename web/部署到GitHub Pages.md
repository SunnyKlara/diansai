# 部署到 GitHub Pages（公网域名访问）

> 目标：让任何人用一个网址就能在手机/电脑上刷题，无需你开机、无需同一 WiFi。
> 仓库：`https://github.com/SunnyKlara/diansai` · 分支 `main`
> 部署后地址：**https://sunnyklara.github.io/diansai/**（根入口自动跳转到 `/web/`）

---

## 为什么能直接部署（已做的代码改造）

GitHub 项目站点的网址是**子路径** `https://用户名.github.io/仓库名/`，而本网页早期把真题
markdown / PDF 写成根绝对路径 `/cases/...`、`/赛题/...`，子路径下会全部 404。

已在 `web/assets/app.js` 顶部引入 `REPO_BASE` + `repoUrl()`：自动识别站点挂在
根（本机/自定义域名）还是子路径（`/diansai`），所有资源路径动态拼接。**本机访问行为不变，
公网子路径也能跑**，二者已实测通过。

配套改动：
- 根目录新增空文件 `.nojekyll` —— 关闭 GitHub 默认的 Jekyll 构建，否则它会**忽略所有
  以 `_` 开头的文件/目录**（仓库里有 `_archive/`、`cases/_进度看板.md` 等），导致 404。
- `web/index.html` 的 `?v=` 版本号已 bump，避免老用户拿到旧缓存。

---

## A. 最快路线：开 GitHub Pages（5 分钟，免费，无需买域名）

### 第 1 步 · 把改动推到 GitHub
在仓库根目录执行（或让我代为提交）：

```bash
git add .
git commit -m "feat(web): 适配 GitHub Pages 子路径托管 + 手机访问"
git push origin main
```

### 第 2 步 · 确认仓库是 Public
GitHub Pages 免费版只对 **public 仓库**生效（private 需 Pro）。
打开 `https://github.com/SunnyKlara/diansai/settings` →
最底部 **Danger Zone** → 若显示 “Change visibility / 当前 Private”，改为 **Public**。

### 第 3 步 · 开启 Pages
1. 进入仓库 → **Settings**（设置）
2. 左侧菜单 → **Pages**
3. **Build and deployment** → **Source** 选 **Deploy from a branch**
4. **Branch** 选 **`main`**，文件夹选 **`/ (root)`**，点 **Save**

### 第 4 步 · 等待发布
- 保存后顶部会出现一条提示，约 1~3 分钟。
- 刷新 Pages 页面，出现绿色 “Your site is live at
  **https://sunnyklara.github.io/diansai/**” 即成功。
- 打开该地址 → 自动跳转到刷题首页。手机浏览器同样可访问，可“添加到主屏幕”当 App 用。

> 之后每次 `git push origin main`，Pages 会**自动重新部署**，几分钟后生效。

---

## B. 进阶：绑定自定义域名（想要 `刷题.xxx.com` 这种好记地址）

> 需要先拥有一个域名（阿里云/腾讯云/Namesilo 等，`.top`/`.xyz` 年费约 ¥10~30）。

### 第 1 步 · 在 GitHub 填域名
Settings → Pages → **Custom domain** 填入你的域名（如 `dist.example.com`）→ Save。
仓库根会自动生成一个 `CNAME` 文件（**不要手动删**）。

### 第 2 步 · 在域名服务商配 DNS
- **用子域名**（推荐，如 `dist.example.com`）：加一条 **CNAME** 记录
  - 主机记录：`dist`
  - 记录值：`sunnyklara.github.io`
- **用根域名**（如 `example.com`）：加 4 条 **A** 记录指向 GitHub：
  `185.199.108.153`、`185.199.109.153`、`185.199.110.153`、`185.199.111.153`

### 第 3 步 · 等 DNS 生效 + 开 HTTPS
- DNS 生效需几分钟到几小时。
- 回到 Settings → Pages，勾选 **Enforce HTTPS**（证书 GitHub 自动签发）。
- 自定义域名是**从根服务**的，`REPO_BASE` 自动变空，所有路径照常工作，**无需再改代码**。

---

## C. 常见问题排查

| 现象 | 原因 | 解决 |
|---|---|---|
| 页面打开但样式/数据空白 | 浏览器缓存旧文件 | 无痕窗口打开；或确认 `index.html` 的 `?v=` 已 bump |
| 某些 `_xxx.md` 404 | Jekyll 忽略下划线文件 | 确认根目录 `.nojekyll` 已提交 |
| PDF / 真题打不开 | 资源路径没经 `repoUrl()` | 确认用的是改造后的 app.js（`?v=20260611`） |
| Pages 一直不 live | 仓库是 Private | 改 Public，或升级 Pro |
| 中文路径文件偶发 404 | 个别系统对编码敏感 | 已实测中文路径在子路径下正常；如遇到再排查具体文件名 |

---

## D. 三种访问方式速查

| 方式 | 地址 | 适用 | 是否需你开机 |
|---|---|---|---|
| 局域网（Phase 1） | `http://内网IP:8765/web/` | 同 WiFi、备赛日常 | 是 |
| 内网穿透（Phase 2） | cloudflared 临时 HTTPS | 临时给人看 | 是 |
| **GitHub Pages（本文）** | `https://sunnyklara.github.io/diansai/` | **任何人随时查阅** | **否** |
