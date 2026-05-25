# AI 与开源资源精选（电赛备赛）

> 收录原则：优先官方/活跃维护/社区认可度高的资源，定期复核链接有效性。
> 内容已按"AI 用法"和"开源代码"两大类整理，注明用途与适用方向。
> 链接可能因平台更新失效，使用时请以实际可达性为准。

---

## 一、AI 工具（按使用阶段）

### 通用大模型

| 工具 | 主要用途 | 备注 |
|---|---|---|
| Claude Sonnet/Opus | 长文档审题、读 datasheet、看波形截图 | 长上下文 + 多模态优秀 |
| ChatGPT (GPT-4o / o1) | 算法推导、代码生成、报告润色 | 多模态 + 工具调用强 |
| Gemini 2.x | 备选，长上下文窗口大 | 中文质量略弱于前两个 |
| 通义千问 / DeepSeek / 豆包 | 国内可直连，离线兜底 | DeepSeek-V3 代码能力强且免费额度高 |

### 编码 IDE / Agent

| 工具 | 适用场景 |
|---|---|
| Kiro | 仓库级 spec/任务/steering 规范化协作（本仓库已配置） |
| Cursor | 文件级快速重构、补全 |
| Claude Code (CLI) | 终端里直接驱动 Claude 改代码 |
| GitHub Copilot | 编辑器内行级补全 |
| Continue.dev | 开源、可接本地模型 |

### 离线兜底（赛场限网时用）

- **Ollama** + DeepSeek-Coder / Qwen2.5-Coder：本地跑代码模型
- 提前下载关键 datasheet、HAL 文档到本地
- 离线代码模板：PID、FFT、卡尔曼、Kalman、滑模、状态机

---

## 二、电赛/智能车开源代码（按方向）

### 智能车与控制（H 题、F 题方向常用）

- **逐飞科技 TC264 开源库**  
  <https://gitee.com/seekfree/TC264_Library>  
  英飞凌 TC264 完整外设封装，智能车竞赛标准库

- **逐飞科技 RT1064 开源库**  
  <https://gitee.com/newtry_lkl/RT1064_Library>  
  i.MX RT1064 视觉组常用

- **逐飞科技 STC8H8K64 开源库**  
  <https://gitee.com/seekfree/STC8H8K64_Library>  
  STC8 单片机参赛常用

- **逐飞科技 LS2K0300 开源库**（龙芯）  
  <https://gitee.com/seekfree/LS2K0300_Library>

- **逐飞助手（上位机）**  
  <https://gitee.com/seekfree/seekfree_assistant>  
  串口、虚拟示波器、摄像头图像三合一调试工具

- **第二十届智能车 缩微光电组教程**  
  <https://gitee.com/lq-tech/Micro-OpticsCart>

- **智能车开源代码合集**  
  <https://github.com/hmayzszjcts/smartcar-Open-source>

### STM32 + PID 参考

- **HAL_STM32F4_Motor_PID_Control**：电机 PID 速度环 + 位置环  
  <https://github.com/HZ1213825/HAL_STM32F4_Motor_PID_Control>

- **PID-Library (Cortex-M)**：通用 PID 库  
  <https://github.com/Majid-Derhambakhsh/PID-Library>

- **pid-stm32f746**：带 GUI 的 PID 控制器  
  <https://github.com/JON95Git/pid-stm32f746>

- **STM32 Line Follower with PID**：循迹小车 + QTR-8RC 反射式传感器  
  <https://github.com/sametoguten/STM32-Line-Follower-with-PID>

### 仪器仪表方向（A/B 题方向）

- **Electronic-competition**：仪器仪表类历年题练习  
  <https://github.com/qiaoxu123/Electronic-competition>  
  ⚠️ 仓库已归档，仅作历史参考

- **MoveTrackSys_TIcup_2023**：2023 国电赛省二作品开源  
  <https://github.com/MenHimChan/MoveTrackSys_TIcup_2023>

### 综合参考

- **STMduino-smartCar**：STM32 + Arduino 智能车，PID 循迹 + WiFi/BLE  
  <https://github.com/clone-yang/STMduino-smartCar>

- **Multi-functional-intelligent-car**：多功能智能小车  
  <https://github.com/iroan/Multi-functional-intelligent-car>

- **ICar**：避障 + 巡线 + PWM + TFT 显示  
  <https://github.com/chrisking94/ICar>

---

## 三、官方与权威信息源

| 名称 | 用途 | 链接 |
|---|---|---|
| 全国大学生电子设计竞赛官网 | 官方文件、竞赛公告 | <https://nuedc.xjtu.edu.cn/> |
| TI 大学计划 | 题目背景、TI 芯片资料 | <https://www.ti.com.cn/zh-cn/university> |
| ST 官方 STM32CubeMX | HAL 库代码生成 | <https://www.st.com/en/development-tools/stm32cubemx.html> |
| ST 官方文档 | HAL/LL 参考手册 | <https://www.st.com/resource/en/user_manual/> |
| LCSC 立创商城 | 元器件查库存 | <https://www.szlcsc.com/> |
| DigiKey | 国际器件搜索 | <https://www.digikey.com/> |
| EEWORLD 论坛 | 国内电赛圈交流 | <https://bbs.eeworld.com.cn/> |
| 嵌入式硬汉论坛 | STM32/RTOS 高质量内容 | <https://www.armbbs.cn/> |

---

## 四、AI 使用经验来源

> 这部分内容更新快，建议直接搜关键词获取最新经验帖。

### 中文社区

| 平台 | 推荐搜索关键词 |
|---|---|
| 知乎 | "电赛 ChatGPT"、"电赛 AI 备赛"、"国一选手 AI 工作流" |
| B 站 | "电赛 国一 复盘"、"电赛 4 天 3 夜"、"Cursor 写嵌入式" |
| 微信公众号 | "电子森林"、"硬汉嵌入式论坛"、"TI 大学计划" |
| CSDN / 掘金 | 搜"电赛 + 大模型"、"嵌入式 + Copilot" 踩坑笔记 |

### 国际社区

- Hackaday Project：<https://hackaday.io/projects>（创意实现思路）
- EEVblog 论坛：<https://www.eevblog.com/forum/>（仪器仪表深水区）
- r/embedded：<https://www.reddit.com/r/embedded/>（嵌入式工具链问题）

---

## 五、我们仓库内的 AI 协作配置

- `.kiro/steering/电赛备赛仓库规范.md`  
  目录结构、Markdown、代码风格、提交规范的强制约束，所有 AI 协作自动加载

- `备赛系统/C_通用能力/AI协作工作流.md`  
  6 阶段工作流（审题→论证→BOM→骨架→调试→报告）+ 反模式清单

- 后续可加（按需）：
  - `.kiro/specs/<题目名>/`：用 spec 工作流跟踪某道题的 requirements/design/tasks
  - `.kiro/hooks/`：保存时自动检查 BOM 表、commit 前自动跑 lint

---

## 六、版权与使用约定

- 本清单仅汇总公开链接，不复制原仓库代码
- 引用任何开源代码到本仓库前，**核对许可证**（GPL/MIT/Apache 等）并保留原作者声明
- 比赛规则要求作品独立完成，开源代码可作为参考与底座，但**核心创新点必须自研**
- 商业 SDK / 厂商私有库提交前确认是否允许公开
