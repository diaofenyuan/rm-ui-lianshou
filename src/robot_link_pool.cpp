#include "robot_link_pool.h"
#include <QRandomGenerator>
#include <QTimer>

RobotLinkPool::RobotLinkPool(QObject *parent) : QObject(parent) { clock.start(); }
RobotLinkPool::~RobotLinkPool() { stop(); }

void RobotLinkPool::stop() {
    ++generation;
    running = false;
    pendingIds.clear(); pendingIndex = 0; recentEventAt.clear(); lastMessageAt.clear(); reconnectCounts.clear();
    const auto current = links;
    links.clear(); ready.clear(); configuredIds.clear(); primaryId = 0;
    for (auto *receiver : current) {
        if (!receiver) continue;
        receiver->stop();
        delete receiver;
    }
    emit stateChanged("未连接", false);
}

bool RobotLinkPool::start(const QString &host, int port, const QVector<int> &robotIds) {
    stop();
    if (robotIds.isEmpty()) return false;
    hostName = host; brokerPort = port; configuredIds = robotIds;
    primaryId = configuredIds.constFirst();
    recentEventAt.clear(); lastMessageAt.clear(); reconnectCounts.clear(); clock.restart();
    pendingIds = configuredIds; pendingIndex = 0; running = true;
    const quint64 currentGeneration = ++generation;
    startNext(currentGeneration);
    return true;
}

void RobotLinkPool::startNext(quint64 expectedGeneration) {
    if (!running || expectedGeneration != generation || pendingIndex >= pendingIds.size()) return;
    const int id = pendingIds.at(pendingIndex++);
    auto *receiver = new StatusReceiver(this);
    links.insert(id, receiver); ready.insert(id, false);
    wire(id, receiver);
    const QStringList topics = id == primaryId ? StatusReceiver::allTopics() : StatusReceiver::singleRobotTopics();
    if (!receiver->start(hostName, brokerPort, QString::number(id), topics)) {
        emit linkChanged(id, false, "MQTT 客户端创建失败", receiver->subscribedTopicCount());
    }
    if (pendingIndex < pendingIds.size()) {
        const int delay = 200 + int(QRandomGenerator::global()->bounded(201u));
        QTimer::singleShot(delay, this, [this, expectedGeneration] { startNext(expectedGeneration); });
    }
}

void RobotLinkPool::wire(int id, StatusReceiver *receiver) {
    connect(receiver, &StatusReceiver::stateChanged, this,
        [this, id](const QString &text, bool isReady) {
            if (!links.contains(id)) return;
            ready[id] = isReady;
            if (!isReady && (text.contains("失败") || text.contains("重试"))) ++reconnectCounts[id];
            // 停止连接时旧 sender 的排队信号可能晚于 QObject 销毁到达；从池中查找，
            // 不捕获已被 stop() 删除的 StatusReceiver 裸指针。
            const auto *current = links.value(id);
            const int topics = current ? current->subscribedTopicCount() : 0;
            emit linkChanged(id, isReady, text, topics);
            if (id == primaryId) emit stateChanged(text, isReady);
        });
    connect(receiver, &StatusReceiver::received, this, [this, id](const rm::GameStatus &value) {
        noteMessage(id);
        if (id == primaryId) emit received(value);
    });
    connect(receiver, &StatusReceiver::receivedInfo, this,
        [this, id](const rm::GameStatus &value, int bytes, int qos) {
            noteMessage(id);
            if (id == primaryId) emit receivedInfo(value, bytes, qos);
        });
    connect(receiver, &StatusReceiver::receivedUnitStatus, this, [this, id](const rm::GlobalUnitStatus &value) {
        noteMessage(id);
        if (id == primaryId) emit receivedUnitStatus(value);
    });
    connect(receiver, &StatusReceiver::receivedLogistics, this, [this, id](const rm::GlobalLogisticsStatus &value) {
        noteMessage(id);
        if (id == primaryId) emit receivedLogistics(value);
    });
    connect(receiver, &StatusReceiver::receivedSpecialMechanism, this,
        [this, id](const rm::GlobalSpecialMechanism &value) {
            noteMessage(id);
            if (id == primaryId) emit receivedSpecialMechanism(value);
        });
    connect(receiver, &StatusReceiver::receivedEvent, this, [this, id](const rm::Event &value) {
        noteMessage(id);
        if (id != primaryId) return;
        // QoS 1 允许 broker 重投；同一 (event_id,param) 在 1.5 秒窗口内只进入一次时间线。
        const QString key = QString::number(value.has_event_id() ? value.event_id() : 0)
            + QLatin1Char('|') + QString::fromStdString(value.param());
        const qint64 now = clock.elapsed();
        const auto previous = recentEventAt.constFind(key);
        if (previous != recentEventAt.cend() && now - previous.value() < 1500) return;
        recentEventAt.insert(key, now);
        for (auto it = recentEventAt.begin(); it != recentEventAt.end();) {
            if (now - it.value() >= 1500) it = recentEventAt.erase(it); else ++it;
        }
        emit receivedEvent(value);
    });
    connect(receiver, &StatusReceiver::receivedInjury, this,
        [this, id](const rm::RobotInjuryStat &value) { noteMessage(id); emit receivedInjury(id, value); });
    connect(receiver, &StatusReceiver::receivedRespawn, this,
        [this, id](const rm::RobotRespawnStatus &value) { noteMessage(id); emit receivedRespawn(id, value); });
    connect(receiver, &StatusReceiver::receivedStatic, this,
        [this, id](const rm::RobotStaticStatus &value) { noteMessage(id); emit receivedStatic(id, value); });
    connect(receiver, &StatusReceiver::receivedDynamic, this,
        [this, id](const rm::RobotDynamicStatus &value) { noteMessage(id); emit receivedDynamic(id, value); });
    connect(receiver, &StatusReceiver::receivedModule, this,
        [this, id](const rm::RobotModuleStatus &value) { noteMessage(id); emit receivedModule(id, value); });
    connect(receiver, &StatusReceiver::receivedPosition, this,
        [this, id](const rm::RobotPosition &value) { noteMessage(id); emit receivedPosition(id, value); });
    connect(receiver, &StatusReceiver::receivedBuff, this,
        [this, id](const rm::Buff &value) { noteMessage(id); emit receivedBuff(id, value); });
    connect(receiver, &StatusReceiver::receivedPenalty, this, [this, id](const rm::PenaltyInfo &value) {
        noteMessage(id);
        if (id == primaryId) emit receivedPenalty(value);
    });
    connect(receiver, &StatusReceiver::receivedRadar, this, [this, id](const rm::RadarInfoToClient &value) {
        noteMessage(id);
        if (id == primaryId) emit receivedRadar(value);
    });
}

quint64 RobotLinkPool::receivedMessages() const {
    quint64 total = 0;
    for (const auto *receiver : links) if (receiver) total += receiver->receivedMessages;
    return total;
}
quint64 RobotLinkPool::malformedMessages() const {
    quint64 total = 0;
    for (const auto *receiver : links) if (receiver) total += receiver->malformedMessages;
    return total;
}
int RobotLinkPool::lastPayloadBytes() const {
    const auto *receiver = links.value(primaryId);
    return receiver ? receiver->lastPayloadBytes : 0;
}
int RobotLinkPool::lastQos() const {
    const auto *receiver = links.value(primaryId);
    return receiver ? receiver->lastQos : -1;
}
int RobotLinkPool::subscribedTopicCount() const {
    int total = 0;
    for (const auto *receiver : links) if (receiver) total += receiver->subscribedTopicCount();
    return total;
}

void RobotLinkPool::noteMessage(int robotId) { lastMessageAt.insert(robotId, clock.elapsed()); }

RobotLinkPool::LinkStatus RobotLinkPool::linkStatus(int robotId) const {
    LinkStatus result;
    result.ready = ready.value(robotId, false);
    result.reconnectCount = reconnectCounts.value(robotId, 0);
    if (const auto *receiver = links.value(robotId)) {
        result.subscribedTopics = receiver->subscribedTopicCount();
        result.receivedMessages = receiver->receivedMessages;
    }
    if (lastMessageAt.contains(robotId)) result.lastMessageAgeMs = clock.elapsed() - lastMessageAt.value(robotId);
    return result;
}
