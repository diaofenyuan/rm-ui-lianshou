#pragma once
#include "status.h"
#include <MQTTAsync.h>
#include <QObject>
#include <QTimer>

class StatusReceiver : public QObject {
    Q_OBJECT
public:
    explicit StatusReceiver(QObject *parent = nullptr);
    ~StatusReceiver();
    // 返回值只表示本地 MQTT 客户端是否创建成功；网络连接仍通过 stateChanged 异步报告。
    bool start(const QString &host, int port, const QString &robotId);
    void stop();
    quint64 receivedMessages = 0;
    quint64 malformedMessages = 0;
    int lastPayloadBytes = 0;
    int lastQos = -1;
signals:
    void received(rm::GameStatus status);
    // 在保留 received 兼容性的同时提供审计元数据，便于独立接收器落盘和排障。
    void receivedInfo(rm::GameStatus status, int payloadBytes, int qos);
    void stateChanged(QString text, bool subscribed);
private:
    MQTTAsync client = nullptr;
    QTimer retry;
    void connectBroker();
    void subscribe();
    static void connected(void *, MQTTAsync_successData *);
    static void failed(void *, MQTTAsync_failureData *);
    static void lost(void *, char *);
    static void subscribed(void *, MQTTAsync_successData *);
    static void subscriptionFailed(void *, MQTTAsync_failureData *);
    static int message(void *, char *, int, MQTTAsync_message *);
};
