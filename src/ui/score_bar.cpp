#include "score_bar.h"
#include "status.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>

namespace {
constexpr quint32 kBaseMaxHealth = 5000; // 协议不提供基地血量上限，按规则手册 5000 绘制比例条。

QString amount(quint32 value) { return QString("%L1").arg(value); }

void drawCaption(QPainter &p, const QRectF &rect, const QString &text, const QColor &color) {
    p.setFont(theme::font(11));
    p.setPen(color);
    p.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, text);
}
void drawValue(QPainter &p, const QRectF &rect, const QString &text, const QColor &color, int pixels) {
    // 数值过长时先缩字号再省略，避免越过相邻单元。
    auto font = theme::font(pixels, true, true);
    while (pixels > 12 && QFontMetrics(font).horizontalAdvance(text) > rect.width())
        font.setPixelSize(--pixels);
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter,
               QFontMetrics(font).elidedText(text, Qt::ElideRight, int(rect.width())));
}
void drawTrack(QPainter &p, const QRectF &rect, double ratio, const QColor &fill) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#E3EAF0"));
    p.drawRoundedRect(rect, 3, 3);
    if (ratio > 0) {
        p.setBrush(fill);
        QRectF front = rect;
        front.setWidth(rect.width() * qBound(0.0, ratio, 1.0));
        p.drawRoundedRect(front, 3, 3);
    }
}
}

ScoreBar::ScoreBar(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("总控台比分条：比分、阶段、倒计时、基地血量与经济");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(86);
}

void ScoreBar::setAllyBlue(bool blue) {
    if (allyBlue == blue) return;
    allyBlue = blue;
    update();
}

void ScoreBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QColor("#DAE2E8"));
    p.setBrush(QColor("#FFFFFF"));
    p.drawRoundedRect(area, 12, 12);

    const QColor ink = theme::text, muted = theme::muted;
    const QColor allyTeam = allyBlue ? theme::blue : theme::red;
    const QColor enemyTeam = allyBlue ? theme::red : theme::blue;
    const bool hasGame = match->ageMs(MatchState::Domain::Game) >= 0;
    const bool hasUnit = match->ageMs(MatchState::Domain::UnitStatus) >= 0;
    const bool hasLogistics = match->ageMs(MatchState::Domain::Logistics) >= 0;
    const bool gameStale = match->isStale(MatchState::Domain::Game);
    const bool unitStale = match->isStale(MatchState::Domain::UnitStatus);
    const QColor gameInk = hasGame && !gameStale ? ink : muted;
    const QColor unitInk = hasUnit && !unitStale ? ink : muted;
    const auto &game = match->game;
    const auto &unit = match->unitStatus;

    const QRectF content = area.adjusted(16, 10, -16, -10);
    const qreal weights[] = {1.15, 0.62, 1.3, 0.62, 1.15, 0.9};
    const qreal weightTotal = 5.74;
    const qreal gap = 10;
    const qreal usable = content.width() - gap * (int(std::size(weights)) - 1);
    qreal x = content.left();
    QRectF cells[6];
    for (int i = 0; i < 6; ++i) {
        const qreal width = usable * weights[i] / weightTotal;
        cells[i] = QRectF(x, content.top(), width, content.height());
        x += width + gap;
    }

    // 我方基地
    {
        const QRectF &cell = cells[0];
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 16),
                    unitStale && hasUnit ? "我方基地 · 过期" : "我方基地", allyTeam);
        QString text = hasUnit && unit.has_base_health() ? amount(unit.base_health()) : "—";
        if (hasUnit && unit.has_base_shield() && unit.base_shield() > 0)
            text += QString(" +%L1").arg(unit.base_shield());
        drawValue(p, QRectF(cell.left(), cell.top() + 17, cell.width(), 26), text, unitInk, 19);
        const double ratio = hasUnit && unit.has_base_health()
            ? unit.base_health() / double(kBaseMaxHealth) : 0;
        drawTrack(p, QRectF(cell.left() + 6, cell.bottom() - 11, cell.width() - 12, 6), ratio, allyTeam);
    }
    // 红方比分 / 中央阶段与倒计时 / 蓝方比分
    {
        const QRectF &cell = cells[1];
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 16), "红方比分", theme::red);
        drawValue(p, QRectF(cell.left(), cell.top() + 17, cell.width(), 30),
                  hasGame && game.has_red_score() ? amount(game.red_score()) : "—", gameInk, 24);
    }
    {
        const QRectF &cell = cells[2];
        QString roundText = "局数 —";
        if (hasGame && game.has_current_round() && game.has_total_rounds())
            roundText = QString("第 %1 / %2 局").arg(game.current_round()).arg(game.total_rounds());
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 15), roundText, muted);
        drawValue(p, QRectF(cell.left(), cell.top() + 16, cell.width(), 30),
                  hasGame && game.has_stage_countdown_sec() ? status::duration(game.stage_countdown_sec()) : "—",
                  gameInk, 26);
        QString stageText = "阶段 —";
        if (hasGame && game.has_current_stage()) {
            stageText = status::stage(game.current_stage());
            if (game.has_is_paused() && game.is_paused()) stageText += " · 已暂停";
        }
        drawCaption(p, QRectF(cell.left(), cell.bottom() - 16, cell.width(), 15), stageText, muted);
    }
    {
        const QRectF &cell = cells[3];
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 16), "蓝方比分", theme::blue);
        drawValue(p, QRectF(cell.left(), cell.top() + 17, cell.width(), 30),
                  hasGame && game.has_blue_score() ? amount(game.blue_score()) : "—", gameInk, 24);
    }
    // 敌方基地
    {
        const QRectF &cell = cells[4];
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 16),
                    unitStale && hasUnit ? "敌方基地 · 过期" : "敌方基地", enemyTeam);
        QString text = hasUnit && unit.has_enemy_base_health() ? amount(unit.enemy_base_health()) : "—";
        if (hasUnit && unit.has_enemy_base_shield() && unit.enemy_base_shield() > 0)
            text += QString(" +%L1").arg(unit.enemy_base_shield());
        drawValue(p, QRectF(cell.left(), cell.top() + 17, cell.width(), 26), text, unitInk, 19);
        const double ratio = hasUnit && unit.has_enemy_base_health()
            ? unit.enemy_base_health() / double(kBaseMaxHealth) : 0;
        drawTrack(p, QRectF(cell.left() + 6, cell.bottom() - 11, cell.width() - 12, 6), ratio, enemyTeam);
    }
    // 经济
    {
        const QRectF &cell = cells[5];
        const auto &logi = match->logistics;
        drawCaption(p, QRectF(cell.left(), cell.top(), cell.width(), 16),
                    hasLogistics ? "当前经济" : "当前经济 · 等待", muted);
        drawValue(p, QRectF(cell.left(), cell.top() + 17, cell.width(), 26),
                  hasLogistics && logi.has_remaining_economy() ? amount(logi.remaining_economy()) : "—",
                  hasLogistics ? ink : muted, 19);
        if (hasLogistics && logi.has_total_economy_obtained()) {
            p.setFont(theme::font(10));
            p.setPen(muted);
            p.drawText(QRectF(cell.left(), cell.bottom() - 15, cell.width(), 14),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       QString("累计 %L1").arg(logi.total_economy_obtained()));
        }
    }
}
