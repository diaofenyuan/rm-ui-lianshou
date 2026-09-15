#include "status.h"
#include "operator_profile.h"
#include <QJsonArray>
#include <QStringList>

namespace status {
namespace {
// 新增数据域共用的小工具：枚举越界时保留数值，与既有“未知枚举不改写”口径一致。
QString labeled(quint32 value, const QStringList &labels, const QString &name) {
    return value < quint32(labels.size()) ? labels[value] : QString("未知%1（%2）").arg(name).arg(value);
}
}
QString stage(quint32 v) {
    static const QStringList labels = {"未开始比赛", "准备阶段", "十五秒裁判系统自检阶段", "五秒倒计时", "比赛中", "比赛结算中"};
    return v < quint32(labels.size()) ? labels[v] : QString("未知阶段（%1）").arg(v);
}
QString result(quint32 v) {
    static const QStringList labels = {"平局", "红方胜利", "蓝方胜利"};
    return v < 3 ? labels[v] : QString("未知结果（%1）").arg(v);
}
QString reason(quint32 v) {
    static const QStringList labels = {"未知", "基地被摧毁", "基地剩余血量", "前哨站剩余血量", "前哨站是否被击毁过", "全队总攻击伤害", "全队机器人总剩余血量", "平局", "异常终止", "主裁判手动判定胜负"};
    return v > 0 && v < quint32(labels.size()) ? labels[v] : QString("未知原因（%1）").arg(v);
}
QString duration(qint32 seconds) {
    const qint64 n = qAbs(qint64(seconds));
    return QString("%1%2:%3").arg(seconds < 0 ? "-" : "").arg(n / 60, 2, 10, QChar('0')).arg(n % 60, 2, 10, QChar('0'));
}
bool isSettlement(const rm::GameStatus &v) {
    return v.has_current_stage() && v.current_stage() == 5;
}
QStringList warnings(const rm::GameStatus &v) {
    QStringList issues;
    if (v.has_current_stage() && v.current_stage() > 5)
        issues << QString("current_stage 超出协议枚举：%1").arg(v.current_stage());
    if (isSettlement(v)) {
        if (!v.has_game_result()) issues << "结算阶段未提供 game_result";
        else if (v.game_result() > 2) issues << QString("game_result 超出协议枚举：%1").arg(v.game_result());
        if (!v.has_end_reason()) issues << "结算阶段未提供 end_reason";
        else if (v.end_reason() == 0 || v.end_reason() > 9)
            issues << QString("end_reason 超出协议枚举：%1").arg(v.end_reason());
    } else {
        if (v.has_game_result() && v.game_result() != 255)
            issues << QString("非结算阶段 game_result 应为 255，实际为 %1").arg(v.game_result());
        if (v.has_end_reason() && v.end_reason() != 255)
            issues << QString("非结算阶段 end_reason 应为 255，实际为 %1").arg(v.end_reason());
    }
    return issues;
}
QStringList changes(const rm::GameStatus &before, const rm::GameStatus &after) {
    QStringList events;
    // 只比较连续快照中双方均提供的字段；缺失字段和局次切换不推导为比分变化。
    const bool newRound = before.has_current_round() && after.has_current_round() && before.current_round() != after.current_round();
    if (newRound) events << QString("进入第 %1 局").arg(after.current_round());
    if (before.has_current_stage() && after.has_current_stage() && (newRound || before.current_stage() != after.current_stage()))
        events << "比赛阶段：" + stage(after.current_stage());
    if (!newRound && before.has_is_paused() && after.has_is_paused() && before.is_paused() != after.is_paused())
        events << (after.is_paused() ? "比赛暂停" : "比赛恢复");
    if (!newRound && before.has_red_score() && after.has_red_score() && before.red_score() != after.red_score())
        events << QString("红方得分：%1 → %2").arg(before.red_score()).arg(after.red_score());
    if (!newRound && before.has_blue_score() && after.has_blue_score() && before.blue_score() != after.blue_score())
        events << QString("蓝方得分：%1 → %2").arg(before.blue_score()).arg(after.blue_score());
    const bool settlement = isSettlement(after);
    const bool resultChanged = !before.has_game_result() || before.game_result() != after.game_result();
    if (settlement && after.has_game_result() && (newRound || !before.has_current_stage() || before.current_stage() != 5 || resultChanged))
        events << "比赛结算：" + result(after.game_result());
    const bool reasonChanged = !before.has_end_reason() || before.end_reason() != after.end_reason();
    if (settlement && after.has_end_reason() && (newRound || !before.has_current_stage() || before.current_stage() != 5 || reasonChanged))
        events << "结束原因：" + reason(after.end_reason());
    return events;
}
QJsonObject json(const rm::GameStatus &v) {
    QJsonObject o;
    // 缺失字段输出 null，避免把未发送与数值 0 混淆；每条消息作为独立快照。
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(current_round); FIELD(total_rounds); FIELD(red_score); FIELD(blue_score);
    FIELD(current_stage); FIELD(stage_countdown_sec); FIELD(stage_elapsed_sec);
    o["is_paused"] = v.has_is_paused() ? QJsonValue(v.is_paused()) : QJsonValue(QJsonValue::Null);
    FIELD(game_result); FIELD(end_reason);
#undef FIELD
    o["message_type"] = "GameStatus";
    o["protocol"] = "RM2026-V2.0.0";
    QJsonObject presence;
#define PRESENT(name) presence[#name] = v.has_##name()
    PRESENT(current_round); PRESENT(total_rounds); PRESENT(red_score); PRESENT(blue_score);
    PRESENT(current_stage); PRESENT(stage_countdown_sec); PRESENT(stage_elapsed_sec);
    PRESENT(is_paused); PRESENT(game_result); PRESENT(end_reason);
#undef PRESENT
    o["presence"] = presence;
    o["stage_name"] = v.has_current_stage() ? stage(v.current_stage()) : "未提供";
    o["current_stage_name"] = v.has_current_stage() ? stage(v.current_stage()) : QJsonValue(QJsonValue::Null);
    o["stage_countdown_text"] = v.has_stage_countdown_sec() ? duration(v.stage_countdown_sec()) : QJsonValue(QJsonValue::Null);
    o["stage_elapsed_text"] = v.has_stage_elapsed_sec() ? duration(v.stage_elapsed_sec()) : QJsonValue(QJsonValue::Null);
    o["game_result_name"] = isSettlement(v) && v.has_game_result() && v.game_result() != 255
        ? QJsonValue(result(v.game_result())) : QJsonValue(QJsonValue::Null);
    o["end_reason_name"] = isSettlement(v) && v.has_end_reason() && v.end_reason() != 255
        ? QJsonValue(reason(v.end_reason())) : QJsonValue(QJsonValue::Null);
    QJsonArray issueArray;
    for (const auto &issue : warnings(v)) issueArray.append(issue);
    o["warnings"] = issueArray;
    return o;
}

QString robotType(quint32 v) {
    const int number = int(v);
    if (number >= 1 && number <= 9)
        return QString("%1号%2").arg(number).arg(QString::fromUtf8(profile::roles[number - 1].name));
    return QString("未知类型（%1）").arg(v);
}
QString connectionState(quint32 v) {
    return labeled(v, {"未连接", "已连接"}, "连接状态");
}
QString fieldState(quint32 v) {
    return labeled(v, {"已上场", "未上场"}, "上场状态");
}
QString aliveState(quint32 v) {
    return labeled(v, {"未知", "存活", "战亡"}, "存活状态");
}
QString moduleState(quint32 v) {
    return labeled(v, {"离线", "在线", "安装不规范（视为离线）"}, "模块状态");
}
QString baseStatus(quint32 v) {
    return labeled(v, {"无敌", "解除无敌，护甲未展开", "解除无敌，护甲展开"}, "基地状态");
}
QString outpostStatus(quint32 v) {
    return labeled(v, {"无敌", "存活，解除无敌，中部装甲旋转", "存活，解除无敌，中部装甲停转",
        "被击毁，不可重建", "被击毁，可重建", "被击毁，重建中"}, "前哨站状态");
}
QString penaltyType(quint32 v) {
    return labeled(v, {"未知", "黄牌", "双方黄牌", "红牌", "超功率", "超热量", "超射速"}, "判罚类型");
}
QString buffType(quint32 v) {
    return labeled(v, {"未知", "攻击增益", "防御增益/易伤", "射击热量冷却增益", "底盘功率增益",
        "回血增益", "可兑换允许发弹量", "地形跨越增益（预备）"}, "Buff 类型");
}
QString mechanismText(quint32 id, qint32 seconds) {
    switch (id) {
    case 1: return QString("己方堡垒被对方占领，剩余 %1 秒").arg(seconds);
    case 2: return QString("对方堡垒被己方占领，剩余 %1 秒").arg(seconds);
    default: return QString("特殊机制 %1，剩余 %2 秒").arg(id).arg(seconds);
    }
}
QString eventText(const rm::Event &v) {
    if (!v.has_event_id()) return "事件（未提供编号）";
    const int id = v.event_id();
    const QString param = QString::fromStdString(v.param());
    const QStringList parts = param.split(',', Qt::SkipEmptyParts);
    const auto number = [&parts](int index) -> int {
        return index >= 0 && index < parts.size() ? parts[index].toInt() : 0;
    };
    switch (id) {
    case 1:
        if (parts.size() >= 2)
            return QString("击杀事件：%1 被 %2 击毁").arg(profile::name(number(0)), profile::name(number(1)));
        return "击杀事件（参数缺失）";
    case 2:
        if (param == "11") return "红方前哨站被摧毁";
        if (param == "111") return "蓝方前哨站被摧毁";
        return QString("前哨站被摧毁（目标 %1）").arg(param);
    case 3:
        if (parts.size() >= 2) return QString("大能量机关激活：%1 臂 · 平均 %2 环").arg(parts[0], parts[1]);
        return "大能量机关被成功激活";
    case 4:
        if (number(0) == 1) return "小能量机关进入已激活状态";
        if (number(0) == 2) return "大能量机关进入已激活状态";
        return QString("能量机关进入已激活状态（类型 %1）").arg(param);
    case 5: return QString("己方英雄造成狙击伤害，累计 %1").arg(param);
    case 6: return QString("对方英雄造成狙击伤害，累计 %1").arg(param);
    case 7: return "对方呼叫空中支援";
    case 8: return QString("对方空中支援被反制，己方剩余可反制 %1 次").arg(param);
    case 9: {
        static const QStringList targets = {"", "前哨站", "基地固定目标", "基地随机固定目标", "基地随机移动目标", "基地末端移动目标"};
        const int side = number(0), target = number(1);
        const QString sideText = side == 1 ? "红方" : side == 2 ? "蓝方" : QString("未知方（%1）").arg(side);
        return QString("飞镖命中：%1%2").arg(sideText,
            target >= 1 && target < targets.size() ? targets[target] : QString("（目标 %1）").arg(param));
    }
    case 10: return "对方飞镖闸门开启";
    case 11: return "基地遭到攻击";
    case 12: return "对方前哨站停转";
    case 13: return "对方基地护甲展开";
    case 14: return "对方请求四级装配，进入强制退出缓冲期";
    case 15: {
        static const QStringList results = {"装配成功", "装配拔出", "装配超时", "离开装配区过久",
            "工程战亡", "四级难度未满足协作时限", "主动退出装配", "完成装配但结算时未检测到能量单元",
            "缓冲期到期，装配流程强制结束"};
        const int r = number(0);
        return QString("装配结果：%1").arg(r >= 0 && r < results.size() ? results[r] : QString("未知（%1）").arg(param));
    }
    default: return QString("未知事件（%1）参数 %2").arg(id).arg(param);
    }
}

QJsonObject json(const rm::GlobalUnitStatus &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(base_health); FIELD(base_status); FIELD(base_shield);
    FIELD(outpost_health); FIELD(outpost_status);
    FIELD(enemy_base_health); FIELD(enemy_base_status); FIELD(enemy_base_shield);
    FIELD(enemy_outpost_health); FIELD(enemy_outpost_status);
    FIELD(total_damage_ally); FIELD(total_damage_enemy);
#undef FIELD
    QJsonArray health, bullets;
    for (int i = 0; i < v.robot_health_size(); ++i) health.append(double(v.robot_health(i)));
    for (int i = 0; i < v.robot_bullets_size(); ++i) bullets.append(double(v.robot_bullets(i)));
    o["robot_health"] = health;
    o["robot_bullets"] = bullets;
    o["message_type"] = "GlobalUnitStatus";
    o["base_status_name"] = v.has_base_status() ? baseStatus(v.base_status()) : QJsonValue(QJsonValue::Null);
    o["outpost_status_name"] = v.has_outpost_status() ? outpostStatus(v.outpost_status()) : QJsonValue(QJsonValue::Null);
    o["enemy_base_status_name"] = v.has_enemy_base_status() ? baseStatus(v.enemy_base_status()) : QJsonValue(QJsonValue::Null);
    o["enemy_outpost_status_name"] = v.has_enemy_outpost_status() ? outpostStatus(v.enemy_outpost_status()) : QJsonValue(QJsonValue::Null);
    return o;
}
QJsonObject json(const rm::GlobalLogisticsStatus &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(remaining_economy); FIELD(total_economy_obtained); FIELD(tech_level); FIELD(encryption_level);
#undef FIELD
    o["message_type"] = "GlobalLogisticsStatus";
    return o;
}
QJsonObject json(const rm::RobotStaticStatus &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(connection_state); FIELD(field_state); FIELD(alive_state);
    FIELD(robot_id); FIELD(robot_type);
    FIELD(performance_system_shooter); FIELD(performance_system_chassis);
    FIELD(level); FIELD(max_health); FIELD(max_heat); FIELD(max_power);
    FIELD(max_buffer_energy); FIELD(max_chassis_energy);
#undef FIELD
    o["heat_cooldown_rate"] = v.has_heat_cooldown_rate() ? QJsonValue(double(v.heat_cooldown_rate())) : QJsonValue(QJsonValue::Null);
    o["message_type"] = "RobotStaticStatus";
    o["robot_type_name"] = v.has_robot_type() ? robotType(v.robot_type()) : QJsonValue(QJsonValue::Null);
    o["connection_state_name"] = v.has_connection_state() ? connectionState(v.connection_state()) : QJsonValue(QJsonValue::Null);
    o["field_state_name"] = v.has_field_state() ? fieldState(v.field_state()) : QJsonValue(QJsonValue::Null);
    o["alive_state_name"] = v.has_alive_state() ? aliveState(v.alive_state()) : QJsonValue(QJsonValue::Null);
    return o;
}
QJsonObject json(const rm::RobotDynamicStatus &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(current_health); FIELD(current_chassis_energy); FIELD(current_buffer_energy);
    FIELD(current_experience); FIELD(experience_for_upgrade);
    FIELD(total_projectiles_fired); FIELD(remaining_ammo);
    FIELD(out_of_combat_countdown);
#undef FIELD
    o["current_heat"] = v.has_current_heat() ? QJsonValue(double(v.current_heat())) : QJsonValue(QJsonValue::Null);
    o["last_projectile_fire_rate"] = v.has_last_projectile_fire_rate() ? QJsonValue(double(v.last_projectile_fire_rate())) : QJsonValue(QJsonValue::Null);
    o["is_out_of_combat"] = v.has_is_out_of_combat() ? QJsonValue(v.is_out_of_combat()) : QJsonValue(QJsonValue::Null);
    o["can_remote_heal"] = v.has_can_remote_heal() ? QJsonValue(v.can_remote_heal()) : QJsonValue(QJsonValue::Null);
    o["can_remote_ammo"] = v.has_can_remote_ammo() ? QJsonValue(v.can_remote_ammo()) : QJsonValue(QJsonValue::Null);
    o["message_type"] = "RobotDynamicStatus";
    return o;
}
QJsonObject json(const rm::RobotRespawnStatus &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(total_respawn_progress); FIELD(current_respawn_progress); FIELD(gold_cost_for_respawn);
#undef FIELD
    o["is_pending_respawn"] = v.has_is_pending_respawn() ? QJsonValue(v.is_pending_respawn()) : QJsonValue(QJsonValue::Null);
    o["can_free_respawn"] = v.has_can_free_respawn() ? QJsonValue(v.can_free_respawn()) : QJsonValue(QJsonValue::Null);
    o["can_pay_for_respawn"] = v.has_can_pay_for_respawn() ? QJsonValue(v.can_pay_for_respawn()) : QJsonValue(QJsonValue::Null);
    o["message_type"] = "RobotRespawnStatus";
    if (v.has_total_respawn_progress() && v.has_current_respawn_progress())
        o["progress_text"] = QString("%1/%2").arg(v.current_respawn_progress()).arg(v.total_respawn_progress());
    return o;
}
QJsonObject json(const rm::Event &v) {
    QJsonObject o;
    o["event_id"] = v.has_event_id() ? QJsonValue(v.event_id()) : QJsonValue(QJsonValue::Null);
    o["param"] = QString::fromStdString(v.param());
    o["message_type"] = "Event";
    o["event_text"] = eventText(v);
    return o;
}
QJsonObject json(const rm::RobotInjuryStat &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(total_damage); FIELD(collision_damage); FIELD(small_projectile_damage);
    FIELD(large_projectile_damage); FIELD(dart_splash_damage); FIELD(module_offline_damage);
    FIELD(offline_damage); FIELD(penalty_damage); FIELD(server_kill_damage); FIELD(killer_id);
#undef FIELD
    o["message_type"] = "RobotInjuryStat";
    if (v.has_killer_id() && v.killer_id() != 0) o["killer_name"] = profile::name(int(v.killer_id()));
    return o;
}
QJsonObject json(const rm::PenaltyInfo &v) {
    QJsonObject o;
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(penalty_type); FIELD(penalty_effect_sec); FIELD(total_penalty_num);
#undef FIELD
    o["message_type"] = "PenaltyInfo";
    o["penalty_type_name"] = v.has_penalty_type() ? penaltyType(v.penalty_type()) : QJsonValue(QJsonValue::Null);
    return o;
}
}
