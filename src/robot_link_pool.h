#pragma once

#include "receiver.h"
#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QVector>

// 多机器人 MQTT 链路池：第一条链路接收全局域，其余链路只接收可按机器人编号归集的域。
// 链路之间相互独立，启动错开 200–400ms，避免同时重连压垮裁判端。
class RobotLinkPool : public QObject {
    Q_OBJECT
public:
    explicit RobotLinkPool(QObject *parent = nullptr);
    ~RobotLinkPool() override;

    bool start(const QString &host, int port, const QVector<int> &robotIds);
    void stop();
    bool isRunning() const { return running; }
    int primaryRobotId() const { return primaryId; }
    QVector<int> robotIds() const { return configuredIds; }
    quint64 receivedMessages() const;
    quint64 malformedMessages() const;
    int lastPayloadBytes() const;
    int lastQos() const;
    int subscribedTopicCount() const;
    bool linkReady(int robotId) const { return ready.value(robotId, false); }

signals:
    // 兼容主窗口现有连接状态栏；只转发主链路状态。
    void stateChanged(QString text, bool ready);
    void linkChanged(int robotId, bool ready, QString text, int subscribedTopics);
    void received(rm::GameStatus status);
    void receivedInfo(rm::GameStatus status, int payloadBytes, int qos);
    void receivedUnitStatus(rm::GlobalUnitStatus value);
    void receivedLogistics(rm::GlobalLogisticsStatus value);
    void receivedSpecialMechanism(rm::GlobalSpecialMechanism value);
    void receivedEvent(rm::Event value);
    void receivedInjury(int robotId, rm::RobotInjuryStat value);
    void receivedRespawn(int robotId, rm::RobotRespawnStatus value);
    void receivedStatic(int robotId, rm::RobotStaticStatus value);
    void receivedDynamic(int robotId, rm::RobotDynamicStatus value);
    void receivedModule(int robotId, rm::RobotModuleStatus value);
    void receivedPosition(int robotId, rm::RobotPosition value);
    void receivedBuff(int robotId, rm::Buff value);
    void receivedPenalty(rm::PenaltyInfo value);
    void receivedRadar(rm::RadarInfoToClient value);

private:
    QHash<int, StatusReceiver *> links;
    QHash<int, bool> ready;
    QVector<int> configuredIds;
    QVector<int> pendingIds;
    QString hostName;
    int brokerPort = 0;
    int primaryId = 0;
    int pendingIndex = 0;
    bool running = false;
    quint64 generation = 0;
    QElapsedTimer clock;
    QHash<QString, qint64> recentEventAt;
    void startNext(quint64 expectedGeneration);
    void wire(int robotId, StatusReceiver *receiver);
};
