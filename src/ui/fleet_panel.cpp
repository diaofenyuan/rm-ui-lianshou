#include "fleet_panel.h"
#include "operator_profile.h"
#include "status.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>

FleetPanel::FleetPanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("我方全队机器人列表");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setTeamBlue(false);
}

void FleetPanel::setTeamBlue(bool value) {
    if (blue == value && !robotIds.isEmpty()) return;
    blue = value;
    robotIds.clear();
    for (int number = 1; number <= 7; ++number) robotIds.append((blue ? 100 : 0) + number);
    update();
}

void FleetPanel::setOwnRobot(int id) {
    ownRobotId = id;
    setTeamBlue(id > 100);
    update();
}

QColor FleetPanel::teamColor() const { return blue ? theme::blue : theme::red; }

int FleetPanel::healthSlot(int number) {
    constexpr int healthSlots[] = {1, 2, 3, 4, 7};
    for (int i = 0; i < 5; ++i) if (healthSlots[i] == number) return i;
    return -1;
}

QString FleetPanel::summaryText() const {
    int linked = 0, fresh = 0, alive = 0, heatCount = 0, ammoCount = 0;
    quint64 heatTotal = 0, ammoTotal = 0;
    for (const int id : robotIds) {
        if (!match->lastLinkIdFor(id).isEmpty()) ++linked;
        const auto *snapshot = match->robot(id);
        if (!snapshot) continue;
        if (!match->isRobotStale(id, MatchState::Domain::RobotDynamic)) ++fresh;
        if (snapshot->robotDynamic.has_current_health() && snapshot->robotDynamic.current_health() > 0)
            ++alive;
        if (snapshot->robotDynamic.has_current_heat()) {
            heatTotal += snapshot->robotDynamic.current_heat();
            ++heatCount;
        }
        if (snapshot->robotDynamic.has_remaining_ammo()) {
            ammoTotal += snapshot->robotDynamic.remaining_ammo();
            ++ammoCount;
        }
    }
    const QString heat = heatCount ? QString(" · 热均 %1").arg(heatTotal / double(heatCount), 0, 'f', 0) : QString();
    const QString ammo = ammoCount ? QString(" · 弹余 %1").arg(ammoTotal) : QString();
    return QString("%1/7 已建链 · %2/7 有数据 · 存活 %3%4%5").arg(linked).arg(fresh).arg(alive).arg(heat).arg(ammo);
}

void FleetPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::text, muted = theme::muted, track{"#E3EAF0"};
    const QColor accent = teamColor();
    const QFont nameFont = theme::font(12, true);
    const QFont detailFont = theme::font(10, false, true);
    const int gap = 2;
    const int rowHeight = robotIds.isEmpty() ? 30 : qMax(27, (height() - gap * (robotIds.size() - 1) - 2) / robotIds.size());
    for (int index = 0; index < robotIds.size(); ++index) {
        const int id = robotIds.at(index);
        const int number = id % 100;
        const QRectF row(1, 1 + index * (rowHeight + gap), width() - 2, rowHeight);
        const auto *snapshot = match->robot(id);
        const bool linked = !match->lastLinkIdFor(id).isEmpty();
        const bool fresh = snapshot && !match->isRobotStale(id, MatchState::Domain::RobotDynamic);
        const bool own = id == ownRobotId;
        painter.setPen(Qt::NoPen);
        painter.setBrush(own ? QColor(accent).lighter(188) : QColor("#F7F9FB"));
        painter.drawRoundedRect(row, 6, 6);
        if (own) {
            painter.setBrush(accent);
            painter.drawRoundedRect(QRectF(row.left(), row.top() + 5, 3, row.height() - 10), 1.5, 1.5);
        }

        painter.setFont(nameFont);
        painter.setPen(own ? accent : ink);
        const QString title = QString("%1 号%2%3").arg(number)
            .arg(QString::fromUtf8(profile::roles[number - 1].name)).arg(own ? " · 本机" : "");
        painter.drawText(QRectF(row.left() + 10, row.top() + 1, row.width() * 0.42, 13),
            Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(nameFont).elidedText(title, Qt::ElideRight, int(row.width() * 0.42)));

        painter.setFont(detailFont);
        painter.setPen(!linked || !snapshot ? muted : fresh ? theme::green : theme::warning);
        QString linkId = match->lastLinkIdFor(id);
        if (linkId.startsWith("mqtt://")) linkId.remove(0, 7);
        const QString linkText = !linked ? "未建链" : !snapshot ? "已建链 · 等待数据"
            : !fresh ? "数据过期" : QString("在线 · #%1").arg(linkId);
        painter.drawText(QRectF(row.right() - 120, row.top() + 4, 112, 18), Qt::AlignRight | Qt::AlignVCenter, linkText);

        QString detail;
        std::optional<quint32> health;
        std::optional<quint32> maxHealth;
        if (snapshot) {
            if (snapshot->robotDynamic.has_current_health()) health = snapshot->robotDynamic.current_health();
            if (snapshot->robotStatic.has_max_health()) maxHealth = snapshot->robotStatic.max_health();
        }
        const int slot = healthSlot(number);
        if (!health.has_value() && slot >= 0) health = match->allyHealth(slot);
        if (health.has_value()) detail += QString("血 %1").arg(*health); else detail += "血 —";
        if (maxHealth.has_value()) detail += QString("/%1").arg(*maxHealth);
        if (snapshot && snapshot->robotDynamic.has_current_heat()) detail += QString(" · 热 %1").arg(snapshot->robotDynamic.current_heat());
        else detail += " · 热 —";
        if (snapshot && snapshot->robotDynamic.has_remaining_ammo()) detail += QString(" · 弹 %1").arg(snapshot->robotDynamic.remaining_ammo());
        else detail += " · 弹 —";
        if (snapshot && snapshot->robotStatic.has_level()) detail += QString(" · Lv%1").arg(snapshot->robotStatic.level());
        else detail += " · Lv —";
        if (snapshot && snapshot->robotModule.has_main_controller())
            detail += QString(" · 主控%1").arg(status::moduleState(snapshot->robotModule.main_controller()));
        if (snapshot && snapshot->respawn.has_current_respawn_progress() && snapshot->respawn.has_total_respawn_progress())
            detail += QString(" · 复活 %1/%2").arg(snapshot->respawn.current_respawn_progress())
                .arg(snapshot->respawn.total_respawn_progress());
        const qint64 positionAge = match->robotAgeMs(id, MatchState::Domain::Position);
        detail += positionAge < 0 ? " · 位 —" : QString(" · 位 %1s").arg(positionAge / 1000.0, 0, 'f', 1);
        painter.setPen(muted);
        painter.drawText(QRectF(row.left() + 10, row.top() + row.height() - 14, row.width() - 20, 13),
            Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(detailFont).elidedText(detail, Qt::ElideRight, int(row.width() - 20)));

        if (rowHeight >= 38 && health.has_value() && maxHealth.has_value() && *maxHealth > 0) {
            const QRectF bar(row.left() + row.width() * 0.44, row.top() + row.height() - 8,
                row.width() * 0.48, 4);
            painter.setPen(Qt::NoPen); painter.setBrush(track); painter.drawRoundedRect(bar, 2, 2);
            painter.setBrush(*health == 0 ? theme::red : accent);
            QRectF fill = bar; fill.setWidth(bar.width() * qBound(0.0, *health / double(*maxHealth), 1.0));
            painter.drawRoundedRect(fill, 2, 2);
        }
    }
}
