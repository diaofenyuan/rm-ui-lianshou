#include "video.h"
#include <QNetworkDatagram>
#include <QtEndian>
#include <QVariant>

VideoReceiver::VideoReceiver(QObject *parent) : QObject(parent) {
    connect(&socket, &QUdpSocket::readyRead, this, &VideoReceiver::readPackets);
    connect(&decoder, &QProcess::readyReadStandardOutput, this, &VideoReceiver::readImages);
    connect(&decoder, &QProcess::readyReadStandardError, this, [this] {
        const auto text = QString::fromUtf8(decoder.readAllStandardError()).trimmed();
        if (!text.isEmpty()) emit problem("解码器：" + text.left(300));
    });
    connect(&decoder, &QProcess::errorOccurred, this, [this] { emit problem("无法运行 FFmpeg：" + decoder.errorString()); });
    expiry.setInterval(50);
    connect(&expiry, &QTimer::timeout, this, [this] { assembler.expire(clock.elapsed()); });
}
VideoReceiver::~VideoReceiver() { stop(); }
void VideoReceiver::stop() {
    socket.close(); expiry.stop();
    if (decoder.state() != QProcess::NotRunning) { decoder.kill(); decoder.waitForFinished(1500); }
    output.clear(); assembler.reset(); waitingForHeaders = true;
}
bool VideoReceiver::start(const QString &address, quint16 port, const QString &ffmpeg) {
    stop();
    packets = decoded = 0; assembler.invalid = assembler.dropped = 0;
    assembler.zeroBasedFrames = assembler.oneBasedFrames = 0;
    const QHostAddress bind(address);
    if (bind.isNull() || !socket.bind(bind, port, QUdpSocket::DontShareAddress)) {
        emit problem("UDP 绑定失败，请检查本机地址及端口占用：" + socket.errorString()); return false;
    }
    socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
    // 子进程承担 HEVC 解码，Qt 只接收完整 BMP；码流不经过临时文件或二次视频编码。
    decoder.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-probesize", "32", "-analyzeduration", "0",
        "-flags", "low_delay", "-f", "hevc", "-i", "pipe:0", "-an", "-vf", "scale=1280:720:force_original_aspect_ratio=decrease",
        "-c:v", "bmp", "-pix_fmt", "bgr24", "-f", "image2pipe", "-flush_packets", "1", "pipe:1"});
    if (!decoder.waitForStarted(3000)) { socket.close(); return false; }
    clock.start(); expiry.start(); return true;
}
void VideoReceiver::readPackets() {
    int budget = 256;
    while (socket.hasPendingDatagrams() && budget-- > 0) {
        auto bytes = socket.receiveDatagram(1401).data(); ++packets;
        auto complete = assembler.push(bytes, clock.elapsed());
        if (!complete) continue;
        if (waitingForHeaders) {
            // 中途接入时先等 VPS 参数集，避免把缺少参数集的预测帧交给解码器。
            bool vps = false;
            for (qsizetype i = 0; i + 4 < complete->size(); ++i) {
                if (complete->at(i) == 0 && complete->at(i+1) == 0 && complete->at(i+2) == 1 &&
                    ((uchar(complete->at(i+3)) >> 1) & 63) == 32) { vps = true; break; }
            }
            if (!vps) continue;
            waitingForHeaders = false;
        }
        if (decoder.state() != QProcess::Running || decoder.bytesToWrite() > 4 * 1024 * 1024) {
            ++assembler.dropped; continue;
        }
        decoder.write(*complete);
    }
    if (socket.hasPendingDatagrams()) QTimer::singleShot(0, this, &VideoReceiver::readPackets);
}
void VideoReceiver::readImages() {
    output.append(decoder.readAllStandardOutput());
    QImage latest;
    while (output.size() >= 14) {
        const auto size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(output.constData() + 2));
        if (!output.startsWith("BM") || size < 54 || size > 16 * 1024 * 1024) {
            output.clear(); emit problem("FFmpeg 输出帧格式错误"); return;
        }
        if (output.size() < size) break;
        auto frame = QImage::fromData(reinterpret_cast<const uchar *>(output.constData()), size, "BMP");
        output.remove(0, size);
        if (!frame.isNull()) { latest = frame; ++decoded; }
    }
    // 界面始终使用最新帧，避免界面刷新慢时持续累积延迟。
    if (!latest.isNull()) emit frameReady(latest);
}
