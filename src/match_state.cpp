#include "match_state.h"

namespace {
constexpr qint64 kFastStaleMs = 1500;   // GameStatus 5Hz、RobotDynamicStatus 10Hz
constexpr qint64 kSlowStaleMs = 3000;   // 1Hz 域
constexpr int kEventLogLimit = 200;
constexpr int kBuffLimit = 32;
}

MatchState::MatchState(QObject *parent) : QObject(parent) { clock.start(); }
void MatchState::reset() {
    game = {}; unitStatus = {}; logistics = {}; mechanisms = {}; respawn = {}; injury = {};
    robotStatic = {}; robotDynamic = {}; robotModule = {}; position = {}; penalty = {}; radar = {};
    gameAt = unitStatusAt = logisticsAt = mechanismsAt = respawnAt = injuryAt = -1;
    robotStaticAt = robotDynamicAt = robotModuleAt = positionAt = radarAt = penaltyAt = -1;
    eventLog.clear(); activeBuffs.clear();
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

void MatchState::applyGame(const rm::GameStatus &value) { game = value; stamp(gameAt); emit gameChanged(); }
void MatchState::applyUnitStatus(const rm::GlobalUnitStatus &value) { unitStatus = value; stamp(unitStatusAt); emit unitStatusChanged(); }
void MatchState::applyLogistics(const rm::GlobalLogisticsStatus &value) { logistics = value; stamp(logisticsAt); emit logisticsChanged(); }
void MatchState::applySpecialMechanism(const rm::GlobalSpecialMechanism &value) { mechanisms = value; stamp(mechanismsAt); emit specialMechanismChanged(); }
void MatchState::applyInjury(const rm::RobotInjuryStat &value) { injury = value; stamp(injuryAt); emit injuryChanged(); }
void MatchState::applyRespawn(const rm::RobotRespawnStatus &value) { respawn = value; stamp(respawnAt); emit respawnChanged(); }
void MatchState::applyStatic(const rm::RobotStaticStatus &value) { robotStatic = value; stamp(robotStaticAt); emit robotStaticChanged(); }
void MatchState::applyDynamic(const rm::RobotDynamicStatus &value) { robotDynamic = value; stamp(robotDynamicAt); emit robotDynamicChanged(); }
void MatchState::applyModule(const rm::RobotModuleStatus &value) { robotModule = value; stamp(robotModuleAt); emit robotModuleChanged(); }
void MatchState::applyPosition(const rm::RobotPosition &value) { position = value; ++positionMessages; stamp(positionAt); emit positionChanged(); }
void MatchState::applyPenalty(const rm::PenaltyInfo &value) { penalty = value; ++penaltyMessages; stamp(penaltyAt); emit penaltyChanged(); }
void MatchState::applyRadar(const rm::RadarInfoToClient &value) { radar = value; ++radarMessages; stamp(radarAt); emit radarChanged(); }

void MatchState::applyEvent(const rm::Event &value) {
    TimedEvent entry; entry.at = clock.elapsed(); entry.event = value;
    eventLog.append(entry);
    ++eventMessages;
    while (eventLog.size() > kEventLogLimit) eventLog.removeFirst();
    emit eventAppended();
}
void MatchState::applyBuff(const rm::Buff &value) {
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

std::optional<quint32> MatchState::allyHealth(int index) const {
    const int offset = index + kSideHealthCount;
    if (index < 0 || offset >= unitStatus.robot_health_size()) return std::nullopt;
    return unitStatus.robot_health(offset);
}
std::optional<quint32> MatchState::enemyHealth(int index) const {
    if (index < 0 || index >= unitStatus.robot_health_size()) return std::nullopt;
    return unitStatus.robot_health(index);
}
std::optional<rm::RadarSingleRobotInfo> MatchState::enemyRadar(int index) const {
    if (index < 0 || index >= radar.robot_info_size()) return std::nullopt;
    return radar.robot_info(index);
}
std::optional<rm::RadarSingleRobotInfo> MatchState::allyRadar(int index) const {
    const int offset = index + kSideRadarCount;
    if (index < 0 || offset >= radar.robot_info_size()) return std::nullopt;
    return radar.robot_info(offset);
}
