#pragma once
#include <QByteArray>
#include <QMap>
#include <optional>

class FrameAssembler {
public:
    std::optional<QByteArray> push(const QByteArray &packet, qint64 now);
    void expire(qint64 now);
    void reset();
    quint64 dropped = 0;
    quint64 invalid = 0;
    int pending() const { return frames.size(); }
private:
    struct Frame { quint32 size = 0; qint64 started = 0; QMap<int, QByteArray> parts; };
    QMap<quint16, Frame> frames;
    bool haveLast = false;
    quint16 last = 0;
    qint64 lastComplete = 0;
};
