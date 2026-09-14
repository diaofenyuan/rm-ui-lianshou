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
    quint64 zeroBasedFrames = 0;
    quint64 oneBasedFrames = 0;
    int pending() const { return frames.size(); }
    // 最近完成帧使用的分片编号基数；尚未完成时为 -1。
    int lastSliceBase() const { return lastBase; }
private:
    struct Frame {
        quint32 size = 0;
        quint32 count = 0;
        qint64 started = 0;
        // 官方协议只规定了字段宽度，未在包头说明分片从 0 还是 1 开始。
        // -1 表示目前仍有歧义，0/1 表示已由边界或尾包长度确认。
        int base = -1;
        QMap<int, QByteArray> parts;
    };
    QMap<quint16, Frame> frames;
    bool haveLast = false;
    quint16 last = 0;
    qint64 lastComplete = 0;
    int lastBase = -1;
};
