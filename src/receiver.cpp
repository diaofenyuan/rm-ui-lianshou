#include "receiver.h"
#include <QMetaObject>
#include <QCoreApplication>
#include <QEvent>

StatusReceiver::StatusReceiver(QObject *p) : QObject(p) {
    retry.setInterval(2000); retry.setSingleShot(true);
    connect(&retry, &QTimer::timeout, this, &StatusReceiver::connectBroker);
}
StatusReceiver::~StatusReceiver() { stop(); }
void StatusReceiver::stop() {
    retry.stop();
    if (client) { MQTTAsync_destroy(&client); client = nullptr; }
    // destroy 等待回调结束；删除先前回调排入的事件，防止旧连接覆盖新状态。
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
    emit stateChanged("未连接", false);
}
bool StatusReceiver::start(const QString &host, int port, const QString &id) {
    stop();
    receivedMessages = malformedMessages = 0; lastPayloadBytes = 0; lastQos = -1;
    QString endpoint = host.trimmed();
    // MQTT URI 中 IPv6 字面量必须加方括号；主机名和 IPv4 保持原样。
    if (endpoint.contains(':') && !endpoint.startsWith('[')) endpoint = '[' + endpoint + ']';
    const auto uri = QString("tcp://%1:%2").arg(endpoint).arg(port).toUtf8();
    const auto bytes = id.toUtf8();
    const int rc = MQTTAsync_create(&client, uri.constData(), bytes.constData(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) { emit stateChanged(QString("创建 MQTT 失败：%1").arg(rc), false); return false; }
    const int callbackRc = MQTTAsync_setCallbacks(client, this, lost, message, nullptr);
    if (callbackRc != MQTTASYNC_SUCCESS) {
        emit stateChanged(QString("注册 MQTT 回调失败：%1").arg(callbackRc), false);
        MQTTAsync_destroy(&client); return false;
    }
    connectBroker();
    return true;
}
void StatusReceiver::connectBroker() {
    if (!client) return;
    emit stateChanged("正在连接 MQTT…", false);
    MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
    options.keepAliveInterval = 10; options.cleansession = 1; options.connectTimeout = 3;
    options.MQTTVersion = MQTTVERSION_3_1_1;
    options.context = this; options.onSuccess = connected; options.onFailure = failed;
    int rc = MQTTAsync_connect(client, &options);
    if (rc != MQTTASYNC_SUCCESS) { emit stateChanged(QString("连接失败（%1），2 秒后重试").arg(rc), false); retry.start(); }
}
void StatusReceiver::connected(void *ctx, MQTTAsync_successData *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { s->subscribe(); }, Qt::QueuedConnection);
}
void StatusReceiver::subscribe() {
    if (!client) return;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.context = this; opts.onSuccess = subscribed; opts.onFailure = subscriptionFailed;
    int rc = MQTTAsync_subscribe(client, "GameStatus", 1, &opts);
    if (rc != MQTTASYNC_SUCCESS) subscriptionFailed(this, nullptr);
}
void StatusReceiver::subscribed(void *ctx, MQTTAsync_successData *data) {
    if (data && data->alt.qos == 128) { subscriptionFailed(ctx, nullptr); return; }
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { emit s->stateChanged("已订阅 GameStatus · QoS 1", true); }, Qt::QueuedConnection);
}
void StatusReceiver::subscriptionFailed(void *ctx, MQTTAsync_failureData *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { emit s->stateChanged("订阅被拒绝或失败，请检查机器人 ID 后重新连接", false); }, Qt::QueuedConnection);
}
void StatusReceiver::failed(void *ctx, MQTTAsync_failureData *data) {
    auto *s = static_cast<StatusReceiver *>(ctx); const int code = data ? data->code : -1;
    QMetaObject::invokeMethod(s, [s, code] { emit s->stateChanged(QString("MQTT 连接失败（%1），2 秒后重试").arg(code), false); s->retry.start(); }, Qt::QueuedConnection);
}
void StatusReceiver::lost(void *ctx, char *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { emit s->stateChanged("MQTT 断开，2 秒后重连", false); s->retry.start(); }, Qt::QueuedConnection);
}
int StatusReceiver::message(void *ctx, char *topic, int topicLen, MQTTAsync_message *msg) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    const QByteArray name = topicLen ? QByteArray(topic, topicLen) : QByteArray(topic ? topic : "");
    const int payloadBytes = msg ? msg->payloadlen : -1;
    const int qos = msg ? msg->qos : -1;
    if (name == "GameStatus" && msg) {
        rm::GameStatus value;
        const bool valid = msg->payloadlen >= 0 && msg->payloadlen <= 65536 && value.ParseFromArray(msg->payload, msg->payloadlen);
        if (valid) QMetaObject::invokeMethod(s, [s, value, payloadBytes, qos] {
            ++s->receivedMessages; s->lastPayloadBytes = payloadBytes; s->lastQos = qos;
            emit s->received(value); emit s->receivedInfo(value, payloadBytes, qos);
        }, Qt::QueuedConnection);
        else QMetaObject::invokeMethod(s, [s] {
            ++s->malformedMessages;
            emit s->stateChanged(QString("已丢弃损坏的 Protobuf 消息（累计 %1 条）").arg(s->malformedMessages), true);
        }, Qt::QueuedConnection);
    }
    if (msg) MQTTAsync_freeMessage(&msg);
    if (topic) MQTTAsync_free(topic);
    return 1;
}
