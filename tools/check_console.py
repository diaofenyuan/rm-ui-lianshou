"""用真实套接字验证总控模式与单兵模式：面板内容、信息叠加、数据域时效与证据截图。"""
import json
import os
import socket
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
FLAGS = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
FFMPEG = str(ROOT / "dist" / "ffmpeg.exe")


def start_simulator(name):
    path = ROOT / "build" / name
    handle = path.open("w", encoding="utf-8")
    process = subprocess.Popen([sys.executable, "-X", "utf8", "tools/simulator.py", "--ffmpeg", FFMPEG],
                               stdout=handle, stderr=subprocess.STDOUT, creationflags=FLAGS)
    try:
        deadline = time.monotonic() + 30
        while "SIMULATOR READY" not in path.read_text(encoding="utf-8"):
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(path.read_text(encoding="utf-8"))
            time.sleep(0.1)
        return process, handle
    except BaseException:
        process.terminate(); process.wait(timeout=5); handle.close(); raise


def check():
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", 3333)) == 0:
            raise RuntimeError("3333 端口已被占用，请先关闭演示窗口")
    sim, handle = start_simulator("console-sim.log")
    client = None
    try:
        with open("build/console-client.log", "w", encoding="utf-8") as log:
            client = subprocess.Popen(["dist/rm_client.exe", "--connect", "--robot-id", "104", "--ffmpeg", FFMPEG,
                "--ui-checks", "--ui-evidence", "build/console",
                "--smoke-seconds", "25", "--screenshot", "build/console-live.png",
                "--metrics", "build/console-metrics.json"],
                stdout=log, stderr=subprocess.STDOUT, creationflags=FLAGS)
            code = client.wait(timeout=45)
        metrics = json.loads(Path("build/console-metrics.json").read_text(encoding="utf-8"))
        assert code == 0, metrics
        assert metrics["ui_checks_passed"], metrics
        assert metrics["robot_id"] == metrics["selected_robot_id"] == 104, metrics
        assert metrics["mqtt_link_count"] == 1 and metrics["mqtt_subscribed_topics"] == 14, metrics
        # 单兵模式：信息叠加默认开启、队友面板固定 5 个协议槽位（1/2/3/4/7）。
        assert metrics["operator_hud_enabled"] and metrics["operator_teammate_rows"] == 5, metrics
        # 单兵模式右上地图与总控模式中央地图是同一控件、同一 MatchState，点位必须一致。
        assert metrics["operator_map_markers"] == metrics["console_markers"], metrics
        # 战术地图：雷达点位已绘制，位置与雷达都是新收到的数据。
        assert metrics["console_markers"] > 0, metrics
        assert metrics["console_radar_messages"] > 0 and metrics["console_position_messages"] > 0, metrics
        assert 0 <= metrics["console_radar_age_ms"] < 3000, metrics
        # 时间线、分析与图传预览都有内容。
        assert metrics["console_timeline"] > 0 and metrics["console_event_messages"] > 0, metrics
        assert metrics["console_analysis"] != "等待数据", metrics
        assert metrics["console_respawn"] != "未收到复活数据", metrics
        assert metrics["decoded_frames"] > 30 and metrics["console_video_preview"], metrics
        for name in ("console-console.png", "console-console-minimap.png", "console-console-compact.png",
                     "console-live.png"):
            assert (ROOT / "build" / name).exists(), name
        evidence = {key: value for key, value in metrics.items()
                    if key.startswith("console_") or key in ("decoded_frames", "ui_checks_passed", "operator")}
        (ROOT / "build" / "console-evidence.json").write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(evidence, ensure_ascii=False, indent=2))
    finally:
        if client and client.poll() is None:
            client.terminate(); client.wait(timeout=5)
        if sim.poll() is None:
            sim.terminate(); sim.wait(timeout=5)
        handle.close()


if __name__ == "__main__":
    check()
