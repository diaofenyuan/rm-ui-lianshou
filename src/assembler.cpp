#include "assembler.h"
#include <QtEndian>

void FrameAssembler::reset() {
    frames.clear(); haveLast = false; lastBase = -1;
}
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
    const quint32 payloadSize = quint32(packet.size() - 8);
    // 同时尝试 0 起始和 1 起始。尾分片长度可以消除大多数歧义，
    // 对尚未收到尾片的完整分片先保留，待后续边界包确认基数。
    const bool zeroCandidate = part < count
        && payloadSize == (part + 1 == count ? total - part * 1392 : 1392);
    const bool oneCandidate = part >= 1 && part <= count
        && payloadSize == (part == count ? total - (part - 1) * 1392 : 1392);
    if (!zeroCandidate && !oneCandidate) { ++invalid; return {}; }
    if (!frames.contains(id) && frames.size() >= 8) { ++dropped; return {}; }
    auto &f = frames[id];
    if (!f.size) { f.size = total; f.count = count; f.started = now; }
    if (f.size != total) { frames.remove(id); ++invalid; return {}; }
    const bool baseAllowed = (f.base < 0) || (f.base == 0 && zeroCandidate) || (f.base == 1 && oneCandidate);
    if (!baseAllowed) { frames.remove(id); ++invalid; return {}; }
    if (f.base < 0 && zeroCandidate != oneCandidate) f.base = zeroCandidate ? 0 : 1;
    const QByteArray payload = packet.mid(8);
    if (f.parts.contains(part) && f.parts[part] != payload) { frames.remove(id); ++invalid; return {}; }
    f.parts[part] = payload;
    if (quint32(f.parts.size()) != count) return {};
    // 完整帧的键必须恰好覆盖一个连续区间，防止“数量够了但缺片/重复片”
    // 被误拼接。count==1 时两种基数在字节流上等价，沿用已推断值。
    int base = f.base;
    if (base < 0) {
        const bool isZero = f.parts.contains(0) && f.parts.contains(int(count - 1));
        const bool isOne = f.parts.contains(1) && f.parts.contains(int(count));
        if (isZero == isOne) { // 未形成唯一连续区间，继续等待或判为异常。
            if (count == 1 && isZero) base = 0;
            else return {};
        } else base = isZero ? 0 : 1;
    }
    for (quint32 i = 0; i < count; ++i) {
        if (!f.parts.contains(base + int(i))) return {};
    }
    QByteArray complete;
    complete.reserve(total);
    for (quint32 i = 0; i < count; ++i) complete.append(f.parts.value(base + int(i)));
    frames.remove(id);
    last = id; haveLast = true; lastComplete = now; lastBase = base;
    if (base == 0) ++zeroBasedFrames; else ++oneBasedFrames;
    return complete;
}
