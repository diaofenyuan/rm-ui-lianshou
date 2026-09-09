#include "status.h"
#include <QStringList>

namespace status {
QString stage(quint32 v) {
    static const QStringList labels = {"未开始比赛", "准备阶段", "裁判系统自检", "五秒倒计时", "比赛中", "比赛结算中"};
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
QJsonObject json(const rm::GameStatus &v) {
    QJsonObject o;
    // 缺失字段输出 null，避免把未发送与数值 0 混淆；每条消息作为独立快照。
#define FIELD(name) o[#name] = v.has_##name() ? QJsonValue(double(v.name())) : QJsonValue(QJsonValue::Null)
    FIELD(current_round); FIELD(total_rounds); FIELD(red_score); FIELD(blue_score);
    FIELD(current_stage); FIELD(stage_countdown_sec); FIELD(stage_elapsed_sec);
    FIELD(game_result); FIELD(end_reason);
#undef FIELD
    o["is_paused"] = v.has_is_paused() ? QJsonValue(v.is_paused()) : QJsonValue(QJsonValue::Null);
    o["stage_name"] = v.has_current_stage() ? stage(v.current_stage()) : "未提供";
    return o;
}
}
