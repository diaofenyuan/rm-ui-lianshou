"""用真实套接字验证已打包的 PLUS1、PLUS2 和服务器重启恢复。"""
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
    process = subprocess.Popen([sys.executable, "-X", "utf8", "tools/simulator.py", "--ffmpeg", FFMPEG,
                                "--reorder", "--drop-every", "17"],
                               stdout=handle, stderr=subprocess.STDOUT, creationflags=FLAGS)
    try:
        deadline = time.monotonic()+30
        while "SIMULATOR READY" not in path.read_text(encoding="utf-8"):
            if process.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError(path.read_text(encoding="utf-8"))
            time.sleep(0.1)
        return process, handle
    except BaseException:
        process.terminate(); process.wait(timeout=5); handle.close(); raise


def check():
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1",3333)) == 0:
            raise RuntimeError("3333 端口已被占用，请先关闭演示窗口")
    sim, handle = start_simulator("integration-sim-1.log")
    client = None
    try:
        cli = subprocess.run(["dist/rm_receive.exe", "--robot-id", "test-receiver", "--count", "5", "--timeout", "10"],
                             capture_output=True, encoding="utf-8", timeout=15, creationflags=FLAGS)
        assert cli.returncode == 0, cli.stderr
        rows = [json.loads(line) for line in cli.stdout.splitlines() if line.startswith("{")]
        assert len(rows) == 5 and all(row["current_round"] == 2 and row["current_stage"] == 4 for row in rows)
        assert all(row["message_type"] == "GameStatus" and row["current_stage_name"] == "比赛中"
                   and row["game_result_name"] is None and row["end_reason_name"] is None
                   and not row["warnings"] for row in rows)
        (ROOT / "build" / "integration-plus1.json").write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding="utf-8")
        with open("build/integration-client.log", "w", encoding="utf-8") as log:
            client = subprocess.Popen(["dist/rm_client.exe", "--connect", "--robot-id", "104", "--ffmpeg", FFMPEG, "--ui-checks",
                "--ui-evidence", "build/operator",
                "--smoke-seconds", "20", "--screenshot", "build/preview.png", "--metrics", "build/integration-metrics.json"],
                stdout=log, stderr=subprocess.STDOUT, creationflags=FLAGS)
            time.sleep(5)
            sim.terminate(); sim.wait(timeout=5); handle.close()
            time.sleep(3)
            sim, handle = start_simulator("integration-sim-2.log")
            code=client.wait(timeout=25)
        metrics=json.loads(Path("build/integration-metrics.json").read_text(encoding="utf-8"))
        assert code == 0, metrics
        assert metrics["ui_checks_passed"] and metrics["decoded_frames"] > 30
        assert metrics["robot_id"] == metrics["selected_robot_id"] == 104
        assert metrics["mqtt_link_count"] == 1 and metrics["mqtt_subscribed_topics"] == 14, metrics
        # 单兵模式：信息叠加默认开启、队友面板固定 5 个协议槽位（1/2/3/4/7）。
        assert metrics["operator_hud_enabled"] and metrics["operator_teammate_rows"] == 5, metrics
        # 总控模式在断线重连后仍应有雷达点位与时间线内容（详细校验见 check_console.py）。
        assert metrics["console_markers"] > 0 and metrics["console_timeline"] > 0, metrics
        print(json.dumps({"plus1_messages":len(rows),"broker_restart_recovered":True,"reordered_packets":True,
                          "drop_every_frames":17,"plus2":metrics},ensure_ascii=False,indent=2))
    finally:
        if client and client.poll() is None: client.terminate();client.wait(timeout=5)
        if sim.poll() is None: sim.terminate();sim.wait(timeout=5)
        handle.close()


if __name__ == "__main__":
    check()
