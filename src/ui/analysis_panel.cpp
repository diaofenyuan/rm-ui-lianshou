#include "analysis_panel.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>

namespace {
QString amount(quint64 value) { return QString("%L1").arg(value); }
}

AnalysisPanel::AnalysisPanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("数据分析：经济、伤害与发弹量");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AnalysisPanel::setAllyBlue(bool blue) {
    if (allyBlue == blue) return;
    allyBlue = blue;
    update();
}

QString AnalysisPanel::statusText() const {
    const auto &logistics = match->logistics;
    const auto &unit = match->unitStatus;
    if (match->ageMs(MatchState::Domain::Logistics) < 0 && match->ageMs(MatchState::Domain::UnitStatus) < 0)
        return "等待数据";
    QStringList parts;
    if (logistics.has_remaining_economy()) parts += QString("经济 %1").arg(amount(logistics.remaining_economy()));
    if (unit.has_total_damage_ally() && unit.has_total_damage_enemy())
        parts += QString("总伤害 %1 / %2").arg(amount(unit.total_damage_ally()), amount(unit.total_damage_enemy()));
    if (match->isStale(MatchState::Domain::Logistics) && match->isStale(MatchState::Domain::UnitStatus))
        parts += "数据过期";
    return parts.isEmpty() ? QString("等待数据") : parts.join(" · ");
}

void AnalysisPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::text, muted = theme::muted;
    const QColor allyTeam = allyBlue ? theme::blue : theme::red;
    const QColor enemyTeam = allyBlue ? theme::red : theme::blue;
    const auto &logistics = match->logistics;
    const auto &unit = match->unitStatus;
    const auto &injury = match->injury;
    const bool hasLogistics = match->ageMs(MatchState::Domain::Logistics) >= 0;
    const bool hasUnit = match->ageMs(MatchState::Domain::UnitStatus) >= 0;
    const bool hasInjury = match->ageMs(MatchState::Domain::Injury) >= 0;
    const bool logisticsStale = hasLogistics && match->isStale(MatchState::Domain::Logistics);
    const bool unitStale = hasUnit && match->isStale(MatchState::Domain::UnitStatus);
    const bool injuryStale = hasInjury && match->isStale(MatchState::Domain::Injury);

    const auto cell = [&p, &muted](const QRectF &rect, const QString &label, const QString &value,
                                   const QColor &color, bool stale) {
        const QRectF labelRect(rect.left(), rect.top(), rect.width(), 15);
        const QRectF valueRect(rect.left(), rect.top() + 15, rect.width(), rect.height() - 15);
        p.setFont(theme::font(10));
        p.setPen(muted);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(p.font()).elidedText(label, Qt::ElideRight, int(labelRect.width())));
        auto font = theme::font(15, true, true);
        while (font.pixelSize() > 11 && QFontMetrics(font).horizontalAdvance(value) > valueRect.width())
            font.setPixelSize(font.pixelSize() - 1);
        p.setFont(font);
        p.setPen(stale ? muted : color);
        p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(font).elidedText(value, Qt::ElideRight, int(valueRect.width())));
    };
    const auto provided = [](bool present, const QString &value) {
        return present ? value : QString("未提供");
    };

    const double rowHeight = 34;
    const QRectF left(0, 0, width() * 0.46, height());
    const QRectF right(left.right() + 10, 0, width() - left.right() - 10, height());

    // 左列：经济与累计发弹。
    const bool hasBullets = hasUnit && unit.robot_bullets_size() > 0;
    quint64 bullets = 0;
    for (int i = 0; i < unit.robot_bullets_size(); ++i) bullets += quint64(qMax(0, unit.robot_bullets(i)));
    cell(QRectF(left.left(), left.top(), left.width() / 2 - 5, rowHeight), "剩余经济",
         provided(hasLogistics && logistics.has_remaining_economy(),
                  amount(logistics.remaining_economy())), ink, logisticsStale);
    cell(QRectF(left.left() + left.width() / 2 + 5, left.top(), left.width() / 2 - 5, rowHeight), "累计经济",
         provided(hasLogistics && logistics.has_total_economy_obtained(),
                  amount(logistics.total_economy_obtained())), ink, logisticsStale);
    cell(QRectF(left.left(), left.top() + rowHeight, left.width() / 2 - 5, rowHeight), "科技等级",
         provided(hasLogistics && logistics.has_tech_level(), amount(logistics.tech_level())), ink, logisticsStale);
    cell(QRectF(left.left() + left.width() / 2 + 5, left.top() + rowHeight, left.width() / 2 - 5, rowHeight),
         "加密等级", provided(hasLogistics && logistics.has_encryption_level(),
                             amount(logistics.encryption_level())), ink, logisticsStale);
    cell(QRectF(left.left(), left.top() + rowHeight * 2, left.width() - 5, rowHeight), "我方累计发弹",
         provided(hasBullets, amount(bullets)), ink, unitStale);

    // 右列：敌我总伤害对比与本机受伤分类。
    cell(QRectF(right.left(), right.top(), right.width() / 2 - 5, rowHeight), "我方总伤害",
         provided(hasUnit && unit.has_total_damage_ally(), amount(unit.total_damage_ally())), allyTeam, unitStale);
    cell(QRectF(right.left() + right.width() / 2 + 5, right.top(), right.width() / 2 - 5, rowHeight), "敌方总伤害",
         provided(hasUnit && unit.has_total_damage_enemy(), amount(unit.total_damage_enemy())), enemyTeam, unitStale);
    const QRectF track(right.left(), right.top() + rowHeight + 6, right.width(), 7);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#E3EAF0"));
    p.drawRoundedRect(track, 3.5, 3.5);
    const quint64 allyDamage = unit.has_total_damage_ally() ? unit.total_damage_ally() : 0;
    const quint64 enemyDamage = unit.has_total_damage_enemy() ? unit.total_damage_enemy() : 0;
    if (allyDamage + enemyDamage > 0) {
        const double share = allyDamage / double(allyDamage + enemyDamage);
        QRectF front = track;
        front.setWidth(track.width() * qBound(0.0, share, 1.0));
        p.setBrush(unitStale ? muted : allyTeam);
        p.drawRoundedRect(front, 3.5, 3.5);
        QRectF back = track;
        back.setLeft(front.right());
        p.setBrush(enemyTeam.lighter(150));
        p.drawRoundedRect(back, 3.5, 3.5);
    }

    // 本机受伤明细分两行，避免在窄面板里被省略号截断。
    p.setFont(theme::font(10));
    p.setPen(muted);
    const QRectF detail(right.left(), right.top() + rowHeight + 18, right.width(), 15);
    const QRectF categories(right.left(), detail.bottom(), right.width(), 15);
    p.drawText(detail, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(
                   hasInjury ? QString("本机受伤 %1%2").arg(
                                   amount(injury.has_total_damage() ? injury.total_damage() : 0),
                                   injuryStale ? "（过期）" : "")
                             : QString("本机受伤 未提供"),
                   Qt::ElideRight, int(detail.width())));
    if (!hasInjury) return;
    const QString categoryText = QString("17mm %1 · 42mm %2 · 碰撞 %3 · 离线 %4")
        .arg(amount(injury.has_small_projectile_damage() ? injury.small_projectile_damage() : 0),
             amount(injury.has_large_projectile_damage() ? injury.large_projectile_damage() : 0),
             amount(injury.has_collision_damage() ? injury.collision_damage() : 0),
             amount(injury.has_offline_damage() ? injury.offline_damage() : 0));
    p.drawText(categories, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(categoryText, Qt::ElideRight, int(categories.width())));
}
