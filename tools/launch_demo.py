"""管理本次演示的子进程，关闭窗口时一起停止模拟端。"""
from pathlib import Path
import argparse
import os
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def valid_robot_id(robot_id):
    return 1 <= robot_id <= 9 or 101 <= robot_id <= 109


def launch(robot_id):
    if not valid_robot_id(robot_id):
        raise RuntimeError("机器人编号需为红方 1–9 或蓝方 101–109")
    os.chdir(ROOT)
    exe = ROOT / "dist" / "rm_client.exe"
    ffmpeg = ROOT / "dist" / "ffmpeg.exe"
    if not exe.exists():
        raise RuntimeError("请先执行 tools/build.ps1 -Package")
    if not ffmpeg.exists():
        raise RuntimeError("发布目录缺少 FFmpeg，请重新打包")
    # 不接管已有服务，也不冒充一个无法确认身份的本地 Broker。
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", 3333)) == 0:
            raise RuntimeError("3333 端口已被占用，请关闭先前的演示或 MQTT 服务")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    (ROOT / "build").mkdir(exist_ok=True)
    with open(ROOT / "build" / "demo-simulator.log", "w", encoding="utf-8") as server_log, \
         open(ROOT / "build" / "demo-client.log", "w", encoding="utf-8") as client_log:
        # 单兵消息（RobotStaticStatus / RobotDynamicStatus / RobotPosition / Event / 雷达视角）
        # 由模拟端按该编号发布：两端用同一个编号，界面里的阵营才和收到的数据一致。
        sim = subprocess.Popen([sys.executable, "-X", "utf8", "tools/simulator.py",
                                "--ffmpeg", str(ffmpeg), "--robot-id", str(robot_id)],
                               stdout=server_log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            deadline = time.monotonic()+45
            while "SIMULATOR READY" not in (ROOT / "build" / "demo-simulator.log").read_text(encoding="utf-8"):
                if sim.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError("模拟端启动失败，查看 build/demo-simulator.log")
                time.sleep(0.1)
            client = subprocess.Popen([str(exe), "--connect", "--robot-id", str(robot_id), "--ffmpeg", str(ffmpeg)],
                                      stdout=client_log, stderr=subprocess.STDOUT, creationflags=flags)
            try:
                return client.wait()
            finally:
                if client.poll() is None: client.terminate(); client.wait(timeout=5)
        finally:
            if sim.poll() is None: sim.terminate()
            sim.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--robot-id", type=int, default=104,
                        help="本次演示的操作位编号（红方 1–9 / 蓝方 101–109），模拟端与客户端共用")
    parsed = parser.parse_args()
    try:
        sys.exit(launch(parsed.robot_id))
    except (RuntimeError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
