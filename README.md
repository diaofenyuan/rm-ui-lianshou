# RoboMaster 单兵自定义客户端

C++17 / Qt 6 桌面程序，实现训练任务的 PLUS1 和 PLUS2。协议基线为 **RM2026 通信协议 V2.0.0（2026-06-26）**。面向 Windows x64，当前先通过本机模拟验证，尚未连接裁判系统硬件。

## 直接运行

双击项目根目录的 **`start-demo.cmd`**。它会启动本地 MQTT Broker、比赛信息发布器、UDP HEVC 发送器和客户端。关闭客户端窗口会同时停止本次模拟端。已有模拟程序占用 3333 时，启动脚本会提示，不会强行关闭其他进程。演示默认使用蓝方 4 号步兵（ID 104）；改用其他操作位时执行 `tools\launch_demo.py --robot-id <编号>`，模拟端与客户端共用同一编号——模拟端的单兵数据（RobotStaticStatus / RobotDynamicStatus / RobotPosition / Event / 雷达视角）只按该编号发布，两端编号不一致时界面阵营会与收到的数据对不上。

只打开界面：`dist/rm_client.exe`。先选择红方 / 蓝方与兵种编号，再点击“连接所选机器人”。实机使用时选择“连接实机”，展开“连接参数”，按[搭建资料](docs/research-and-setup.md)配置网络，并先确认官方选手端登录的是同一机器人。

界面以白色为主，两个模式共用同一条主连接与同一份状态模型。背景说明与各面板的数据来源见[单兵模式与总控模式说明](docs/operator-mode.md)。

**单兵模式**（`Ctrl+Tab` 切到此页）以全屏图传为主体，信息以半透明 HUD 叠在画面内：顶部信息条给出本机身份与存活状态、阶段名、阶段倒计时（比赛中最后 10 秒转紧急色）、第 N / M 局、特殊机制计时与两枚链路时效点；左上为**队友面板**，按协议固定槽位 1/2/3/4/7 出 5 行（血量来自全局域，位置时效点来自雷达的己方槽位）；左下为**本机卡**（血量 / 热量含上限、弹量、等级、主控、累计发弹）；右上为**全场地图**，与总控模式中央地图是同一控件、同一套坐标换算。

单兵模式**不显示比分**（比分只在总控模式呈现）。事件时间线只在总控模式展开。左下角“信息叠加”复选框控制整个 HUD 的显隐，关闭后只剩纯画面；工具行里同时给出数据来源、操作位铭牌与图传分辨率。配置栏内的下拉框与端口框不响应滚轮，滚轮一律用于滚动配置栏本身（下拉框点击后展开、端口可用方向键调整）。

快捷键：`F10` 进入 / 退出**专注模式**（全屏 + 隐藏配置栏、接收统计与日志，HUD 全部保留、卡底略降不透明度）；`F11` 切换**纯全屏**（无 HUD，与专注态互斥）；`Esc` 先退专注、再退纯全屏，一次退到底；`M` 开关地图（单兵模式切右上角 HUD 地图，总控模式切中央战术地图；焦点在日志框等文本控件内时不触发，避免打字误切）。

**总控模式**由顶栏比分条、我方 / 敌方机器人列表、中央战术地图、复活状态与底部事件、分析、图传预览面板组成：战术地图使用官方场地底图，绘制雷达提供的双方点位与本机位置、朝向，数据超过 3 秒未更新时降级显示最后已知位置。面板构成、数据来源与坐标口径见[总控模式设计说明](docs/console-design.md)。顶栏比分条统一按红方 / 蓝方命名，红方在左、蓝方在右，本方一侧标注“我方”；机器人血量按协议 2.2.4 的己方在前 / 对方在后顺序取槽位，与 2.2.19 雷达的相反。

兵种编号按官方协议附录二：1 号英雄、2 号工程、3/4/5 号步兵、6 号空中、7 号哨兵、8 号飞镖、9 号雷达；蓝方机器人 ID 在编号上加 100。MQTT 使用该机器人 ID。连接期间修改选择不会立即切换，点击“应用兵种并重连”后才生效；切换时清除旧比分和图传。选择兵种不会遥控官方选手端切换图传，也不表示所有操作位均已有专属功能。当前已接入全局比赛/态势域和当前连接操作位的单机域，单兵模式可显示本机血量、热量、弹量、等级、主控与复活状态；机器人控制指令不在本客户端范围内。

底部“接收日志”可展开，分别查看比赛 JSON 和比赛动态 / 运行事件，支持暂停滚动和复制。比赛动态只记录连续消息中已确认的阶段、暂停、比分和结算变化；重复消息和正常倒计时更新不会刷屏。暂停滚动不会停止接收。“距最近更新”表示数据的接收时间间隔，不是网络延迟。

也可指定初始操作位，例如蓝方 4 号步兵：`dist/rm_client.exe --robot-id 104`。本地模拟器发布同一组全局比赛信息与测试图，不模拟不同兵种的独立图传。

PLUS1 独立接收程序：

```powershell
# 先启动演示，使用不同的 Client ID，避免 MQTT 将已有客户端挤下线。
.\dist\rm_receive.exe --host 127.0.0.1 --robot-id test-receiver --count 5 --timeout 10

# 实机：机器人编号应与官方选手端登录的编号一致。
.\dist\rm_receive.exe --host 192.168.12.1 --robot-id 3
```

标准输出每行一条 JSON，包含十个协议字段及接收时间；连接日志写入标准错误。`--count` / `--timeout` 便于自动检查。不指定条数时持续输出。

PLUS1 的字段、链路、实机接入顺序和验收标准见 [GameStatus 接收说明](docs/plus1-game-status.md)。只验证 PLUS1 可运行 `.\.venv\Scripts\python.exe -X utf8 tools\check_plus1.py`。

## 功能与边界

- **PLUS1**：订阅 `GameStatus`，QoS 1，官方 Protobuf 生成代码解析，输出比赛局号、总局数、红蓝方得分、阶段、剩余时间、已过时间、暂停、胜者和结束原因。
- **PLUS2**：接收 UDP 图传，解析大端包头、重组 HEVC 帧，用 FFmpeg 解码，把画面固定按 16:9 绘制并居中。信息叠加层（HUD）单独重绘、不跟图传帧率；`F10` 专注（全屏 + 保留 HUD）、`F11` 纯全屏（无 HUD）、`Esc` 一次退到底、`M` 开关地图、`Ctrl+Tab` 切换模式。
- **单兵模式**：图传为主体 + 半透明 HUD。队友面板与全场地图不需要额外连接即可用（血量取自全局域 `GlobalUnitStatus`、点位取自 `RadarInfoToClient`）；热量 / 弹量 / 等级 / 主控等**单机域**字段目前只覆盖本机，队友列要等各队友各自建链后才能补齐（见[改造计划](docs/operator-mode-plan.md)批次 C）。
- MQTT 断开后每 2 秒重连并重新订阅；超过 1.5 秒未收到信息标记过期，不自行猜测倒计时。图传中断时标明显示的是最后一帧。
- Protobuf `optional` 字段缺失时输出 `null` / 显示“未提供”；每个消息独立显示，不把前一条缺失字段静默补入。未知枚举保留数值。结算前不把 `255` 显示为获胜结果。
- 图传最多缓存 8 个未完成帧，单帧上限 4 MiB，150 ms 后丢弃残帧；处理乱序、重复、异常长度和 16 位帧号回绕。分片序号当前按 **0 起始**实现，官方 V2.0.0 未明确写出起始基数，此项保留实机抓包确认。
- 官方图传为 1920×1080、60 fps。当前解码后显示缩放至不超过 1280×720，优先显示最新帧；**1080p60 实机性能尚未验证**。本地模拟为 1280×720、30 fps 测试图。
- 模拟器只模拟这两条通信链路，不具备官方服务器的裁判判罚、模块管理和真实比赛逻辑。它使用 Python 辅助启动 MQTT Broker，接收程序与桌面客户端均为 C++。

## 构建

已安装的 Qt 和编译器放在项目 `.tools` 中，不修改系统网卡或防火墙。

```powershell
# 已有环境：编译、执行核心测试、生成模拟端协议类并打包运行库。
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Package

# 换一台 Windows 机器：需先有 Python 3.11 与 FFmpeg（支持 libx265）。
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Setup -Package
```

只准备模拟端环境时可以双击项目根目录的 `setup-venv.cmd`（等价于 `tools\setup-venv.ps1`）：它定位 Python 3.11、创建 `.venv`、按清华源安装 `tools/requirements.txt`，并在已有 `protoc.exe` 时生成 protobuf Python 代码。加 `-Force` 重建环境，加 `-WithBuildTools` 一并安装 aqtinstall / cmake / ninja，加 `-UseDefaultIndex` 改用 PyPI 官方源。

依赖：Qt 6.8.3 MinGW、MinGW 13.1、CMake/Ninja、Protobuf 3.21.12、Eclipse Paho C 1.3.14；模拟端版本锁定在 `tools/requirements.txt`。Qt 从官方源下载；Python 使用清华源。CMake 从对应官方 GitHub 仓库拉取 C/C++ 依赖。FFmpeg 需能执行 `ffmpeg -encoders` 并包含 `libx265`；打包会复制当前 PATH 中的 `ffmpeg.exe`，本机使用静态发行版。

`dist` 包含客户端、独立接收器、Qt/MinGW 运行库及 FFmpeg。模拟演示还需要本项目的 `tools`、`.venv` 与 `build/generated/game_status_pb2.py`，不能只复制 `dist` 后运行模拟脚本。

## 验证

`tools/build.ps1` 执行核心协议测试，包括固定二进制样本、缺失字段、未知枚举、负数时间、损坏 Protobuf、UDP 乱序重复、超时丢包、长度限制和帧号回绕。

```powershell
# 会临时占用本机 3333、3334；运行前先关闭演示窗口。
.\.venv\Scripts\python.exe -X utf8 tools\check_link.py
```

此检查运行已经打包的 EXE，以蓝方 4 号步兵（ID 104）连接，验证 PLUS1 输出、PLUS2 实际解码、18 个红蓝操作位的选择、待应用身份保持、信息叠加开关、队友面板槽位、地图开关、专注即全屏、Esc 与纯全屏互斥，以及模拟服务器停止 3 秒后重新启动的恢复情况；图传同时注入乱序和每 17 帧丢失一个分片。结果留在 `build/integration-metrics.json`，程序窗口截图为 `build/preview.png`，各状态截图为 `build/operator-*.png`。测试结果只代表本地模拟。

```powershell
# 只验证单兵模式与总控模式：信息叠加、地图开关、点位、时间线、复活、分析与图传预览，同样占用 3333、3334。
.\.venv\Scripts\python.exe -X utf8 tools\check_console.py
```

该检查要求单兵模式「信息叠加」默认开启、队友面板固定 5 个协议槽位、两模式地图点位一致；总控模式战术地图已绘制雷达点位、事件时间线有条目、数据分析与复活状态已收到数据、图传预览有解码帧。证据截图：`build/console-console.png`、`build/console-console-minimap.png`、`build/console-console-compact.png`、`build/console-live.png`，以及单兵模式各状态 `build/console-operator.png`、`build/console-focus.png`、`build/console-hud.png`、`build/console-compact.png`、`build/console-minimum.png`；证据留在 `build/console-evidence.json`。

## 代码入口

| 部分 | 入口 |
|---|---|
| 官方消息定义 | `proto/game_status.proto`、`proto/rm_messages.proto` |
| MQTT 接收与重连 | `src/receiver.cpp` |
| 比赛状态聚合与事件时间线 | `src/match_state.cpp` |
| 场地坐标换算 | `src/map_transform.cpp` |
| 图传重组与解码 | `src/assembler.cpp`、`src/video.cpp` |
| 主窗口：配置栏、日志、链路接线与模式切换 | `src/window.cpp` |
| 单兵模式页面与 HUD | `src/ui/operator_page.cpp`、`src/ui/operator_hud.cpp` 及其同目录控件 |
| 总控模式面板 | `src/ui/console_page.cpp` 及其同目录控件 |
| 地图与场地坐标口径（两个模式共用） | `src/ui/minimap.cpp`、`src/map_transform.cpp` |
| 兵种与机器人编号 | `src/operator_profile.h` |
| 独立终端程序 | `src/receive_main.cpp` |
| 模拟发送端 | `tools/simulator.py` |

官方 PDF 保持原件，放在 `docs/references`。第三方库许可随运行目录存于 `dist/licenses`；对外分发二进制时应同时保留相应许可、来源及适用的源代码材料。战术地图底图 `assets/map/map.jpg` 为使用者提供的官方场地地图，对外分发前需确认该素材的授权；见[总控模式设计说明](docs/console-design.md)。

界面的信息集中呈现、比赛事件去重和专注视图参考了复旦大学星云 EGA 战队的[开源介绍](https://bbs.robomaster.com/article/1943329)及 [RM26_Client](https://github.com/ClearWei/RM26_Client)；本项目保持现有 Qt Widgets 实现，未引入其代码或素材。
