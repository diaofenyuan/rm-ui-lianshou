#include "match_state.h"
#include "operator_profile.h"
#include <QPair>
#include <QSet>
#include <QTime>
#include <optional>

namespace {
constexpr qint64 kFastStaleMs = 1500;   // GameStatus 5Hz、RobotDynamicStatus 10Hz
constexpr qint64 kSlowStaleMs = 3000;   // 1Hz 域
constexpr int kEventLogLimit = 200;
constexpr int kBuffLimit = 32;
constexpr int kTimelineLimit = 300;

// GlobalUnitStatus.robot_health 槽位编号（协议 2.2.4 第 53 页数据编号 11）：
// 按己方 1/2/3/4/7 号、对方 1/2/3/4/7 号的顺序发送，即前 5 项我方、后 5 项对方。
// 注意与 2.2.19 雷达的 robot_info 顺序（前 6 项对方、后 6 项己方）相反，不要互相套用。
constexpr int kHealthSlots[5] = {1, 2, 3, 4, 7};
QString slotName(int index) {
    const bool ally = index < 5;
    const int number = kHealthSlots[index % 5];
    return QString("%1 %2 号%3").arg(ally ? "我方" : "对方").arg(number)
        .arg(QString::fromUtf8(profile::roles[number - 1].name));
}

// RobotModuleStatus 各模块字段：字段名与取值，未提供的字段返回 nullopt 以便跳过比较。
using ModuleField = QPair<QString, std::optional<quint32>>;
QVector<ModuleField> moduleStates(const rm::RobotModuleStatus &v) {
    const auto field = [](const QString &name, bool present, quint32 value) {
        return qMakePair(name, present ? std::optional<quint32>(value) : std::nullopt);
    };
    return {field("电源管理", v.has_power_manager(), v.power_manager()),
        field("RFID", v.has_rfid(), v.rfid()),
        field("灯带", v.has_light_strip(), v.light_strip()),
        field("17mm 发射机构", v.has_small_shooter(), v.small_shooter()),
        field("42mm 发射机构", v.has_big_shooter(), v.big_shooter()),
        field("定位模块", v.has_uwb(), v.uwb()),
        field("装甲", v.has_armor(), v.armor()),
        field("图传", v.has_video_transmission(), v.video_transmission()),
        field("超级电容", v.has_capacitor(), v.capacitor()),
        field("主控", v.has_main_controller(), v.main_controller()),
        field("激光检测模块", v.has_laser_detection_module(), v.laser_detection_module())};
}
}

MatchState::MatchState(QObject *parent) : QObject(parent) { clock.start(); }
void MatchState::reset() {
    game = {}; unitStatus = {}; logistics = {}; mechanisms = {}; respawn = {}; injury = {};
    robotStatic = {}; robotDynamic = {}; robotModule = {}; position = {}; penalty = {}; radar = {};
    gameAt = unitStatusAt = logisticsAt = mechanismsAt = respawnAt = injuryAt = -1;
    robotStaticAt = robotDynamicAt = robotModuleAt = positionAt = radarAt = penaltyAt = -1;
    eventLog.clear(); activeBuffs.clear(); entries.clear(); robots.clear(); linkIds.clear();
    emit stateReset();
}
qint64 MatchState::stamp(qint64 &target) { return target = clock.elapsed(); }

qint64 MatchState::ageMs(Domain domain) const {
    qint64 at = -1;
    switch (domain) {
        case Domain::Game: at = gameAt; break;
        case Domain::UnitStatus: at = unitStatusAt; break;
        case Domain::Logistics: at = logisticsAt; break;
        case Domain::SpecialMechanism: at = mechanismsAt; break;
        case Domain::Respawn: at = respawnAt; break;
        case Domain::Injury: at = injuryAt; break;
        case Domain::RobotStatic: at = robotStaticAt; break;
        case Domain::RobotDynamic: at = robotDynamicAt; break;
        case Domain::RobotModule: at = robotModuleAt; break;
        case Domain::Position: at = positionAt; break;
        case Domain::Radar: at = radarAt; break;
        case Domain::Penalty: at = penaltyAt; break;
    }
    return at < 0 ? -1 : clock.elapsed() - at;
}
bool MatchState::isStale(Domain domain) const {
    const qint64 age = ageMs(domain);
    if (age < 0) return true;
    switch (domain) {
        case Domain::Game:
        case Domain::RobotDynamic: return age > kFastStaleMs;
        default: return age > kSlowStaleMs;
    }
}

MatchState::RobotSnapshot &MatchState::snapshot(int robotId) { return robots[robotId]; }
const MatchState::RobotSnapshot *MatchState::robot(int robotId) const {
    const auto it = robots.constFind(robotId);
    return it == robots.cend() ? nullptr : &it.value();
}
qint64 MatchState::robotAgeMs(int robotId, Domain domain) const {
    const auto *value = robot(robotId);
    if (!value) return -1;
    qint64 at = -1;
    switch (domain) {
        case Domain::Respawn: at = value->respawnAt; break;
        case Domain::Injury: at = value->injuryAt; break;
        case Domain::RobotStatic: at = value->robotStaticAt; break;
        case Domain::RobotDynamic: at = value->robotDynamicAt; break;
        case Domain::RobotModule: at = value->robotModuleAt; break;
        case Domain::Position: at = value->positionAt; break;
        default: break;
    }
    return at < 0 ? -1 : clock.elapsed() - at;
}
bool MatchState::isRobotStale(int robotId, Domain domain) const {
    const qint64 age = robotAgeMs(robotId, domain);
    if (age < 0) return true;
    return domain == Domain::RobotDynamic ? age > kFastStaleMs : age > kSlowStaleMs;
}

void MatchState::appendTimeline(Category category, const QString &text, bool alert) {
    if (text.isEmpty()) return;
    TimelineEntry entry;
    entry.at = clock.elapsed();
    entry.stamp = QTime::currentTime().toString("HH:mm:ss");
    entry.category = category;
    entry.text = text;
    entry.alert = alert;
    entries.append(entry);
    while (entries.size() > kTimelineLimit) entries.removeFirst();
    emit timelineChanged();
}

void MatchState::recordHealthChanges(const rm::GlobalUnitStatus &value) {
    if (unitStatusAt < 0) return;
    const int slotCount = qMin(value.robot_health_size(), unitStatus.robot_health_size());
    for (int i = 0; i < slotCount; ++i) {
        const quint32 before = unitStatus.robot_health(i), after = value.robot_health(i);
        if (before == after) continue;
        if (before > 0 && after == 0) appendTimeline(Category::Robot, QString("%1 被击毁").arg(slotName(i)), true);
        else if (before == 0 && after > 0) appendTimeline(Category::Robot, QString("%1 复活").arg(slotName(i)));
    }
}

void MatchState::applyGame(const rm::GameStatus &value) {
    if (gameAt >= 0)
        for (const QString &text : status::changes(game, value))
            appendTimeline(Category::Match, text, text.contains("结算") || text.contains("暂停"));
    game = value; stamp(gameAt); emit gameChanged();
}
void MatchState::applyUnitStatus(const rm::GlobalUnitStatus &value) {
    recordHealthChanges(value);
    unitStatus = value; stamp(unitStatusAt); emit unitStatusChanged();
}
void MatchState::applyLogistics(const rm::GlobalLogisticsStatus &value) { logistics = value; stamp(logisticsAt); emit logisticsChanged(); }
void MatchState::applySpecialMechanism(const rm::GlobalSpecialMechanism &value) {
    if (mechanismsAt >= 0) {
        // 机制每秒重发，只记录出现与结束，避免刷屏。
        QSet<int> before, after;
        for (int i = 0; i < mechanisms.mechanism_id_size(); ++i) before.insert(int(mechanisms.mechanism_id(i)));
        for (int i = 0; i < value.mechanism_id_size(); ++i) after.insert(int(value.mechanism_id(i)));
        for (int id : after) {
            if (before.contains(id)) continue;
            qint32 seconds = 0;
            for (int i = 0; i < value.mechanism_id_size(); ++i)
                if (int(value.mechanism_id(i)) == id && i < value.mechanism_time_sec_size())
                    seconds = value.mechanism_time_sec(i);
            appendTimeline(Category::Mechanism, status::mechanismText(quint32(id), seconds), true);
        }
        for (int id : before)
            if (!after.contains(id)) appendTimeline(Category::Mechanism, QString("特殊机制 %1 结束").arg(id));
    }
    mechanisms = value; stamp(mechanismsAt); emit specialMechanismChanged();
}
void MatchState::mirrorInjury(const rm::RobotInjuryStat &value) {
    injury = value; stamp(injuryAt); emit injuryChanged();
}
void MatchState::applyInjury(const rm::RobotInjuryStat &value) {
    if (primaryRobotId > 0) applyInjury(primaryRobotId, value); else mirrorInjury(value);
}
void MatchState::applyInjury(int robotId, const rm::RobotInjuryStat &value) {
    auto &current = snapshot(robotId); current.injury = value; stamp(current.injuryAt);
    if (isPrimary(robotId)) mirrorInjury(value);
    emit robotsChanged();
}
void MatchState::mirrorRespawn(const rm::RobotRespawnStatus &value) {
    if (respawnAt >= 0 && respawn.has_is_pending_respawn() && value.has_is_pending_respawn()) {
        if (!respawn.is_pending_respawn() && value.is_pending_respawn())
            appendTimeline(Category::Robot, "进入复活读条", true);
        else if (respawn.is_pending_respawn() && !value.is_pending_respawn())
            appendTimeline(Category::Robot, "复活读条结束");
    }
    respawn = value; stamp(respawnAt); emit respawnChanged();
}
void MatchState::applyRespawn(const rm::RobotRespawnStatus &value) {
    if (primaryRobotId > 0) applyRespawn(primaryRobotId, value); else mirrorRespawn(value);
}
void MatchState::applyRespawn(int robotId, const rm::RobotRespawnStatus &value) {
    auto &current = snapshot(robotId); current.respawn = value; stamp(current.respawnAt);
    if (isPrimary(robotId)) mirrorRespawn(value);
    emit robotsChanged();
}
void MatchState::mirrorStatic(const rm::RobotStaticStatus &value) {
    robotStatic = value; stamp(robotStaticAt); emit robotStaticChanged();
}
void MatchState::applyStatic(const rm::RobotStaticStatus &value) {
    if (primaryRobotId > 0) applyStatic(primaryRobotId, value); else mirrorStatic(value);
}
void MatchState::applyStatic(int robotId, const rm::RobotStaticStatus &value) {
    auto &current = snapshot(robotId); current.robotStatic = value; stamp(current.robotStaticAt);
    if (isPrimary(robotId)) mirrorStatic(value);
    emit robotsChanged();
}
void MatchState::mirrorDynamic(const rm::RobotDynamicStatus &value) {
    robotDynamic = value; stamp(robotDynamicAt); emit robotDynamicChanged();
}
void MatchState::applyDynamic(const rm::RobotDynamicStatus &value) {
    if (primaryRobotId > 0) applyDynamic(primaryRobotId, value); else mirrorDynamic(value);
}
void MatchState::applyDynamic(int robotId, const rm::RobotDynamicStatus &value) {
    auto &current = snapshot(robotId); current.robotDynamic = value; stamp(current.robotDynamicAt);
    if (isPrimary(robotId)) mirrorDynamic(value);
    emit robotsChanged();
}
void MatchState::mirrorModule(const rm::RobotModuleStatus &value) {
    if (robotModuleAt >= 0) {
        const auto before = moduleStates(robotModule), after = moduleStates(value);
        for (int i = 0; i < before.size() && i < after.size(); ++i) {
            if (!before[i].second || !after[i].second) continue;
            const bool onlineBefore = *before[i].second == 1, onlineAfter = *after[i].second == 1;
            if (onlineBefore && !onlineAfter) appendTimeline(Category::Robot, QString("模块离线：%1").arg(before[i].first), true);
            else if (!onlineBefore && onlineAfter) appendTimeline(Category::Robot, QString("模块恢复：%1").arg(before[i].first));
        }
    }
    robotModule = value; stamp(robotModuleAt); emit robotModuleChanged();
}
void MatchState::applyModule(const rm::RobotModuleStatus &value) {
    if (primaryRobotId > 0) applyModule(primaryRobotId, value); else mirrorModule(value);
}
void MatchState::applyModule(int robotId, const rm::RobotModuleStatus &value) {
    auto &current = snapshot(robotId); current.robotModule = value; stamp(current.robotModuleAt);
    if (isPrimary(robotId)) mirrorModule(value);
    emit robotsChanged();
}
void MatchState::mirrorPosition(const rm::RobotPosition &value) {
    position = value; stamp(positionAt); emit positionChanged();
}
void MatchState::applyPosition(const rm::RobotPosition &value) {
    if (primaryRobotId > 0) applyPosition(primaryRobotId, value);
    else { ++positionMessages; mirrorPosition(value); }
}
void MatchState::applyPosition(int robotId, const rm::RobotPosition &value) {
    auto &current = snapshot(robotId); current.position = value; stamp(current.positionAt);
    ++positionMessages;
    if (isPrimary(robotId)) mirrorPosition(value);
    emit robotsChanged();
}
void MatchState::applyPenalty(const rm::PenaltyInfo &value) {
    QString text = "判罚";
    if (value.has_penalty_type()) text += QString("：%1").arg(status::penaltyType(value.penalty_type()));
    if (value.has_penalty_effect_sec()) text += QString("，持续 %1 秒").arg(value.penalty_effect_sec());
    if (value.has_total_penalty_num()) text += QString("，累计 %1 次").arg(value.total_penalty_num());
    appendTimeline(Category::Penalty, text, true);
    penalty = value; ++penaltyMessages; stamp(penaltyAt); emit penaltyChanged();
}
void MatchState::applyRadar(const rm::RadarInfoToClient &value) { radar = value; ++radarMessages; stamp(radarAt); emit radarChanged(); }

void MatchState::applyEvent(const rm::Event &value) {
    TimedEvent entry; entry.at = clock.elapsed(); entry.event = value;
    eventLog.append(entry);
    ++eventMessages;
    while (eventLog.size() > kEventLogLimit) eventLog.removeFirst();
    // 前哨站、基地、飞镖与空中支援类事件需要提醒；击杀、能量机关等只入时间线。
    const int id = value.has_event_id() ? value.event_id() : 0;
    bool alert = false;
    switch (id) {
    case 2: case 7: case 9: case 10: case 11: case 12: case 13: case 14: alert = true; break;
    default: break;
    }
    appendTimeline(Category::Event, status::eventText(value), alert);
    emit eventAppended();
}
void MatchState::applyBuff(const rm::Buff &value) {
    if (primaryRobotId > 0) {
        const int id = value.has_robot_id() ? int(value.robot_id()) : primaryRobotId;
        applyBuff(id, value);
        return;
    }
    // 同一机器人同一类型的 Buff 以最新消息为准；剩余时间归零表示失效，直接移除。
    const bool expired = value.has_buff_left_time() && value.buff_left_time() == 0;
    for (int i = 0; i < activeBuffs.size(); ++i) {
        const auto &existing = activeBuffs[i].buff;
        if (existing.has_robot_id() && value.has_robot_id() && existing.robot_id() != value.robot_id()) continue;
        if (existing.has_buff_type() && value.has_buff_type() && existing.buff_type() != value.buff_type()) continue;
        if (expired) activeBuffs.removeAt(i); else activeBuffs[i] = {clock.elapsed(), value};
        emit buffsChanged();
        return;
    }
    if (expired) return;
    TimedBuff entry; entry.at = clock.elapsed(); entry.buff = value;
    activeBuffs.append(entry);
    while (activeBuffs.size() > kBuffLimit) activeBuffs.removeFirst();
    emit buffsChanged();
}
void MatchState::applyBuff(int robotId, const rm::Buff &value) {
    auto &current = snapshot(robotId); current.buff = value; stamp(current.buffAt);
    const bool expired = value.has_buff_left_time() && value.buff_left_time() == 0;
    for (int i = 0; i < activeBuffs.size(); ++i) {
        const auto &existing = activeBuffs[i].buff;
        const bool sameRobot = existing.has_robot_id() && value.has_robot_id()
            ? existing.robot_id() == value.robot_id() : (!existing.has_robot_id() || !value.has_robot_id());
        if (!sameRobot || (existing.has_buff_type() && value.has_buff_type()
            && existing.buff_type() != value.buff_type())) continue;
        if (expired) activeBuffs.removeAt(i); else activeBuffs[i] = {clock.elapsed(), value};
        emit buffsChanged(); emit robotsChanged();
        return;
    }
    if (!expired) {
        TimedBuff entry; entry.at = clock.elapsed(); entry.buff = value;
        activeBuffs.append(entry);
        while (activeBuffs.size() > kBuffLimit) activeBuffs.removeFirst();
        emit buffsChanged();
    }
    emit robotsChanged();
}

// robot_health 前 5 项是我方：越界或消息只带部分槽位时返回空值，由界面显示"未提供"，
// 不允许回落到相邻阵营的槽位。
std::optional<quint32> MatchState::allyHealth(int index) const {
    if (index < 0 || index >= kSideHealthCount || index >= unitStatus.robot_health_size())
        return std::nullopt;
    return unitStatus.robot_health(index);
}
std::optional<quint32> MatchState::enemyHealth(int index) const {
    const int offset = index + kSideHealthCount;
    if (index < 0 || index >= kSideHealthCount || offset >= unitStatus.robot_health_size())
        return std::nullopt;
    return unitStatus.robot_health(offset);
}
// 2.2.19 雷达的 robot_info 前 6 项是对方，后 6 项是己方。
std::optional<rm::RadarSingleRobotInfo> MatchState::enemyRadar(int index) const {
    if (index < 0 || index >= kSideRadarCount || index >= radar.robot_info_size()) return std::nullopt;
    return radar.robot_info(index);
}
std::optional<rm::RadarSingleRobotInfo> MatchState::allyRadar(int index) const {
    const int offset = index + kSideRadarCount;
    if (index < 0 || index >= kSideRadarCount || offset >= radar.robot_info_size()) return std::nullopt;
    return radar.robot_info(offset);
}
