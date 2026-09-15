#pragma once
#include "game_status.pb.h"
#include "rm_messages.pb.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace status {
QString stage(quint32 value);
QString result(quint32 value);
QString reason(quint32 value);
QString duration(qint32 seconds);
bool isSettlement(const rm::GameStatus &value);
QStringList warnings(const rm::GameStatus &value);
QJsonObject json(const rm::GameStatus &value);
QStringList changes(const rm::GameStatus &before, const rm::GameStatus &after);

// RM2026 V2.0.0 新增数据域的可读文案；未知枚举一律保留数值。
QString robotType(quint32 value);          // 附录二编号：1 英雄、2 工程、3/4/5 步兵、6 空中、7 哨兵、8 飞镖、9 雷达
QString connectionState(quint32 value);    // RobotStaticStatus：0 未连接 / 1 已连接
QString fieldState(quint32 value);         // RobotStaticStatus：0 已上场 / 1 未上场
QString aliveState(quint32 value);         // RobotStaticStatus：0 未知 / 1 存活 / 2 战亡
QString moduleState(quint32 value);        // RobotModuleStatus：0 离线 / 1 在线 / 2 安装不规范视为离线
QString baseStatus(quint32 value);         // GlobalUnitStatus：0 无敌 / 1 护甲未展开 / 2 护甲展开
QString outpostStatus(quint32 value);      // GlobalUnitStatus：0 无敌 / 1–2 存活 / 3–5 击毁与重建
QString penaltyType(quint32 value);        // PenaltyInfo：1 黄牌 / 3 红牌 / 4 超功率 / 5 超热量 / 6 超射速 等
QString buffType(quint32 value);           // Buff：1 攻击 / 2 防御 / 3 热量冷却 / 4 底盘功率 / 5 回血 / 6 兑换发弹量
QString eventText(const rm::Event &value); // event_id 1–15 的中文事件描述，未知编号保留数值
QString mechanismText(quint32 id, qint32 seconds); // GlobalSpecialMechanism：1 己方堡垒被占 / 2 对方堡垒被占

QJsonObject json(const rm::GlobalUnitStatus &value);
QJsonObject json(const rm::GlobalLogisticsStatus &value);
QJsonObject json(const rm::RobotStaticStatus &value);
QJsonObject json(const rm::RobotDynamicStatus &value);
QJsonObject json(const rm::RobotRespawnStatus &value);
QJsonObject json(const rm::Event &value);
QJsonObject json(const rm::RobotInjuryStat &value);
QJsonObject json(const rm::PenaltyInfo &value);
}
Q_DECLARE_METATYPE(rm::GameStatus)
