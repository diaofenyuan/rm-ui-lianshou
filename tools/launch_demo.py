"""管理本次演示的子进程，关闭窗口时一起停止模拟端。"""
from pathlib import Path
import os
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def launch():
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
        sim = subprocess.Popen([sys.executable, "-X", "utf8", "tools/simulator.py", "--ffmpeg", str(ffmpeg)],
                               stdout=server_log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            deadline = time.monotonic()+45
            while "SIMULATOR READY" not in (ROOT / "build" / "demo-simulator.log").read_text(encoding="utf-8"):
                if sim.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError("模拟端启动失败，查看 build/demo-simulator.log")
                time.sleep(0.1)
            client = subprocess.Popen([str(exe), "--connect", "--ffmpeg", str(ffmpeg)],
                                      stdout=client_log, stderr=subprocess.STDOUT, creationflags=flags)
            try:
                return client.wait()
            finally:
                if client.poll() is None: client.terminate(); client.wait(timeout=5)
        finally:
            if sim.poll() is None: sim.terminate()
            sim.wait(timeout=5)


if __name__ == "__main__":
    try:
        sys.exit(launch())
    except (RuntimeError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
