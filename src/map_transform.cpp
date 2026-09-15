#include "map_transform.h"
#include <cmath>

namespace maptf {
QPointF normalize(double xM, double yM) {
    return {xM / kFieldLengthM, yM / kFieldWidthM};
}

QPointF normalizeRadar(quint32 xCm, quint32 yCm) {
    return normalize(xCm / kRadarCmPerM, yCm / kRadarCmPerM);
}

bool plausible(double xM, double yM) {
    return xM >= -kToleranceM && xM <= kFieldLengthM + kToleranceM
        && yM >= -kToleranceM && yM <= kFieldWidthM + kToleranceM;
}

QPointF clamped(const QPointF &normalized) {
    return {qBound(0.0, normalized.x(), 1.0), qBound(0.0, normalized.y(), 1.0)};
}

QRectF fieldRect(const QRectF &area) {
    if (area.width() <= 0 || area.height() <= 0) return area;
    const double aspect = kFieldLengthM / kFieldWidthM;
    double width = area.width();
    double height = width / aspect;
    if (height > area.height()) {
        height = area.height();
        width = height * aspect;
    }
    return {area.x() + (area.width() - width) / 2, area.y() + (area.height() - height) / 2, width, height};
}

QPointF toPixels(const QPointF &normalized, const QRectF &field) {
    return {field.x() + normalized.x() * field.width(), field.y() + normalized.y() * field.height()};
}

QPointF forView(const QPointF &normalized, bool allyBlue) {
    // 蓝方视角把场地旋转 180°，使我方基地与红方视角时处于同一侧。
    return allyBlue ? QPointF(1 - normalized.x(), 1 - normalized.y()) : normalized;
}

double yawToCanvasDegrees(double yawDeg, bool allyBlue) {
    // 正北（协议未指明对应轴，当前按 +Y 实现）→ 画布向下；东（+X）→ 画布向右。
    double degrees = 90.0 - yawDeg + (allyBlue ? 180.0 : 0.0);
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0 ? degrees + 360.0 : degrees;
}
}
