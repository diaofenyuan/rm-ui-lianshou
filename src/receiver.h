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
    void start(const QString &host, int port, const QString &robotId);
    void stop();
signals:
    void received(rm::GameStatus status);
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
