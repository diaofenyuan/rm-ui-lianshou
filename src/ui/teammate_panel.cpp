#include "teammate_panel.h"
#include "operator_profile.h"
#include "status.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

namespace {
// 协议 2.2.4 的 robot_health 前 5 项口径（我方 1/2/3/4/7 号），与 match_state.cpp 保持一致。
constexpr int kSlotCount = 5;
constexpr int kSlotNumbers[kSlotCount] = {1, 2, 3, 4, 7};
// 雷达槽位顺序（协议 2.2.19）：对方 1/2/3/4/6/7 号在前，己方同编号在后。
// 编号 1/2/3/4 对应己方雷达索引 0–3，编号 7 对应索引 5（索引 4 是 6 号空中）。
constexpr int kRadarNumbers[6] = {1, 2, 3, 4, 6, 7};

int radarIndexFor(int slotNumber) {
    for (int i = 0; i < 6; ++i)
        if (kRadarNumbers[i] == slotNumber) return i;
    return -1;
}

void drawCard(QPainter &p, const QRectF &area) {
    p.setPen(QPen(theme::hud::edge, 1));
    p.setBrush(theme::hud::card);
    p.drawRoundedRect(area, 8, 8);
}

// 数值列先缩字号再省略：异常大的值不越出本列。
void drawText(QPainter &p, const QRectF &rect, const QString &text, const QColor &color,
              int pixels, bool numeric, Qt::Alignment align) {
    auto font = theme::font(pixels, numeric, numeric);
    while (pixels > 9 && QFontMetrics(font).horizontalAdvance(text) > rect.width())
        font.setPixelSize(--pixels);
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, align, QFontMetrics(font).elidedText(text, Qt::ElideRight, int(rect.width())));
}

// 协议字段缺失时的统一占位文案：不把"未发送"与数值 0 混为一谈。
QString missingText() { return QString::fromUtf8("未提供"); }
}

TeammatePanel::TeammatePanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("队友状态：5 个协议槽位的血量与存活情况");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void TeammatePanel::setRobot(int id) {
    const int number = profile::valid(id) ? id % 100 : 0;
    const bool blue = id > 100;
    if (ownNumber == number && allyBlue == blue) return;
    ownNumber = number;
    allyBlue = blue;
    update();
}
void TeammatePanel::setScale(qreal value) {
    if (qFuzzyCompare(scale, value)) return;
    scale = value;
    update();
}
void TeammatePanel::setOpacity(qreal value) {
    if (qFuzzyCompare(opacity, value)) return;
    opacity = value;
    update();
}
int TeammatePanel::rowCount() const { return kSlotCount; }

QStringList TeammatePanel::rowTexts() const {
    QStringList rows;
    for (int i = 0; i < kSlotCount; ++i) {
        const int slot = kSlotNumbers[i];
        const auto health = match->allyHealth(i);
        rows << QString("%1 号%2 %3").arg(slot)
            .arg(QString::fromUtf8(profile::roles[slot - 1].name))
            .arg(health ? QString::number(*health) : missingText());
    }
    return rows;
}

void TeammatePanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool hasUnit = match->ageMs(MatchState::Domain::UnitStatus) >= 0;
    const bool unitStale = match->isStale(MatchState::Domain::UnitStatus);
    const bool radarStale = match->isStale(MatchState::Domain::Radar);
    // 全队血量只有一份全局域，过期即整面板降级，避免用旧血量误导指挥判断。
    p.setOpacity(opacity * (unitStale || !hasUnit ? 0.55 : 1.0));

    const QRectF area = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    drawCard(p, area);
    const qreal pad = 9 * scale;
    const qreal headerHeight = int(qMax(16.0, 20.0 * scale));
    const int base = int(qMax(9.0, 10.5 * scale));

    p.setFont(theme::font(base, true));
    p.setPen(theme::hud::text);
    p.drawText(QRectF(area.left() + pad, area.top() + 4 * scale, area.width() - 2 * pad, headerHeight),
               Qt::AlignVCenter | Qt::AlignLeft, "队友");
    QString note = !hasUnit ? "血量未提供" : unitStale ? "血量已过期" : "5 个槽位";
    p.setFont(theme::font(base - 1));
    p.setPen(unitStale || !hasUnit ? theme::hud::warn : theme::hud::muted);
    p.drawText(QRectF(area.left() + pad, area.top() + 4 * scale, area.width() - 2 * pad, headerHeight),
               Qt::AlignVCenter | Qt::AlignRight, note);

    const QRectF rowsArea(area.left() + pad, area.top() + headerHeight + 6 * scale,
                          area.width() - 2 * pad, area.height() - headerHeight - 10 * scale);
    const qreal rowHeight = rowsArea.height() / kSlotCount;
    const qreal contentWidth = rowsArea.width();

    for (int i = 0; i < kSlotCount; ++i) {
        const int slot = kSlotNumbers[i];
        const QRectF row(rowsArea.left(), rowsArea.top() + rowHeight * i, contentWidth, rowHeight);
        const bool own = slot == ownNumber;
        if (own) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(127, 196, 255, 36));
            p.drawRoundedRect(row.adjusted(-4 * scale, 1, 4 * scale, -1), 5, 5);
            p.setBrush(theme::hud::accent);
            p.drawRoundedRect(QRectF(row.left() - 4 * scale, row.top() + rowHeight * 0.22,
                                     2.5 * scale, rowHeight * 0.56), 1.5, 1.5);
        }
        const auto health = match->allyHealth(i);
        const QColor healthColor = !health ? theme::hud::muted
            : *health == 0 ? theme::hud::bad : theme::hud::good;

        // 位置时效点：雷达提供了该编号的点位即可视为"位置可见"，与血量域相互独立。
        const int radarIndex = radarIndexFor(slot);
        std::optional<rm::RadarSingleRobotInfo> radarInfo;
        if (radarIndex >= 0) radarInfo = match->allyRadar(radarIndex);
        const bool located = radarInfo.has_value() && radarInfo->has_target_pos_x() && !radarStale;
        const QPointF dot(row.left() + 4 * scale, row.center().y() - rowHeight * 0.05);
        p.setPen(QPen(located ? theme::hud::accent : QColor(255, 255, 255, 62), 1.4));
        p.setBrush(located ? QBrush(theme::hud::accent) : QBrush(Qt::NoBrush));
        p.drawEllipse(dot, qMax(2.4, 3.2 * scale), qMax(2.4, 3.2 * scale));

        const qreal textTop = row.top() + rowHeight * 0.06;
        const qreal textHeight = rowHeight * 0.62;
        const qreal nameLeft = row.left() + 16 * scale;
        const qreal nameWidth = contentWidth * 0.42 - 16 * scale;
        const qreal healthWidth = contentWidth * 0.24;
        const qreal stateWidth = contentWidth - nameWidth - healthWidth - 16 * scale;
        drawText(p, QRectF(nameLeft, textTop, nameWidth, textHeight),
                 QString("%1 号%2").arg(slot).arg(QString::fromUtf8(profile::roles[slot - 1].name)),
                 own ? theme::hud::text : QColor(theme::hud::text.red(), theme::hud::text.green(),
                                                 theme::hud::text.blue(), 225),
                 base, false, Qt::AlignVCenter | Qt::AlignLeft);
        drawText(p, QRectF(nameLeft + nameWidth, textTop, healthWidth, textHeight),
                 health ? QString::number(*health) : missingText(),
                 healthColor, base, true, Qt::AlignVCenter | Qt::AlignRight);
        const QString stateText = !health ? missingText() : *health == 0 ? QString("阵亡") : QString("存活");
        drawText(p, QRectF(nameLeft + nameWidth + healthWidth, textTop, stateWidth, textHeight),
                 stateText, healthColor, base - 1, false, Qt::AlignVCenter | Qt::AlignRight);

        // 血条：只有拿到血量上限（本机 RobotStaticStatus.max_health）才画比例；
        // 队友上限需各自连接，未拿到时以虚线槽表示"比例未知"，不伪造比例。
        const qreal barHeight = qMax(3.0, 4.0 * scale);
        const QRectF track(row.left(), row.bottom() - barHeight - rowHeight * 0.04,
                           contentWidth, barHeight);
        const quint32 maxHealth = (own && match->ageMs(MatchState::Domain::RobotStatic) >= 0
                                   && match->robotStatic.has_max_health())
            ? match->robotStatic.max_health() : 0;
        if (maxHealth > 0 && health) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::hud::track);
            p.drawRoundedRect(track, barHeight / 2, barHeight / 2);
            const double ratio = qBound(0.0, *health / double(maxHealth), 1.0);
            if (ratio > 0) {
                p.setBrush(healthColor);
                p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * ratio, track.height()),
                                  barHeight / 2, barHeight / 2);
            }
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 58), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(track.left(), track.center().y()),
                       QPointF(track.right(), track.center().y()));
        }
    }
}

// ---------------------------------------------------------------------------

OwnStatusCard::OwnStatusCard(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("本机状态：血量、热量、弹量、等级与主控模块");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void OwnStatusCard::setRobot(int id) {
    if (robotId == id) return;
    robotId = id;
    update();
}
void OwnStatusCard::setScale(qreal value) {
    if (qFuzzyCompare(scale, value)) return;
    scale = value;
    update();
}
void OwnStatusCard::setOpacity(qreal value) {
    if (qFuzzyCompare(opacity, value)) return;
    opacity = value;
    update();
}

void OwnStatusCard::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(opacity);
    const QRectF area = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    drawCard(p, area);

    const int base = int(qMax(9.0, 10.5 * scale));
    const qreal pad = 9 * scale;
    const bool hasStatic = match->ageMs(MatchState::Domain::RobotStatic) >= 0;
    const bool hasDynamic = match->ageMs(MatchState::Domain::RobotDynamic) >= 0;
    const bool hasModule = match->ageMs(MatchState::Domain::RobotModule) >= 0;
    const auto &stat = match->robotStatic;
    const auto &dyn = match->robotDynamic;
    const auto missing = missingText();
    // 单机域字段统一取用口径：域未收到或字段缺失一律显示"未提供"，不用 0 冒充。
    const auto value = [&missing](bool present, const QString &text) {
        return present && !text.isEmpty() ? text : missing;
    };

    const QString identity = robotId ? profile::name(robotId) : QString("未连接");
    p.setFont(theme::font(base, true));
    p.setPen(theme::hud::text);
    const QRectF header(area.left() + pad, area.top() + 3 * scale, area.width() - 2 * pad,
                        int(qMax(16.0, 19.0 * scale)));
    p.drawText(header, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText("本机 · " + identity, Qt::ElideRight, int(header.width())));

    const QRectF grid(area.left() + pad, header.bottom() + 2 * scale,
                      area.width() - 2 * pad, area.bottom() - header.bottom() - 6 * scale);
    const int columns = 3, rows = 2;
    const qreal cellWidth = grid.width() / columns, cellHeight = grid.height() / rows;

    const QString health = hasDynamic && dyn.has_current_health()
        ? (hasStatic && stat.has_max_health() && stat.max_health() > 0
               ? QString("%1/%2").arg(dyn.current_health()).arg(stat.max_health())
               : QString::number(dyn.current_health()))
        : missing;
    const QString heat = hasDynamic && dyn.has_current_heat()
        ? (hasStatic && stat.has_max_heat() && stat.max_heat() > 0
               ? QString("%1/%2").arg(dyn.current_heat(), 0, 'f', 1).arg(stat.max_heat())
               : QString::number(dyn.current_heat(), 'f', 1))
        : missing;
    const QString ammo = value(hasDynamic && dyn.has_remaining_ammo(),
                               hasDynamic && dyn.has_remaining_ammo() ? QString::number(dyn.remaining_ammo()) : QString());
    const QString level = value(hasStatic && stat.has_level(),
                                hasStatic && stat.has_level() ? QString::number(stat.level()) : QString());
    const QString main = value(hasModule && match->robotModule.has_main_controller(),
                               hasModule && match->robotModule.has_main_controller()
                                   ? status::moduleState(match->robotModule.main_controller()) : QString());
    const QString fired = value(hasDynamic && dyn.has_total_projectiles_fired(),
                                hasDynamic && dyn.has_total_projectiles_fired()
                                    ? QString::number(dyn.total_projectiles_fired()) : QString());

    const QString captions[6] = {"血量", "热量", "弹量", "等级", "主控", "累计发弹"};
    const QString values[6] = {health, heat, ammo, level, main, fired};
    for (int i = 0; i < 6; ++i) {
        const QRectF cell(grid.left() + cellWidth * (i % columns), grid.top() + cellHeight * (i / columns),
                          cellWidth, cellHeight);
        // 第 2 行整体下移，避免两行文字贴在一起。
        const QRectF inner = cell.adjusted(0, 1, i % columns == columns - 1 ? 0 : -4 * scale, -1);
        p.setFont(theme::font(base - 1));
        p.setPen(theme::hud::muted);
        const QRectF captionRect(inner.left(), inner.top(), inner.width(), inner.height() * 0.36);
        const QRectF valueRect(inner.left(), captionRect.bottom(), inner.width(), inner.height() * 0.64);
        p.drawText(captionRect, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(captions[i], Qt::ElideRight, int(captionRect.width())));
        drawText(p, valueRect, values[i],
                 values[i] == missing ? theme::hud::muted : theme::hud::text,
                 base, true, Qt::AlignVCenter | Qt::AlignLeft);
    }
}
