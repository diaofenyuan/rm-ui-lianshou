#include "minimap.h"
#include "map_transform.h"
#include "theme.h"
#include <QFontMetrics>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace {
// 雷达槽位顺序（协议 2.2.19）：对方 1/2/3/4/6/7 号，随后是己方同编号。
constexpr int kRadarNumbers[6] = {1, 2, 3, 4, 6, 7};

// 底图来自官方场地地图（28 m × 15 m，红方端在左、+Y 朝上），放在源码树 assets/map 下，
// 通过 resources.qrc 以 :/map/field.jpg 打包；素材权属见 docs/console-design.md。
QPixmap loadFieldMap() {
    QImageReader reader(":/map/field.jpg");
    reader.setAutoTransform(false);   // 素材自带 EXIF 旋转标记，按原始像素读取即为横向场地。
    const QImage image = reader.read();
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}
}

MinimapPanel::MinimapPanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("战术地图：双方点位与朝向");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MinimapPanel::setOwnRobot(int id) {
    const int number = (id >= 1 && id <= 9) || (id >= 101 && id <= 109) ? id % 100 : 0;
    if (ownNumber == number && allyBlue == (id > 100)) return;
    ownNumber = number;
    allyBlue = id > 100;
    update();
}

QColor MinimapPanel::teamColor(bool enemy) const {
    const bool blue = enemy ? !allyBlue : allyBlue;
    return blue ? theme::blue : theme::red;
}

bool MinimapPanel::hasAnyData() const {
    return match->ageMs(MatchState::Domain::Position) >= 0 || match->ageMs(MatchState::Domain::Radar) >= 0;
}

QVector<MinimapPanel::Marker> MinimapPanel::markers() const {
    QVector<Marker> result;
    const auto &position = match->position;
    const bool hasPosition = match->ageMs(MatchState::Domain::Position) >= 0
        && position.has_x() && position.has_y();
    if (hasPosition && maptf::plausible(position.x(), position.y())) {
        Marker own;
        own.normalized = maptf::normalize(position.x(), position.y());
        own.number = position.has_robot_id() && position.robot_id() != 0
            ? int(position.robot_id()) % 100 : ownNumber;
        own.own = true;
        own.yaw = position.has_yaw();
        own.yawDegrees = position.yaw();
        result.append(own);
    }
    // 雷达每次触发都发送全部 12 项；缺少的槽位视为未观测，不绘制。
    if (match->ageMs(MatchState::Domain::Radar) < 0) return result;
    for (int i = 0; i < 6; ++i) {
        for (int side = 0; side < 2; ++side) {
            const bool enemy = side == 0;
            auto info = enemy ? match->enemyRadar(i) : match->allyRadar(i);
            if (!info.has_value() || !info->has_target_pos_x() || !info->has_target_pos_y()) continue;
            const double xM = info->target_pos_x() / maptf::kRadarCmPerM;
            const double yM = info->target_pos_y() / maptf::kRadarCmPerM;
            if (!maptf::plausible(xM, yM)) continue;
            const int number = kRadarNumbers[i];
            if (!enemy && number == ownNumber && hasPosition) continue;
            Marker marker;
            marker.normalized = maptf::normalize(xM, yM);
            marker.number = number;
            marker.enemy = enemy;
            marker.highlight = info->has_is_high_light() ? int(info->is_high_light()) : 0;
            result.append(marker);
        }
    }
    return result;
}

int MinimapPanel::markerCount() const { return markers().size(); }

void MinimapPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::text, muted = theme::muted;
    const QRectF area = rect().adjusted(2, 2, -2, -2);
    const QRectF footer(area.left(), area.bottom() - 16, area.width(), 16);
    const QRectF field = maptf::fieldRect(QRectF(area.left(), area.top(), area.width(), area.height() - 18));

    // 底图：按控件尺寸缓存缩放结果，蓝方视角预先旋转 180°。
    if (bitmap.isNull()) bitmap = loadFieldMap();
    const QSize key = field.size().toSize();
    if (renderedKey != key || renderedBlue != allyBlue) {
        renderedKey = key;
        renderedBlue = allyBlue;
        rendered = QPixmap();
        if (!bitmap.isNull() && key.width() > 0 && key.height() > 0) {
            QPixmap scaled = bitmap.scaled(key, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (allyBlue) scaled = scaled.transformed(QTransform().rotate(180), Qt::SmoothTransformation);
            rendered = scaled;
        }
    }
    QPainterPath clip;
    clip.addRoundedRect(field, 6, 6);
    p.save();
    p.setClipPath(clip);
    if (rendered.isNull()) p.fillRect(field, QColor("#F4F8FA"));
    else p.drawPixmap(field.topLeft(), rendered);
    // 过期时压低底图对比度，突出"当前没有实时位置"。
    const bool positionStale = match->isStale(MatchState::Domain::Position);
    const bool radarStale = match->isStale(MatchState::Domain::Radar);
    if (hasAnyData() && positionStale && radarStale) p.fillRect(field, QColor(255, 255, 255, 130));
    p.restore();
    p.setPen(QColor("#BDCBD5"));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(field, 6, 6);

    // 点位：本机带朝向；雷达点位在高亮时加环，过期整体半透明。
    const auto list = markers();
    for (const auto &marker : list) {
        const QPointF point = maptf::toPixels(maptf::forView(marker.normalized, allyBlue), field);
        QColor color = teamColor(marker.enemy);
        const bool faded = marker.own ? positionStale : radarStale;
        if (faded) color.setAlpha(90);
        if (marker.yaw && !faded) {
            const double radians = qDegreesToRadians(maptf::yawToCanvasDegrees(marker.yawDegrees, allyBlue));
            p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(point, point + QPointF(std::cos(radians), std::sin(radians)) * 13);
        }
        p.setPen(QPen(QColor("#FFFFFF"), 1.5));
        p.setBrush(color);
        const double radius = marker.own ? 8.5 : 7.0;
        p.drawEllipse(point, radius, radius);
        if (marker.highlight > 0) {
            p.setPen(QPen(color, 1.5, marker.highlight == 2 ? Qt::DashLine : Qt::SolidLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(point, radius + 4.5, radius + 4.5);
        }
        p.setFont(theme::font(9, true, true));
        p.setPen(QColor("#FFFFFF"));
        p.drawText(QRectF(point.x() - 7, point.y() - 7, 14, 14), Qt::AlignCenter, QString::number(marker.number));
    }

    // 页脚：左侧为数据时效，右侧为视角说明。
    QString note;
    if (!hasAnyData()) note = "等待位置数据";
    else if (positionStale && radarStale) note = "位置数据已过期 · 仅显示最后已知位置";
    else if (radarStale) note = "雷达数据过期 · 点位为最后已知位置";
    else note = QString("点位 %1 · 官方场地底图").arg(list.size());
    p.setFont(theme::font(10));
    p.setPen(muted);
    p.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, note);
    p.setPen(ink);
    p.drawText(footer, Qt::AlignRight | Qt::AlignVCenter,
               QString("我方 %1 · 敌方 %2").arg(allyBlue ? "蓝方" : "红方", allyBlue ? "红方" : "蓝方"));
}
