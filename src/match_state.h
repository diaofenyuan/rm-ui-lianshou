#pragma once
#include "rm_messages.pb.h"
#include "status.h"
#include <QElapsedTimer>
#include <QObject>
#include <QVector>
#include <optional>

// 中心化比赛状态模型：汇总全部 MQTT 数据域的最新快照。
// 只负责存储、接收时间戳与过期判定，不做协议解读（文案见 status::），
// 也不持有 UI 控件；UI 通过域级信号做局部刷新。
class MatchState : public QObject {
    Q_OBJECT
public:
    explicit MatchState(QObject *parent = nullptr);
    // 切换机器人或断开时清空全部状态。
    void reset();

    // 各数据域的时效阈值（毫秒）：5/10Hz 域 1.5 秒，1Hz 域 3 秒；
    // Event/Buff/Radar 为触发式，不参与过期判定。
    enum class Domain { Game, UnitStatus, Logistics, SpecialMechanism, Respawn,
        Injury, RobotStatic, RobotDynamic, RobotModule, Position, Penalty };
    qint64 ageMs(Domain domain) const;   // 距该域最近一次更新的毫秒数；从未收到返回 -1
    bool isStale(Domain domain) const;   // 从未收到或超过该域阈值

    rm::GameStatus game;
    rm::GlobalUnitStatus unitStatus;
    rm::GlobalLogisticsStatus logistics;
    rm::GlobalSpecialMechanism mechanisms;
    rm::RobotRespawnStatus respawn;
    rm::RobotInjuryStat injury;
    rm::RobotStaticStatus robotStatic;
    rm::RobotDynamicStatus robotDynamic;
    rm::RobotModuleStatus robotModule;
    rm::RobotPosition position;
    rm::PenaltyInfo penalty;
    rm::RadarInfoToClient radar;

    struct TimedEvent { qint64 at = 0; rm::Event event; };
    struct TimedBuff { qint64 at = 0; rm::Buff buff; };
    const QVector<TimedEvent> &events() const { return eventLog; }
    const QVector<TimedBuff> &buffs() const { return activeBuffs; }

    // GlobalUnitStatus.robot_health 固定顺序（协议 2.2.4）：索引 0–4 为己方 1/2/3/4/7 号，5–9 为对方 1/2/3/4/7 号。
    static constexpr int kSideHealthCount = 5;
    std::optional<quint32> allyHealth(int index) const;   // index 0–4
    std::optional<quint32> enemyHealth(int index) const;  // index 0–4
    // RadarInfoToClient.robot_info 固定顺序（协议 2.2.19）：索引 0–5 为对方 1/2/3/4/6/7 号，6–11 为己方。
    static constexpr int kSideRadarCount = 6;
    std::optional<rm::RadarSingleRobotInfo> enemyRadar(int index) const;  // index 0–5
    std::optional<rm::RadarSingleRobotInfo> allyRadar(int index) const;   // index 0–5

public slots:
    void applyGame(const rm::GameStatus &value);
    void applyUnitStatus(const rm::GlobalUnitStatus &value);
    void applyLogistics(const rm::GlobalLogisticsStatus &value);
    void applySpecialMechanism(const rm::GlobalSpecialMechanism &value);
    void applyEvent(const rm::Event &value);
    void applyInjury(const rm::RobotInjuryStat &value);
    void applyRespawn(const rm::RobotRespawnStatus &value);
    void applyStatic(const rm::RobotStaticStatus &value);
    void applyDynamic(const rm::RobotDynamicStatus &value);
    void applyModule(const rm::RobotModuleStatus &value);
    void applyPosition(const rm::RobotPosition &value);
    void applyBuff(const rm::Buff &value);
    void applyPenalty(const rm::PenaltyInfo &value);
    void applyRadar(const rm::RadarInfoToClient &value);

signals:
    void stateReset();
    void gameChanged();
    void unitStatusChanged();
    void logisticsChanged();
    void specialMechanismChanged();
    void respawnChanged();
    void injuryChanged();
    void robotStaticChanged();
    void robotDynamicChanged();
    void robotModuleChanged();
    void positionChanged();
    void penaltyChanged();
    void radarChanged();
    void buffsChanged();
    void eventAppended();

private:
    QElapsedTimer clock;
    qint64 gameAt = -1, unitStatusAt = -1, logisticsAt = -1, mechanismsAt = -1, respawnAt = -1,
        injuryAt = -1, robotStaticAt = -1, robotDynamicAt = -1, robotModuleAt = -1,
        positionAt = -1, penaltyAt = -1;
    QVector<TimedEvent> eventLog;
    QVector<TimedBuff> activeBuffs;
    qint64 stamp(qint64 &target);
};
Q_DECLARE_METATYPE(rm::GlobalUnitStatus)
Q_DECLARE_METATYPE(rm::GlobalLogisticsStatus)
Q_DECLARE_METATYPE(rm::GlobalSpecialMechanism)
Q_DECLARE_METATYPE(rm::Event)
Q_DECLARE_METATYPE(rm::RobotInjuryStat)
Q_DECLARE_METATYPE(rm::RobotRespawnStatus)
Q_DECLARE_METATYPE(rm::RobotStaticStatus)
Q_DECLARE_METATYPE(rm::RobotDynamicStatus)
Q_DECLARE_METATYPE(rm::RobotModuleStatus)
Q_DECLARE_METATYPE(rm::RobotPosition)
Q_DECLARE_METATYPE(rm::Buff)
Q_DECLARE_METATYPE(rm::PenaltyInfo)
Q_DECLARE_METATYPE(rm::RadarInfoToClient)
Q_DECLARE_METATYPE(rm::RadarSingleRobotInfo)
