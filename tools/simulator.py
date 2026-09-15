"""仅在本机提供 MQTT + UDP 测试链路，不实现官方赛事引擎的比赛判定。"""
import argparse
import asyncio
import contextlib
import logging
import math
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
import rm_messages_pb2


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


def game_status(elapsed, rate=1.0):
    # 缩短准备和比赛时长以便观察所有阶段；这些时长仅用于演示。
    t = int(elapsed * rate) % 70
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


# 总控台数据域的合成内容：数值仅为让面板可观察，不代表真实比赛逻辑。
# 频率对齐官方表 2-1：Dynamic 10Hz，其余慢速域 1Hz，Event/Penalty 触发式。

def global_unit_status(elapsed):
    # 协议 2.2.4：robot_health 按己方 1/2/3/4/7 号、对方 1/2/3/4/7 号顺序共 10 项。
    phase = int(elapsed) % 60
    def hp(base, delay): return max(0, base - max(0, phase - delay) * 7)
    ally = [hp(500, 0), hp(400, 5), hp(350, 10), hp(350, 15), hp(300, 20)]
    enemy = [hp(500, 2), hp(400, 8), hp(350, 12), hp(350, 18), hp(300, 22)]
    base_state = 0 if phase < 20 else (1 if phase < 40 else 2)
    outpost_state = 0 if phase < 25 else 1
    return rm_messages_pb2.GlobalUnitStatus(
        base_health=max(0, 5000 - phase * 30), base_status=base_state,
        base_shield=200 if phase < 20 else 0,
        outpost_health=max(0, 1500 - phase * 10), outpost_status=outpost_state,
        enemy_base_health=max(0, 5000 - phase * 20), enemy_base_status=base_state, enemy_base_shield=0,
        enemy_outpost_health=max(0, 1500 - phase * 12), enemy_outpost_status=outpost_state,
        robot_health=ally + enemy, robot_bullets=[max(0, 800 - phase * 5)] * 5,
        total_damage_ally=phase * 130, total_damage_enemy=phase * 110)


def global_logistics(elapsed):
    phase = int(elapsed) % 60
    return rm_messages_pb2.GlobalLogisticsStatus(remaining_economy=400 + phase * 3,
        total_economy_obtained=4000 + phase * 13, tech_level=1 + phase // 20 % 3, encryption_level=2)


def global_special_mechanism(elapsed):
    phase = int(elapsed) % 60
    if 30 <= phase < 35:
        return rm_messages_pb2.GlobalSpecialMechanism(mechanism_id=[1], mechanism_time_sec=[35 - phase])
    return rm_messages_pb2.GlobalSpecialMechanism()


def robot_static(elapsed, robot_id):
    phase = int(elapsed) % 60
    return rm_messages_pb2.RobotStaticStatus(connection_state=1, field_state=0,
        alive_state=1 if phase < 55 else 2, robot_id=robot_id, robot_type=robot_id % 100,
        performance_system_shooter=1, performance_system_chassis=2, level=1 + phase // 45 % 3,
        max_health=400, max_heat=100, heat_cooldown_rate=10.0, max_power=80,
        max_buffer_energy=60, max_chassis_energy=100)


def robot_dynamic(elapsed):
    t = elapsed % 60
    return rm_messages_pb2.RobotDynamicStatus(current_health=max(0, 400 - int(t) * 5),
        current_heat=(t * 17) % 100, last_projectile_fire_rate=18.5, current_chassis_energy=40,
        current_buffer_energy=30, current_experience=int(t) * 12 % 600,
        experience_for_upgrade=600 - int(t) * 12 % 600, total_projectiles_fired=int(t) * 3,
        remaining_ammo=max(0, 800 - int(t) * 3), is_out_of_combat=False, out_of_combat_countdown=0,
        can_remote_heal=True, can_remote_ammo=True)


def robot_module(_elapsed):
    return rm_messages_pb2.RobotModuleStatus(power_manager=1, rfid=0, light_strip=1,
        small_shooter=1, big_shooter=0, uwb=0, armor=1, video_transmission=1,
        capacitor=1, main_controller=1, laser_detection_module=1)


def injury_stat(elapsed):
    phase = int(elapsed) % 60
    return rm_messages_pb2.RobotInjuryStat(total_damage=phase * 90, collision_damage=phase * 4,
        small_projectile_damage=phase * 60, large_projectile_damage=phase * 15,
        dart_splash_damage=phase * 3, module_offline_damage=phase * 2,
        offline_damage=phase, penalty_damage=phase * 5, server_kill_damage=0,
        killer_id=0 if phase < 30 else 101)


def robot_respawn(elapsed):
    phase = int(elapsed) % 20
    if phase >= 18:
        return rm_messages_pb2.RobotRespawnStatus(is_pending_respawn=True, total_respawn_progress=33,
            current_respawn_progress=min(33, (phase - 18) * 16), can_free_respawn=True,
            gold_cost_for_respawn=440, can_pay_for_respawn=True)
    return rm_messages_pb2.RobotRespawnStatus(is_pending_respawn=False)


def robot_position(elapsed, robot_id):
    # 世界坐标系与量纲见 docs/console-design.md：X 沿长边指向蓝方，Y 指向红方停机坪，单位米。
    # 本机在己方半场做小范围机动，便于观察小地图的视角与降级行为。
    home = 19.0 if robot_id > 100 else 8.0
    angle = elapsed * 0.4
    return rm_messages_pb2.RobotPosition(x=home + 2.2 * math.cos(angle), y=5.0 + 3.5 * math.sin(angle),
        z=0.0, yaw=math.degrees(angle) % 360, robot_id=robot_id)


def radar_slot(elapsed, index, enemy, ally_blue):
    # 对方 1/2/3/4/6/7 号与己方同编号各占半场：对方压向我方半场一侧，己方在自己半场活动。
    enemy_home = 7.0 if ally_blue else 21.0
    ally_home = 21.0 if ally_blue else 7.0
    home = enemy_home if enemy else ally_home
    spread = (index - 2.5) * 1.6
    angle = elapsed * 0.5 + index * 0.6
    return (int((home + 2.6 * math.cos(angle)) * 100), int((7.5 + spread * 0.5 + 2.0 * math.sin(angle)) * 100))


def radar_info(elapsed, ally_blue):
    infos = []
    for enemy in (True, False):
        for index in range(6):
            x_cm, y_cm = radar_slot(elapsed, index, enemy, ally_blue)
            infos.append(rm_messages_pb2.RadarSingleRobotInfo(
                target_pos_x=x_cm, target_pos_y=y_cm, is_high_light=1 if enemy and index % 3 == 0 else 0))
    return rm_messages_pb2.RadarInfoToClient(robot_info=infos)


def kill_event():
    return rm_messages_pb2.Event(event_id=1, param="1,101")


def active_buff(elapsed, robot_id):
    phase = elapsed % 30
    if phase < 10:
        return rm_messages_pb2.Buff(robot_id=robot_id, buff_type=1, buff_level=25,
            buff_max_time=10, buff_left_time=10 - int(phase))
    return rm_messages_pb2.Buff()


async def run(args):
    frames = await asyncio.to_thread(video_frames, args.ffmpeg, ROOT / ".tools" / "demo.hevc")
    broker = Broker({"listeners": {"default": {"type": "tcp", "bind": f"{args.bind}:{args.port}"}},
                     "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}}})
    await broker.start()
    publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="rm-local-simulator")
    publisher.connect_async("127.0.0.1", args.port, 10)
    publisher.loop_start()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    started = time.monotonic()
    print(f"SIMULATOR READY: MQTT {args.bind}:{args.port}; UDP {args.udp_destination}:{args.udp_port}; "
          f"slice-base={args.slice_base}; status-rate={args.status_rate}", flush=True)

    async def telemetry():
        while True:
            if publisher.is_connected():
                publisher.publish("GameStatus", game_status(time.monotonic()-started+args.offset,
                    args.status_rate).SerializeToString(), qos=1)
            await asyncio.sleep(0.2)

    async def console_telemetry():
        # Dynamic 10Hz；慢速域合并到每 10 个 tick（1Hz）；Event/Penalty 按触发节奏插入。
        tick = 0
        while True:
            if publisher.is_connected():
                t = time.monotonic() - started
                publisher.publish("RobotDynamicStatus", robot_dynamic(t).SerializeToString(), qos=1)
                if tick % 10 == 0:
                    for topic, message in (
                        ("GlobalUnitStatus", global_unit_status(t)),
                        ("GlobalLogisticsStatus", global_logistics(t)),
                        ("GlobalSpecialMechanism", global_special_mechanism(t)),
                        ("RobotStaticStatus", robot_static(t, args.robot_id)),
                        ("RobotModuleStatus", robot_module(t)),
                        ("RobotInjuryStat", injury_stat(t)),
                        ("RobotRespawnStatus", robot_respawn(t)),
                        ("RobotPosition", robot_position(t, args.robot_id)),
                        ("RadarInfoToClient", radar_info(t, args.robot_id > 100)),
                        ("Buff", active_buff(t, args.robot_id)),
                    ):
                        payload = message.SerializeToString()
                        if payload: publisher.publish(topic, payload, qos=1)
                    if int(t) % 37 == 36:
                        penalty = rm_messages_pb2.PenaltyInfo(penalty_type=4, penalty_effect_sec=5, total_penalty_num=1)
                        publisher.publish("PenaltyInfo", penalty.SerializeToString(), qos=1)
                if tick % 250 == 125:
                    publisher.publish("Event", kill_event().SerializeToString(), qos=1)
            tick += 1
            await asyncio.sleep(0.1)

    async def video():
        frame_id = 0
        deadline = time.monotonic()
        while True:
            data = frames[frame_id % len(frames)]
            chunks = [struct.pack(">HHI", frame_id % 65536, i//1392 + args.slice_base, len(data)) + data[i:i+1392]
                      for i in range(0, len(data), 1392)]
            if args.reorder: chunks.reverse()
            for i, chunk in enumerate(chunks):
                if args.drop_every and frame_id % args.drop_every == 0 and i == 0: continue
                udp.sendto(chunk, (args.udp_destination, args.udp_port))
            frame_id += 1
            deadline += 1/30
            await asyncio.sleep(max(0, deadline-time.monotonic()))

    jobs = [asyncio.create_task(telemetry()), asyncio.create_task(console_telemetry()), asyncio.create_task(video())]
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
    p.add_argument("--bind", default="127.0.0.1", help="MQTT 监听地址；默认只监听本机回环")
    p.add_argument("--udp-destination", default="127.0.0.1", help="图传 UDP 目的地址")
    p.add_argument("--seconds", type=int, default=0)
    p.add_argument("--offset", type=float, default=15)
    p.add_argument("--status-rate", type=float, default=1.0, help="状态时间倍率，测试全阶段时可加速")
    p.add_argument("--slice-base", type=int, choices=(0, 1), default=0, help="UDP 分片编号从 0 或 1 开始")
    p.add_argument("--reorder", action="store_true")
    p.add_argument("--drop-every", type=int, default=0)
    p.add_argument("--robot-id", type=int, default=104,
                   help="单兵消息（RobotStaticStatus/RobotDynamicStatus 等）使用的机器人 ID")
    logging.basicConfig(level=logging.ERROR)
    with contextlib.suppress(KeyboardInterrupt):
        parsed = p.parse_args()
        if parsed.status_rate <= 0: p.error("--status-rate 必须大于 0")
        if parsed.slice_base not in (0, 1): p.error("--slice-base 只能为 0 或 1")
        asyncio.run(run(parsed))
