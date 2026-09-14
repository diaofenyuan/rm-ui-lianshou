#include "status.h"
#include <QJsonArray>
#include <QStringList>

namespace status {
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
}
