#include "receiver.h"
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QRandomGenerator>

namespace {
// 官方表 2-1 的服务器→自定义客户端 topic；topic 名与 Protobuf 消息名一致。
constexpr const char *const kTopics[] = {
    "GameStatus", "GlobalUnitStatus", "GlobalLogisticsStatus", "GlobalSpecialMechanism",
    "Event", "RobotInjuryStat", "RobotRespawnStatus", "RobotStaticStatus",
    "RobotDynamicStatus", "RobotModuleStatus", "RobotPosition", "Buff",
    "PenaltyInfo", "RadarInfoToClient",
};
constexpr int kTopicCount = int(sizeof(kTopics) / sizeof(kTopics[0]));
constexpr int kMaxPayloadBytes = 65536;
}

QStringList StatusReceiver::allTopics() {
    QStringList result;
    result.reserve(kTopicCount);
    for (const char *topic : kTopics) result.append(QString::fromLatin1(topic));
    return result;
}

QStringList StatusReceiver::singleRobotTopics() {
    return {"RobotInjuryStat", "RobotRespawnStatus", "RobotStaticStatus",
        "RobotDynamicStatus", "RobotModuleStatus", "RobotPosition", "Buff"};
}

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
bool StatusReceiver::start(const QString &host, int port, const QString &id, const QStringList &topics) {
    stop();
    topicFilter = topics;
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
    if (rc != MQTTASYNC_SUCCESS) { emit stateChanged(QString("连接失败（%1），约 2 秒后重试").arg(rc), false); scheduleRetry(); }
}
void StatusReceiver::connected(void *ctx, MQTTAsync_successData *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { s->subscribe(); }, Qt::QueuedConnection);
}
void StatusReceiver::subscribe() {
    if (!client) return;
    pendingSubscriptions = subscribedCount = 0;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.context = this; opts.onSuccess = subscribed; opts.onFailure = subscriptionFailed;
    const QStringList topics = topicFilter.isEmpty() ? allTopics() : topicFilter;
    for (const QString &topic : topics) {
        const auto name = topic.toUtf8();
        if (MQTTAsync_subscribe(client, name.constData(), 1, &opts) == MQTTASYNC_SUCCESS) {
            ++pendingSubscriptions; ++subscribedCount;
        }
    }
    if (!pendingSubscriptions) subscriptionFailed(this, nullptr);
}
void StatusReceiver::subscribed(void *ctx, MQTTAsync_successData *data) {
    if (data && data->alt.qos == 128) { subscriptionFailed(ctx, nullptr); return; }
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] {
        if (--s->pendingSubscriptions > 0) return;
        emit s->stateChanged(QString("已订阅 %1 个 topic · QoS 1").arg(s->subscribedCount), true);
    }, Qt::QueuedConnection);
}
void StatusReceiver::subscriptionFailed(void *ctx, MQTTAsync_failureData *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { emit s->stateChanged("订阅被拒绝或失败，请检查机器人 ID 后重新连接", false); }, Qt::QueuedConnection);
}
void StatusReceiver::failed(void *ctx, MQTTAsync_failureData *data) {
    auto *s = static_cast<StatusReceiver *>(ctx); const int code = data ? data->code : -1;
    QMetaObject::invokeMethod(s, [s, code] { emit s->stateChanged(QString("MQTT 连接失败（%1），约 2 秒后重试").arg(code), false); s->scheduleRetry(); }, Qt::QueuedConnection);
}
void StatusReceiver::lost(void *ctx, char *) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    QMetaObject::invokeMethod(s, [s] { emit s->stateChanged("MQTT 断开，约 2 秒后重连", false); s->scheduleRetry(); }, Qt::QueuedConnection);
}

void StatusReceiver::scheduleRetry() {
    if (!client) return;
    // 多台机器人同时掉线时错开连接，避免集中打满裁判端和本机 MQTT broker。
    retry.start(2000 + int(QRandomGenerator::global()->bounded(501u)));
}
int StatusReceiver::message(void *ctx, char *topic, int topicLen, MQTTAsync_message *msg) {
    auto *s = static_cast<StatusReceiver *>(ctx);
    const QByteArray name = topicLen ? QByteArray(topic, topicLen) : QByteArray(topic ? topic : "");
    const int payloadBytes = msg ? msg->payloadlen : -1;
    const int qos = msg ? msg->qos : -1;
    if (msg && payloadBytes >= 0) {
        // 解析延后到 UI 线程执行，此处必须复制 payload，随后立即归还 Paho 缓冲。
        const QByteArray payload(static_cast<const char *>(msg->payload), payloadBytes);
        QMetaObject::invokeMethod(s, [s, name, payload, payloadBytes, qos] {
            s->dispatch(name, payload, payloadBytes, qos);
        }, Qt::QueuedConnection);
    }
    if (msg) MQTTAsync_freeMessage(&msg);
    if (topic) MQTTAsync_free(topic);
    return 1;
}
template <typename T>
void StatusReceiver::emitParsed(const QByteArray &payload, void (StatusReceiver::*signal)(T)) {
    T value;
    if (!value.ParseFromArray(payload.constData(), payload.size())) return reportMalformed();
    ++receivedMessages;
    (this->*signal)(value);
}
void StatusReceiver::reportMalformed() {
    ++malformedMessages;
    emit stateChanged(QString("已丢弃损坏的 Protobuf 消息（累计 %1 条）").arg(malformedMessages), true);
}
void StatusReceiver::dispatch(const QByteArray &topic, const QByteArray &payload, int payloadBytes, int qos) {
    // 未知 topic 不属于本项目协议契约，直接忽略；已知 topic 只统计损坏报文，不中断接收。
    if (topic == "GameStatus") {
        rm::GameStatus value;
        if (payloadBytes <= kMaxPayloadBytes && value.ParseFromArray(payload.constData(), payloadBytes)) {
            ++receivedMessages; lastPayloadBytes = payloadBytes; lastQos = qos;
            emit received(value); emit receivedInfo(value, payloadBytes, qos);
        } else reportMalformed();
        return;
    }
    if (topic == "GlobalUnitStatus") return emitParsed(payload, &StatusReceiver::receivedUnitStatus);
    if (topic == "GlobalLogisticsStatus") return emitParsed(payload, &StatusReceiver::receivedLogistics);
    if (topic == "GlobalSpecialMechanism") return emitParsed(payload, &StatusReceiver::receivedSpecialMechanism);
    if (topic == "Event") return emitParsed(payload, &StatusReceiver::receivedEvent);
    if (topic == "RobotInjuryStat") return emitParsed(payload, &StatusReceiver::receivedInjury);
    if (topic == "RobotRespawnStatus") return emitParsed(payload, &StatusReceiver::receivedRespawn);
    if (topic == "RobotStaticStatus") return emitParsed(payload, &StatusReceiver::receivedStatic);
    if (topic == "RobotDynamicStatus") return emitParsed(payload, &StatusReceiver::receivedDynamic);
    if (topic == "RobotModuleStatus") return emitParsed(payload, &StatusReceiver::receivedModule);
    if (topic == "RobotPosition") return emitParsed(payload, &StatusReceiver::receivedPosition);
    if (topic == "Buff") return emitParsed(payload, &StatusReceiver::receivedBuff);
    if (topic == "PenaltyInfo") return emitParsed(payload, &StatusReceiver::receivedPenalty);
    if (topic == "RadarInfoToClient") return emitParsed(payload, &StatusReceiver::receivedRadar);
}
