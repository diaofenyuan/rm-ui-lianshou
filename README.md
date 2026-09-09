# RoboMaster 裁判系统自定义客户端

C++17 / Qt 6 桌面程序，实现训练任务的 PLUS1 和 PLUS2。协议基线为 **RM2026 通信协议 V2.0.0（2026-06-26）**。面向 Windows x64，当前先通过本机模拟验证，尚未连接裁判系统硬件。

## 直接运行

双击项目根目录的 **`start-demo.cmd`**。它会启动本地 MQTT Broker、比赛信息发布器、UDP HEVC 发送器和客户端。关闭客户端窗口会同时停止本次模拟端。已有模拟程序占用 3333 时，启动脚本会提示，不会强行关闭其他进程。

只打开界面：`dist/rm_client.exe`。实机使用时，在界面中选择“连接实机”，按[搭建资料](docs/research-and-setup.md)配置网络和机器人 ID，再点击连接。

界面以白色为主，左侧监看图传和比分，右侧查看两条链路的接收状态。端口、监听 IP 和 FFmpeg 路径收在“高级连接参数”中；底部“接收日志”可展开，分别查看比赛 JSON 与运行事件，支持暂停滚动和复制。暂停滚动不会停止接收。“距最近更新”表示数据的接收时间间隔，不是网络延迟。

PLUS1 独立接收程序：

```powershell
# 先启动演示，使用不同的 Client ID，避免 MQTT 将已有客户端挤下线。
.\dist\rm_receive.exe --host 127.0.0.1 --robot-id test-receiver --count 5 --timeout 10

# 实机：机器人编号应与官方选手端登录的编号一致。
.\dist\rm_receive.exe --host 192.168.12.1 --robot-id 3
```

标准输出每行一条 JSON，包含十个协议字段及接收时间；连接日志写入标准错误。`--count` / `--timeout` 便于自动检查。不指定条数时持续输出。

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

依赖：Qt 6.8.3 MinGW、MinGW 13.1、CMake/Ninja、Protobuf 3.21.12、Eclipse Paho C 1.3.14；模拟端版本锁定在 `tools/requirements.txt`。Qt 从官方源下载；Python 使用清华源。CMake 从对应官方 GitHub 仓库拉取 C/C++ 依赖。FFmpeg 需能执行 `ffmpeg -encoders` 并包含 `libx265`；打包会复制当前 PATH 中的 `ffmpeg.exe`，本机使用静态发行版。

`dist` 包含客户端、独立接收器、Qt/MinGW 运行库及 FFmpeg。模拟演示还需要本项目的 `tools`、`.venv` 与 `build/generated/game_status_pb2.py`，不能只复制 `dist` 后运行模拟脚本。

## 验证

`tools/build.ps1` 执行核心协议测试，包括固定二进制样本、缺失字段、未知枚举、负数时间、损坏 Protobuf、UDP 乱序重复、超时丢包、长度限制和帧号回绕。

```powershell
# 会临时占用本机 3333、3334；运行前先关闭演示窗口。
.\.venv\Scripts\python.exe -X utf8 tools\check_link.py
```

此检查运行已经打包的 EXE，验证 PLUS1 输出、PLUS2 实际解码、叠加开关、全屏切换，以及模拟服务器停止 3 秒后重新启动的恢复情况；图传同时注入乱序和每 17 帧丢失一个分片。结果留在 `build/integration-metrics.json`，程序窗口截图为 `build/preview.png`。测试结果只代表本地模拟。

## 代码入口

| 部分 | 入口 |
|---|---|
| 官方消息定义 | `proto/game_status.proto` |
| MQTT 接收与重连 | `src/receiver.cpp` |
| 图传重组与解码 | `src/assembler.cpp`、`src/video.cpp` |
| 中文界面及叠加 | `src/window.cpp` |
| 独立终端程序 | `src/receive_main.cpp` |
| 模拟发送端 | `tools/simulator.py` |

官方 PDF 保持原件，放在 `docs/references`。第三方库许可随运行目录存于 `dist/licenses`；对外分发二进制时应同时保留相应许可、来源及适用的源代码材料。
