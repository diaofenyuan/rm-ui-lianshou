"""仅在本机提供 MQTT + UDP 测试链路，不实现官方赛事引擎的比赛判定。"""
import argparse
import asyncio
import contextlib
import logging
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import time

from amqtt.broker import Broker
import paho.mqtt.client as mqtt

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "build" / "generated"))
import game_status_pb2


def video_frames(ffmpeg, video_path):
    video_path.parent.mkdir(parents=True, exist_ok=True)
    if not video_path.exists():
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=1280x720:rate=30", "-t", "6", "-an", "-c:v", "libx265",
                        "-preset", "ultrafast", "-tune", "zerolatency", "-x265-params",
                        "aud=1:repeat-headers=1:keyint=30:min-keyint=30:scenecut=0:bframes=0:log-level=error",
                        "-f", "hevc", "-y", str(video_path)], check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
    data = video_path.read_bytes()
    starts = [m.start() for m in re.finditer(b"\x00\x00\x00\x01\x46", data)]
    if not starts:
        raise RuntimeError("视频必须是带 AUD 的 Annex-B HEVC 流，请删除缓存后重新生成")
    starts[0] = 0
    return [data[a:b] for a, b in zip(starts, starts[1:] + [len(data)])]


def game_status(elapsed):
    # 缩短准备和比赛时长以便观察所有阶段；这些时长仅用于演示。
    t = int(elapsed) % 70
    stage, countdown, spent, paused = 0, 0, 0, False
    if 2 <= t < 7: stage, countdown, spent = 1, 7-t, t-2
    elif 7 <= t < 10: stage, countdown, spent = 2, 10-t, t-7
    elif 10 <= t < 15: stage, countdown, spent = 3, 15-t, t-10
    elif 15 <= t < 60:
        paused = 30 <= t < 35
        spent = t-15 if t<30 else 15 if t<35 else t-20
        stage, countdown = 4, 40-spent
    elif t >= 60: stage, countdown, spent = 5, 0, t-60
    return game_status_pb2.GameStatus(current_round=2, total_rounds=3, red_score=1,
        blue_score=1 if stage==5 else 0, current_stage=stage, stage_countdown_sec=countdown,
        stage_elapsed_sec=spent, is_paused=paused, game_result=2 if stage==5 else 255,
        end_reason=2 if stage==5 else 255)


async def run(args):
    frames = await asyncio.to_thread(video_frames, args.ffmpeg, ROOT / ".tools" / "demo.hevc")
    broker = Broker({"listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{args.port}"}},
                     "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}}})
    await broker.start()
    publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="rm-local-simulator")
    publisher.connect_async("127.0.0.1", args.port, 10)
    publisher.loop_start()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    started = time.monotonic()
    print(f"SIMULATOR READY: MQTT 127.0.0.1:{args.port}; UDP 127.0.0.1:{args.udp_port}", flush=True)

    async def telemetry():
        while True:
            if publisher.is_connected():
                publisher.publish("GameStatus", game_status(time.monotonic()-started+args.offset).SerializeToString(), qos=1)
            await asyncio.sleep(0.2)

    async def video():
        frame_id = 0
        deadline = time.monotonic()
        while True:
            data = frames[frame_id % len(frames)]
            chunks = [struct.pack(">HHI", frame_id % 65536, i//1392, len(data)) + data[i:i+1392]
                      for i in range(0, len(data), 1392)]
            if args.reorder: chunks.reverse()
            for i, chunk in enumerate(chunks):
                if args.drop_every and frame_id % args.drop_every == 0 and i == 0: continue
                udp.sendto(chunk, ("127.0.0.1", args.udp_port))
            frame_id += 1
            deadline += 1/30
            await asyncio.sleep(max(0, deadline-time.monotonic()))

    jobs = [asyncio.create_task(telemetry()), asyncio.create_task(video())]
    try:
        if args.seconds:
            await asyncio.wait_for(asyncio.gather(*jobs), timeout=args.seconds)
        else:
            await asyncio.gather(*jobs)
    except asyncio.TimeoutError:
        pass
    finally:
        for job in jobs: job.cancel()
        await asyncio.gather(*jobs, return_exceptions=True)
        publisher.disconnect(); publisher.loop_stop(); udp.close()
        await broker.shutdown()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ffmpeg", default="ffmpeg")
    p.add_argument("--port", type=int, default=3333)
    p.add_argument("--udp-port", type=int, default=3334)
    p.add_argument("--seconds", type=int, default=0)
    p.add_argument("--offset", type=int, default=15)
    p.add_argument("--reorder", action="store_true")
    p.add_argument("--drop-every", type=int, default=0)
    logging.basicConfig(level=logging.ERROR)
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(run(p.parse_args()))
