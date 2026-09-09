#include "assembler.h"
#include <QtEndian>

void FrameAssembler::reset() { frames.clear(); haveLast = false; }
void FrameAssembler::expire(qint64 now) {
    for (auto it = frames.begin(); it != frames.end();) {
        if (now - it->started > 150) { it = frames.erase(it); ++dropped; }
        else ++it;
    }
    if (haveLast && now - lastComplete > 1000) haveLast = false;
}
std::optional<QByteArray> FrameAssembler::push(const QByteArray &packet, qint64 now) {
    expire(now);
    if (packet.size() < 9 || packet.size() > 1400) { ++invalid; return {}; }
    const auto *p = reinterpret_cast<const uchar *>(packet.constData());
    const auto id = qFromBigEndian<quint16>(p);
    const auto part = qFromBigEndian<quint16>(p + 2);
    const auto total = qFromBigEndian<quint32>(p + 4);
    // 帧号按 16 位回绕比较，已经显示的帧不重复送入有状态的 HEVC 解码器。
    const quint16 distance = quint16(id - last);
    if (haveLast && (distance == 0 || distance >= 32768)) return {};
    if (total == 0 || total > 4 * 1024 * 1024) { ++invalid; return {}; }
    const quint32 count = (total + 1391) / 1392;
    const quint32 expected = part + 1 == count ? total - part * 1392 : 1392;
    if (part >= count || quint32(packet.size() - 8) != expected) { ++invalid; return {}; }
    if (!frames.contains(id) && frames.size() >= 8) { ++dropped; return {}; }
    auto &f = frames[id];
    if (!f.size) { f.size = total; f.started = now; }
    if (f.size != total) { frames.remove(id); ++invalid; return {}; }
    const QByteArray payload = packet.mid(8);
    if (f.parts.contains(part) && f.parts[part] != payload) { frames.remove(id); ++invalid; return {}; }
    f.parts[part] = payload;
    if (quint32(f.parts.size()) != count) return {};
    QByteArray complete;
    complete.reserve(total);
    for (const auto &bytes : f.parts) complete.append(bytes);
    frames.remove(id);
    last = id; haveLast = true; lastComplete = now;
    return complete;
}
