#include "status_strip.h"
#include "operator_profile.h"
#include "status.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>

namespace {
// 阶段 4 = 比赛中：仅在这一阶段的末尾倒计时才使用紧急色，避免准备阶段误报。
constexpr quint32 kStageRunning = 4;
constexpr qint32 kUrgentSeconds = 10;

// 数值列先缩字号再省略，与比分条口径一致：异常大的值不越出本列。
void drawValue(QPainter &p, const QRectF &rect, const QString &text, const QColor &color,
               int pixels, Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft) {
    auto font = theme::font(pixels, true, true);
    while (pixels > 10 && QFontMetrics(font).horizontalAdvance(text) > rect.width())
        font.setPixelSize(--pixels);
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, align, QFontMetrics(font).elidedText(text, Qt::ElideRight, int(rect.width())));
}
void drawCaption(QPainter &p, const QRectF &rect, const QString &text, const QColor &color, int pixels) {
    p.setFont(theme::font(pixels));
    p.setPen(color);
    p.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(p.font()).elidedText(text, Qt::ElideRight, int(rect.width())));
}
// 链路时效点：实心=在线，空心=未就绪，受色觉影响较小，故同时绘制圆点与文字。
void drawLinkDot(QPainter &p, const QPointF &center, const QString &label, bool alive, bool ready,
                 qreal scale) {
    const qreal radius = qMax(3.0, 4.0 * scale);
    const QColor color = alive ? theme::hud::good : ready ? theme::hud::warn : theme::hud::bad;
    p.setPen(QPen(color, 1.6));
    p.setBrush(alive ? QBrush(color) : QBrush(Qt::NoBrush));
    p.drawEllipse(center, radius, radius);
    p.setFont(theme::font(int(qMax(9.0, 10.0 * scale))));
    p.setPen(theme::hud::muted);
    p.drawText(QRectF(center.x() + radius + 4 * scale, center.y() - radius - 2,
                      int(34 * scale), radius * 2 + 4),
               Qt::AlignVCenter | Qt::AlignLeft, label);
}
}

StatusStrip::StatusStrip(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("顶部信息条：本机身份与存活、阶段与倒计时、局数与链路时效");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StatusStrip::setRobot(int id) {
    const int number = profile::valid(id) ? id % 100 : 0;
    const bool blue = id > 100;
    if (ownNumber == number && allyBlue == blue) return;
    ownNumber = number;
    allyBlue = blue;
    update();
}

void StatusStrip::setLinkState(bool ready, bool aliveData, bool aliveImage) {
    if (mqttReady == ready && dataAlive == aliveData && imageAlive == aliveImage) return;
    mqttReady = ready; dataAlive = aliveData; imageAlive = aliveImage;
    update();
}

void StatusStrip::setScale(qreal value) {
    if (qFuzzyCompare(scale, value)) return;
    scale = value;
    update();
}

void StatusStrip::setOpacity(qreal value) {
    if (qFuzzyCompare(opacity, value)) return;
    opacity = value;
    update();
}

QString StatusStrip::summaryText() const {
    const auto &game = match->game;
    const bool hasGame = match->ageMs(MatchState::Domain::Game) >= 0;
    QStringList parts;
    parts << QString("身份 %1").arg(ownNumber ? profile::name(allyBlue ? 100 + ownNumber : ownNumber) : "未连接");
    parts << (hasGame && game.has_current_stage() ? QString("阶段 %1").arg(status::stage(game.current_stage())) : "阶段 未提供");
    parts << (hasGame && game.has_stage_countdown_sec() ? QString("倒计时 %1").arg(status::duration(game.stage_countdown_sec())) : "倒计时 未提供");
    if (hasGame && game.has_current_round() && game.has_total_rounds())
        parts << QString("第 %1 / %2 局").arg(game.current_round()).arg(game.total_rounds());
    return parts.join(" · ");
}

void StatusStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(opacity);
    const auto &game = match->game;
    const bool hasGame = match->ageMs(MatchState::Domain::Game) >= 0;
    const bool gameStale = match->isStale(MatchState::Domain::Game);
    const bool paused = hasGame && !gameStale && game.has_is_paused() && game.is_paused();
    const bool settled = hasGame && !gameStale && status::isSettlement(game);

    const QRectF area = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(theme::hud::edge, 1));
    p.setBrush(settled ? theme::hud::settled : paused ? theme::hud::paused : theme::hud::cardStrong);
    p.drawRoundedRect(area, 8, 8);

    const int base = int(qMax(9.0, 11.0 * scale));
    const QRectF content = area.adjusted(12 * scale, 4 * scale, -12 * scale, -4 * scale);
    // 左：身份与存活 / 中：阶段与倒计时 / 右：局数、机制与链路
    const qreal leftWidth = content.width() * 0.30;
    const qreal rightWidth = content.width() * 0.32;
    const QRectF leftCell(content.left(), content.top(), leftWidth, content.height());
    const QRectF midCell(leftCell.right() + 8 * scale, content.top(),
                         content.width() - leftWidth - rightWidth - 16 * scale, content.height());
    const QRectF rightCell(midCell.right() + 8 * scale, content.top(), rightWidth, content.height());

    // ---- 左：本机身份 + 存活 / 阵亡 ----
    const QString identity = ownNumber
        ? QString("%1 · ID %2").arg(profile::name(allyBlue ? 100 + ownNumber : ownNumber))
              .arg(allyBlue ? 100 + ownNumber : ownNumber)
        : QString("未选择操作位");
    const bool hasStatic = match->ageMs(MatchState::Domain::RobotStatic) >= 0;
    const auto &stat = match->robotStatic;
    QString aliveText = "存活状态未提供";
    QColor aliveColor = theme::hud::muted;
    if (hasStatic && stat.has_connection_state() && stat.connection_state() == 0) {
        aliveText = "未连接"; aliveColor = theme::hud::warn;
    } else if (hasStatic && stat.has_alive_state()) {
        switch (stat.alive_state()) {
        case 1: aliveText = "存活"; aliveColor = theme::hud::good; break;
        case 2: aliveText = "阵亡"; aliveColor = theme::hud::bad; break;
        default: aliveText = QString("状态未知（%1）").arg(stat.alive_state()); aliveColor = theme::hud::warn; break;
        }
    } else if (hasStatic && !stat.has_alive_state()) {
        aliveText = "存活状态未提供";
    }
    if (hasStatic && match->isStale(MatchState::Domain::RobotStatic)) {
        aliveColor = theme::hud::muted;
        aliveText += " · 已过期";
    }
    drawCaption(p, QRectF(leftCell.left(), leftCell.top(), leftCell.width(), leftCell.height()/2),
                identity, theme::hud::text, base);
    drawCaption(p, QRectF(leftCell.left(), leftCell.center().y(), leftCell.width(), leftCell.height()/2),
                aliveText, aliveColor, base);

    // ---- 中：阶段 + 倒计时 ----
    QString stageText = hasGame && game.has_current_stage() ? status::stage(game.current_stage()) : "等待比赛信息";
    if (hasGame && gameStale) stageText += " · 已过期";
    else if (paused) stageText += " · 已暂停";
    else if (hasGame && !game.has_is_paused()) stageText += " · 暂停状态未提供";
    if (settled && game.has_game_result())
        stageText += " · " + status::result(game.game_result());
    const bool urgent = hasGame && !gameStale && game.has_current_stage()
        && game.current_stage() == kStageRunning && !paused
        && game.has_stage_countdown_sec() && game.stage_countdown_sec() >= 0
        && game.stage_countdown_sec() <= kUrgentSeconds;
    drawCaption(p, QRectF(midCell.left(), midCell.top(), midCell.width(), midCell.height()*0.42),
                stageText, paused ? theme::hud::warn : theme::hud::muted, base);
    drawValue(p, QRectF(midCell.left(), midCell.center().y() - midCell.height()*0.06,
                        midCell.width(), midCell.height()*0.56),
              hasGame && game.has_stage_countdown_sec() ? status::duration(game.stage_countdown_sec()) : "--:--",
              urgent ? theme::hud::bad : theme::hud::text,
              int(qBound(18.0, 30.0 * scale, 46.0)), Qt::AlignVCenter | Qt::AlignLeft);

    // ---- 右：局数 + 特殊机制 + 链路时效 ----
    const QString round = hasGame
        ? QString("第 %1 / %2 局").arg(game.has_current_round() ? QString::number(game.current_round()) : "—")
              .arg(game.has_total_rounds() ? QString::number(game.total_rounds()) : "—")
        : "局数未提供";
    QString mechanism;
    if (match->ageMs(MatchState::Domain::SpecialMechanism) >= 0
        && match->mechanisms.mechanism_id_size() > 0) {
        QStringList items;
        for (int i = 0; i < match->mechanisms.mechanism_id_size(); ++i) {
            const qint32 seconds = i < match->mechanisms.mechanism_time_sec_size()
                ? match->mechanisms.mechanism_time_sec(i) : 0;
            items << status::mechanismText(match->mechanisms.mechanism_id(i), seconds);
        }
        mechanism = items.join("；");
    } else {
        mechanism = "无生效特殊机制";
    }
    const qreal rowHeight = rightCell.height() / 3.0;
    drawCaption(p, QRectF(rightCell.left(), rightCell.top(), rightCell.width(), rowHeight),
                round, theme::hud::text, base);
    drawCaption(p, QRectF(rightCell.left(), rightCell.top() + rowHeight, rightCell.width(), rowHeight),
                mechanism, theme::hud::muted, base);
    const QRectF linkRow(rightCell.left(), rightCell.top() + rowHeight*2, rightCell.width(), rowHeight);
    const qreal dotY = linkRow.center().y();
    drawLinkDot(p, QPointF(linkRow.left() + 4 * scale, dotY), "数据", mqttReady && dataAlive, mqttReady, scale);
    drawLinkDot(p, QPointF(linkRow.left() + 60 * scale, dotY), "图传", imageAlive, true, scale);
}
