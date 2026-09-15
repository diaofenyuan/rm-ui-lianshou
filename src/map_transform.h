#pragma once
#include <QPointF>
#include <QRectF>

// 场地世界坐标 → 小地图像素的纯换算。口径见 docs/console-design.md：
// 原点为红方补给站附近围挡交点，+X 沿场地长边指向蓝方，+Y 沿场地短边指向红方停机坪，单位为米。
// 底图按此口径绘制：红方端在左、+Y 朝上，因此像素映射对 Y 轴取反。
// 雷达坐标为厘米，与上述坐标系同源（原点一致性待实机核实）。
namespace maptf {
constexpr double kFieldLengthM = 28.0;   // X 轴量程
constexpr double kFieldWidthM = 15.0;    // Y 轴量程
constexpr double kRadarCmPerM = 100.0;
constexpr double kToleranceM = 1.0;      // 允许贴边与轻微超界

QPointF normalize(double xM, double yM);            // 世界坐标（米）→ [0,1]，不裁剪
QPointF normalizeRadar(quint32 xCm, quint32 yCm);   // 雷达坐标（厘米）→ [0,1]
bool plausible(double xM, double yM);               // 是否落在场地范围内（含容差）
QPointF clamped(const QPointF &normalized);         // 越界面板内继续绘制时按边界裁剪

QRectF fieldRect(const QRectF &area);               // 在 area 内按 28:15 居中等比取景
QPointF toPixels(const QPointF &normalized, const QRectF &field);
QPointF forView(const QPointF &normalized, bool allyBlue);   // 蓝方视角 180° 旋转
double yawToCanvasDegrees(double yawDeg, bool allyBlue);     // 画布角度：东 0°，顺时针为正
}
