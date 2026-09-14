"""独立验证 PLUS1：模拟发布 GameStatus，检查接收器的完整 JSON 输出。"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
FLAGS = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def start_simulator(args):
    log_path = ROOT / "build" / "plus1-simulator.log"
    log_path.parent.mkdir(exist_ok=True)
    handle = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [sys.executable, "-X", "utf8", "tools/simulator.py", "--ffmpeg", args.ffmpeg,
         "--port", str(args.port), "--udp-port", str(args.udp_port)],
        stdout=handle, stderr=subprocess.STDOUT, creationflags=FLAGS,
    )
    try:
        deadline = time.monotonic() + 30
        while "SIMULATOR READY" not in log_path.read_text(encoding="utf-8"):
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(log_path.read_text(encoding="utf-8"))
            time.sleep(0.1)
        return process, handle
    except BaseException:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        handle.close()
        raise


def check(args):
    if not (1 <= args.port <= 65535 and 1 <= args.udp_port <= 65535):
        raise ValueError("端口必须在 1 到 65535 之间")
    if args.count < 1 or args.timeout < 1:
        raise ValueError("count 和 timeout 必须为正数")
    # 分别以 TCP/UDP 绑定探测端口；UDP connect_ex 在 Windows 上不能可靠表示占用状态。
    for port, kind in ((args.port, socket.SOCK_STREAM), (args.udp_port, socket.SOCK_DGRAM)):
        with socket.socket(socket.AF_INET, kind) as probe:
            try:
                probe.bind((args.host, port))
            except OSError as error:
                raise RuntimeError(f"端口不可用：{args.host}:{port} ({error})") from error

    simulator, log_handle = start_simulator(args)
    try:
        receiver = ROOT / "dist" / "rm_receive.exe"
        if not receiver.exists():
            receiver = ROOT / "build" / "rm_receive.exe"
        if not receiver.exists():
            raise RuntimeError("找不到 rm_receive.exe，请先执行 tools/build.ps1 -Package")
        result = subprocess.run(
            [str(receiver), "--host", args.host, "--port", str(args.port),
             "--robot-id", "plus1-check", "--count", str(args.count), "--timeout", str(args.timeout)],
            capture_output=True, encoding="utf-8", errors="replace", timeout=args.timeout + 5,
            creationflags=FLAGS,
        )
        if result.returncode != 0:
            raise RuntimeError(f"rm_receive 退出码 {result.returncode}\n{result.stderr}")
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        if len(rows) != args.count:
            raise AssertionError(f"期望 {args.count} 条，实际 {len(rows)} 条")
        required = {
            "current_round", "total_rounds", "red_score", "blue_score", "current_stage",
            "stage_countdown_sec", "stage_elapsed_sec", "is_paused", "game_result", "end_reason",
        }
        for row in rows:
            missing = required - row.keys()
            if missing:
                raise AssertionError(f"缺少协议字段：{sorted(missing)}")
            if row["message_type"] != "GameStatus" or row["current_stage_name"] != "比赛中":
                raise AssertionError(f"GameStatus 派生信息不正确：{row}")
            if row["game_result_name"] is not None or row["end_reason_name"] is not None:
                raise AssertionError("非结算阶段不应展示胜者或结束原因")
            if row["warnings"]:
                raise AssertionError(f"模拟消息出现协议警告：{row['warnings']}")
        evidence = ROOT / "build" / "plus1-evidence.json"
        evidence.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({"plus1_messages": len(rows), "all_protocol_fields": True,
                          "derived_labels": True, "evidence": str(evidence)}, ensure_ascii=False, indent=2))
    finally:
        if simulator.poll() is None:
            simulator.terminate()
            simulator.wait(timeout=5)
        log_handle.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--udp-port", type=int, default=3334)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=10)
    parser.add_argument("--ffmpeg", default=str(ROOT / "dist" / "ffmpeg.exe"))
    check(parser.parse_args())
