#pragma once
#include "assembler.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUdpSocket>

class VideoReceiver : public QObject {
    Q_OBJECT
public:
    explicit VideoReceiver(QObject *parent = nullptr);
    ~VideoReceiver();
    bool start(const QString &address, quint16 port, const QString &ffmpeg);
    void stop();
    quint64 decoded = 0;
    quint64 packets = 0;
    quint64 dropped() const { return assembler.dropped; }
    quint64 invalid() const { return assembler.invalid; }
    quint64 zeroBasedFrames() const { return assembler.zeroBasedFrames; }
    quint64 oneBasedFrames() const { return assembler.oneBasedFrames; }
    int sliceBase() const { return assembler.lastSliceBase(); }
signals:
    void frameReady(QImage frame);
    void problem(QString message);
private:
    QUdpSocket socket;
    QProcess decoder;
    FrameAssembler assembler;
    QElapsedTimer clock;
    QTimer expiry;
    QByteArray output;
    bool waitingForHeaders = true;
    void readPackets();
    void readImages();
};
