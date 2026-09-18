# RoboMaster 单兵自定义客户端

C++17 / Qt 6 桌面程序，实现训练任务的 PLUS1 和 PLUS2。协议基线为 **RM2026 通信协议 V2.0.0（2026-06-26）**。面向 Windows x64，当前先通过本机模拟验证，尚未连接裁判系统硬件。

## 直接运行

双击项目根目录的 **`start-demo.cmd`**。它会启动本地 MQTT Broker、比赛信息发布器、UDP HEVC 发送器和客户端。关闭客户端窗口会同时停止本次模拟端。已有模拟程序占用 3333 时，启动脚本会提示，不会强行关闭其他进程。

只打开界面：`dist/rm_client.exe`。先选择红方 / 蓝方与兵种编号，再点击“连接所选机器人”。实机使用时选择“连接实机”，展开“连接参数”，按[搭建资料](docs/research-and-setup.md)配置网络，并先确认官方选手端登录的是同一机器人。

界面以白色为主，比赛概览独立显示在主视角上方，关闭画内叠加后仍能查看比分、阶段、倒计时和结算结果。右侧选择操作位、配置连接并查看两条链路状态。F10 切换专注模式，隐藏配置、接收统计和日志，保留当前操作位与链路异常提示；F11 切换全屏，Esc 先退出全屏，再退出专注模式。

Ctrl+Tab 在**总控台**与单兵视角之间切换。总控台由顶栏比分条、我方 / 敌方机器人列表、中央战术地图、复活状态与底部事件、分析、图传预览面板组成：战术地图使用官方场地底图，绘制雷达提供的双方点位与本机位置、朝向，数据超过 3 秒未更新时降级显示最后已知位置。面板构成、数据来源与坐标口径见[总控台设计说明](docs/console-design.md)。

兵种编号按官方协议附录二：1 号英雄、2 号工程、3/4/5 号步兵、6 号空中、7 号哨兵、8 号飞镖、9 号雷达；蓝方机器人 ID 在编号上加 100。MQTT 使用该机器人 ID。连接期间修改选择不会立即切换，点击“应用兵种并重连”后才生效；切换时清除旧比分和图传。选择兵种不会遥控官方选手端切换图传，也不表示所有操作位均已有专属功能。当前接入的是全局 `GameStatus` 和官方转发的主图传，单兵血量、热量、弹量及机器人控制指令尚未接入。

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
- **PLUS2**：接收 UDP 图传，解析大端包头、重组 HEVC 帧，用 FFmpeg 解码，在画面上叠加中文 HUD。支持开关叠加、F11 全屏、Esc 退出全屏。
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

此检查运行已经打包的 EXE，以蓝方 4 号步兵（ID 104）连接，验证 PLUS1 输出、PLUS2 实际解码、18 个红蓝操作位的选择、待应用身份保持、专注模式、叠加与全屏，以及模拟服务器停止 3 秒后重新启动的恢复情况；图传同时注入乱序和每 17 帧丢失一个分片。结果留在 `build/integration-metrics.json`，程序窗口截图为 `build/preview.png`，专注与紧凑布局截图为 `build/operator-*.png`。测试结果只代表本地模拟。

```powershell
# 只验证总控台：点位、时间线、复活、分析与图传预览，同样占用 3333、3334。
.\.venv\Scripts\python.exe -X utf8 tools\check_console.py
```

总控台检查要求战术地图已绘制雷达点位、事件时间线有条目、数据分析与复活状态已收到数据、图传预览有解码帧，并保存 `build/console-console.png`、`build/console-console-minimap.png`、`build/console-console-compact.png` 与 `build/console-live.png` 四张截图；证据留在 `build/console-evidence.json`。

## 代码入口

| 部分 | 入口 |
|---|---|
| 官方消息定义 | `proto/game_status.proto`、`proto/rm_messages.proto` |
| MQTT 接收与重连 | `src/receiver.cpp` |
| 比赛状态聚合与事件时间线 | `src/match_state.cpp` |
| 场地坐标换算 | `src/map_transform.cpp` |
| 图传重组与解码 | `src/assembler.cpp`、`src/video.cpp` |
| 中文界面及叠加 | `src/window.cpp` |
| 总控台面板 | `src/ui/console_page.cpp` 及其同目录控件 |
| 兵种与机器人编号 | `src/operator_profile.h` |
| 独立终端程序 | `src/receive_main.cpp` |
| 模拟发送端 | `tools/simulator.py` |

官方 PDF 保持原件，放在 `docs/references`。第三方库许可随运行目录存于 `dist/licenses`；对外分发二进制时应同时保留相应许可、来源及适用的源代码材料。战术地图底图 `assets/map/map.jpg` 为使用者提供的官方场地地图，对外分发前需确认该素材的授权；见[总控台设计说明](docs/console-design.md)。

界面的信息集中呈现、比赛事件去重和专注视图参考了复旦大学星云 EGA 战队的[开源介绍](https://bbs.robomaster.com/article/1943329)及 [RM26_Client](https://github.com/ClearWei/RM26_Client)；本项目保持现有 Qt Widgets 实现，未引入其代码或素材。
