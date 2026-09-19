#pragma once
#include "rm_messages.pb.h"
#include "status.h"
#include <MQTTAsync.h>
#include <QObject>
#include <QStringList>
#include <QTimer>

// 按 topic 解析并发出类型化信号；默认订阅 RM2026 V2.0.0 表 2-1 全部 topic，
// 也可传入单机域子集供 RobotLinkPool 使用；断线约 2 秒（带抖动）后重新订阅。
class StatusReceiver : public QObject {
    Q_OBJECT
public:
    explicit StatusReceiver(QObject *parent = nullptr);
    ~StatusReceiver();
    // 返回值只表示本地 MQTT 客户端是否创建成功；网络连接仍通过 stateChanged 异步报告。
    bool start(const QString &host, int port, const QString &robotId,
        const QStringList &topics = {});
    void stop();
    static QStringList allTopics();
    // 非主链路只接收能按 robot_id 归集的单机器人域，避免全局 topic 被重复消费。
    static QStringList singleRobotTopics();
    int subscribedTopicCount() const { return subscribedCount; }
    quint64 receivedMessages = 0;
    quint64 malformedMessages = 0;
    int lastPayloadBytes = 0;
    int lastQos = -1;
signals:
    void received(rm::GameStatus status);
    // 在保留 received 兼容性的同时提供审计元数据，便于独立接收器落盘和排障。
    void receivedInfo(rm::GameStatus status, int payloadBytes, int qos);
    void receivedUnitStatus(rm::GlobalUnitStatus value);
    void receivedLogistics(rm::GlobalLogisticsStatus value);
    void receivedSpecialMechanism(rm::GlobalSpecialMechanism value);
    void receivedEvent(rm::Event value);
    void receivedInjury(rm::RobotInjuryStat value);
    void receivedRespawn(rm::RobotRespawnStatus value);
    void receivedStatic(rm::RobotStaticStatus value);
    void receivedDynamic(rm::RobotDynamicStatus value);
    void receivedModule(rm::RobotModuleStatus value);
    void receivedPosition(rm::RobotPosition value);
    void receivedBuff(rm::Buff value);
    void receivedPenalty(rm::PenaltyInfo value);
    void receivedRadar(rm::RadarInfoToClient value);
    void stateChanged(QString text, bool subscribed);
private:
    MQTTAsync client = nullptr;
    QTimer retry;
    int pendingSubscriptions = 0;
    int subscribedCount = 0;
    QStringList topicFilter;
    void connectBroker();
    void scheduleRetry();
    void subscribe();
    void dispatch(const QByteArray &topic, const QByteArray &payload, int payloadBytes, int qos);
    template <typename T> void emitParsed(const QByteArray &payload, void (StatusReceiver::*signal)(T));
    void reportMalformed();
    static void connected(void *, MQTTAsync_successData *);
    static void failed(void *, MQTTAsync_failureData *);
    static void lost(void *, char *);
    static void subscribed(void *, MQTTAsync_successData *);
    static void subscriptionFailed(void *, MQTTAsync_failureData *);
    static int message(void *, char *, int, MQTTAsync_message *);
};
