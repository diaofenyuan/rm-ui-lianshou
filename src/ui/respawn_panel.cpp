#include "respawn_panel.h"
#include "theme.h"
#include <QPainter>
#include <QStringList>

RespawnPanel::RespawnPanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("复活状态：读条进度与买活信息");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QString RespawnPanel::statusText() const {
    const auto &value = match->respawn;
    if (match->ageMs(MatchState::Domain::Respawn) < 0) return "未收到复活数据";
    if (match->isStale(MatchState::Domain::Respawn)) return "复活数据过期";
    if (!value.has_is_pending_respawn() || !value.is_pending_respawn()) return "未在复活读条";
    return QString("复活读条 %1/%2").arg(value.has_current_respawn_progress() ? int(value.current_respawn_progress()) : 0)
        .arg(value.has_total_respawn_progress() ? int(value.total_respawn_progress()) : 0);
}

void RespawnPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &value = match->respawn;
    const bool hasData = match->ageMs(MatchState::Domain::Respawn) >= 0;
    if (!hasData) {
        p.setFont(theme::font(11));
        p.setPen(theme::muted);
        p.drawText(rect(), Qt::AlignCenter, "未收到复活数据");
        return;
    }
    const bool stale = match->isStale(MatchState::Domain::Respawn);
    const bool pending = value.has_is_pending_respawn() && value.is_pending_respawn();
    const QColor ink = stale ? theme::muted : theme::text;

    p.setFont(theme::font(12, true));
    p.setPen(pending && !stale ? theme::red : ink);
    p.drawText(QRectF(0, 0, width(), 20), Qt::AlignLeft | Qt::AlignVCenter, statusText());
    if (stale) {
        p.setFont(theme::font(10));
        p.setPen(theme::muted);
        p.drawText(QRectF(0, 0, width(), 20), Qt::AlignRight | Qt::AlignVCenter, "数据过期");
    }
    if (!pending) return;

    const int current = value.has_current_respawn_progress() ? int(value.current_respawn_progress()) : 0;
    const int total = value.has_total_respawn_progress() ? int(value.total_respawn_progress()) : 0;
    const QRectF track(0, 26, width(), 8);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#E3EAF0"));
    p.drawRoundedRect(track, 4, 4);
    if (total > 0 && current > 0) {
        QRectF front = track;
        front.setWidth(track.width() * qBound(0.0, current / double(total), 1.0));
        p.setBrush(stale ? theme::muted : theme::red);
        p.drawRoundedRect(front, 4, 4);
    }

    QStringList details;
    if (value.has_can_free_respawn())
        details += value.can_free_respawn() ? "免费复活：可用" : "免费复活：不可用";
    if (value.has_gold_cost_for_respawn() && value.gold_cost_for_respawn() > 0) {
        QString cost = QString("买活金币 %1").arg(value.gold_cost_for_respawn());
        if (value.has_can_pay_for_respawn()) cost += value.can_pay_for_respawn() ? "（可支付）" : "（不可支付）";
        details += cost;
    }
    if (!details.isEmpty()) {
        p.setFont(theme::font(11));
        p.setPen(theme::muted);
        p.drawText(QRectF(0, 40, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, details.join(" · "));
    }
}
