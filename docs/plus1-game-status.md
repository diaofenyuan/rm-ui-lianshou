# PLUS1：GameStatus 比赛全局信息接收

本阶段只负责接收和输出比赛全局状态，不向机器人发送控制指令。协议真相源为 **RoboMaster 2026 高校系列赛通信协议 V2.0.0（2026-06-26）** 的 2.2.3 节。

## 链路

赛事引擎服务器 → MQTT `GameStatus` → `StatusReceiver` → `rm_receive` → 一行一条 JSON。

- MQTT 默认地址：`tcp://127.0.0.1:3333`（实机改为官方选手端第二网卡地址）
- Topic：`GameStatus`
- QoS：订阅 QoS 1
- Client ID：目标机器人编号；同一机器人不要同时启动两个同 ID 的接收器
- Payload：官方 Protobuf 二进制，不是 JSON 字符串

## 协议字段

| 字段 | 类型 | 含义 |
|---|---|---|
| `current_round` | `uint32` | 当前局号，从 1 开始 |
| `total_rounds` | `uint32` | 总局数 |
| `red_score` / `blue_score` | `uint32` | 红、蓝方得分 |
| `current_stage` | `uint32` | 0 未开始、1 准备、2 十五秒裁判系统自检、3 五秒倒计时、4 比赛中、5 比赛结算中 |
| `stage_countdown_sec` | `int32` | 当前阶段剩余秒数，负数原样保留 |
| `stage_elapsed_sec` | `int32` | 当前阶段已过秒数，负数原样保留 |
| `is_paused` | `bool` | 是否暂停 |
| `game_result` | `uint32` | 结算阶段才有意义：0 平局、1 红方胜利、2 蓝方胜利；其余阶段通常为 255 |
| `end_reason` | `uint32` | 结算阶段结束原因，1–9 对应协议枚举；其余阶段通常为 255 |

程序始终输出原始十个字段；字段不存在输出 `null`，不会用上一条消息补值。输出同时包含 `message_type`、阶段/时间可读文本、结算可读文本和 `warnings`，便于人工查看和脚本验收。未知枚举保留数字并写入警告，不会静默改写。

## 本地运行

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Package
start-demo.cmd
```

另开终端运行独立接收器（使用不同 Client ID，避免把桌面客户端挤下线）：

```powershell
.\dist\rm_receive.exe --host 127.0.0.1 --port 3333 --robot-id plus1-receiver --count 5 --timeout 10
```

只验证 PLUS1：

```powershell
.\.venv\Scripts\python.exe -X utf8 tools\check_plus1.py
```

通过后会生成 `build/plus1-evidence.json`，其中每一行都是一条完整 `GameStatus` 快照。

## 实机接入顺序

1. 按官方联网手册启动赛事引擎，裁判端局域网网卡使用 `192.168.1.2`，确认服务器已启动。
2. 官方选手端先登录目标机器人，并确认官方画面和比赛信息正常。
3. 将自定义客户端电脑接入官方选手端的第二张网卡；按双网卡方案使用官方选手端 `192.168.12.1`、本机 `192.168.12.2`。
4. 先运行 `rm_receive`，地址改为 `192.168.12.1`，Client ID 改为与官方选手端相同的机器人编号。
5. 在裁判端依次切换准备、自检、倒计时、比赛、暂停和结算，核对十个字段及可读标签；完成后再启动 PLUS2 桌面客户端。

本项目不会自动修改网卡、路由或防火墙。真实服务器、机器人在线状态和正式图传必须在具备硬件的环境中单独验收；本地模拟证据不等同于实机验收。

## PLUS1 验收标准

- 能连续收到至少 5 条 `GameStatus`，且每条都能由 Protobuf 解析。
- 十个协议字段均可观察，缺失字段显示 `null`。
- 阶段、倒计时、暂停、比分和结算信息与裁判端操作一致。
- MQTT 断开后能重连；损坏 Protobuf 不使程序退出。
- `tools/check_plus1.py` 和核心测试均通过。
