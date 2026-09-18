#include "robot_list.h"
#include "operator_profile.h"
#include "status.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>

namespace {
struct Slot { int number; const char *name; };
constexpr Slot kSlots[] = {{1, "英雄"}, {2, "工程"}, {3, "步兵"}, {4, "步兵"}, {7, "哨兵"}};
bool inSlots(int number) {
    for (const auto &slot : kSlots) if (slot.number == number) return true;
    return false;
}
}

RobotListPanel::RobotListPanel(MatchState *state, bool enemySide, QWidget *parent)
    : QWidget(parent), match(state), enemy(enemySide) {
    setAccessibleName(enemy ? "敌方机器人列表" : "我方机器人列表");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RobotListPanel::setOwnRobot(int id) {
    const int number = (id >= 1 && id <= 9) || (id >= 101 && id <= 109) ? id % 100 : 0;
    if (ownNumber == number && allyBlue == (id > 100)) return;
    ownNumber = number;
    allyBlue = id > 100;
    update();
}

QString RobotListPanel::summaryText() const {
    int alive = 0, present = 0;
    for (int i = 0; i < 5; ++i) {
        const auto health = enemy ? match->enemyHealth(i) : match->allyHealth(i);
        if (health.has_value()) {
            ++present;
            if (*health > 0) ++alive;
        }
    }
    QString text = QString("存活 %1/5").arg(alive);
    if (present < 5) text += QString(" · 缺 %1").arg(5 - present);
    if (match->isStale(MatchState::Domain::UnitStatus)) text += " · 数据过期";
    return text;
}

int RobotListPanel::rowCount() const {
    const bool extra = !enemy && ownNumber >= 1 && !inSlots(ownNumber);
    return 5 + (extra ? 1 : 0);
}

QColor RobotListPanel::teamColor() const {
    const bool blue = enemy ? !allyBlue : allyBlue;
    return blue ? theme::blue : theme::red;
}

void RobotListPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool unitStale = match->isStale(MatchState::Domain::UnitStatus);
    const bool hasUnit = match->ageMs(MatchState::Domain::UnitStatus) >= 0;
    const bool ownFresh = !match->isStale(MatchState::Domain::RobotDynamic)
        && !match->isStale(MatchState::Domain::RobotStatic);
    const QColor ink = theme::text, muted = theme::muted, track{"#E3EAF0"};
    const QColor team = teamColor();
    const QFont nameFont = theme::font(12, true);
    const QFont detailFont = theme::font(11, false, true);
    const QFont numberFont = theme::font(11, false, true);
    const int normalHeight = 30, ownHeight = 46, gap = 6;
    const qreal w = width();

    const int rows = rowCount();
    qreal y = 1;
    for (int i = 0; i < rows; ++i) {
        const bool own = !enemy && ownNumber >= 1 && (i == 5 || ownNumber == kSlots[i].number);
        int number = 0;
        const char *roleName = "";
        if (i < 5) {
            number = kSlots[i].number;
            roleName = kSlots[i].name;
        } else {
            number = ownNumber;
            roleName = profile::roles[ownNumber - 1].name;
        }
        const qreal height = own ? ownHeight : normalHeight;

        std::optional<quint32> health;
        std::optional<quint32> maxHealth;
        if (i < 5) health = enemy ? match->enemyHealth(i) : match->allyHealth(i);
        else if (match->ageMs(MatchState::Domain::RobotDynamic) >= 0
                 && match->robotDynamic.has_current_health())
            health = match->robotDynamic.current_health();
        if (own && match->ageMs(MatchState::Domain::RobotStatic) >= 0
            && match->robotStatic.has_max_health())
            maxHealth = match->robotStatic.max_health();
        const bool destroyed = health.has_value() && *health == 0;

        if (own) {
            p.setPen(Qt::NoPen);
            p.setBrush(team);
            p.drawRoundedRect(QRectF(0, y + 5, 3, height - 10), 1.5, 1.5);
        }
        p.setFont(nameFont);
        p.setPen(unitStale && hasUnit ? muted : own ? team : ink);
        QString display = QString("%1 %2").arg(number).arg(QString::fromUtf8(roleName));
        if (i == 5) display += "（本机）";
        p.drawText(QRectF(10, y, 70, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(nameFont).elidedText(display, Qt::ElideRight, 70));

        const qreal numberWidth = 66;
        const qreal trackLeft = 84, trackRight = w - numberWidth - 8;
        if (trackRight > trackLeft + 20) {
            const QRectF trackRect(trackLeft, y + (own ? 5 : height / 2 - 3.5),
                                   trackRight - trackLeft, 7);
            const double ratio = health.has_value() && maxHealth.has_value() && *maxHealth > 0
                ? *health / double(*maxHealth) : 0;
            p.setPen(Qt::NoPen);
            p.setBrush(track);
            p.drawRoundedRect(trackRect, 3, 3);
            if (ratio > 0) {
                p.setBrush(team);
                QRectF front = trackRect;
                front.setWidth(trackRect.width() * qBound(0.0, ratio, 1.0));
                p.drawRoundedRect(front, 3, 3);
            }
            p.setFont(numberFont);
            const QColor numberInk = !health.has_value() ? muted
                : destroyed ? theme::red : unitStale && hasUnit ? muted : ink;
            const QString hpText = !health.has_value() ? "未提供"
                : destroyed ? "已阵亡" : QString("%L1").arg(*health);
            p.setPen(numberInk);
            p.drawText(QRectF(trackRight + 4, y, numberWidth, own ? 18 : height),
                       Qt::AlignRight | (own ? Qt::AlignTop : Qt::AlignVCenter), hpText);
        }
        if (own) {
            const auto &dyn = match->robotDynamic;
            const auto &stat = match->robotStatic;
            const auto &mod = match->robotModule;
            QString detail;
            if (dyn.has_current_heat()) {
                detail += QString("热量 %L1").arg(dyn.current_heat());
                if (stat.has_max_heat()) detail += QString("/%L1").arg(stat.max_heat());
            } else detail += "热量 —";
            detail += dyn.has_remaining_ammo() ? QString(" · 弹量 %L1").arg(dyn.remaining_ammo()) : " · 弹量 —";
            detail += stat.has_level() ? QString(" · Lv%1").arg(stat.level()) : " · Lv —";
            if (mod.has_main_controller()) detail += QString(" · 主控%1").arg(status::moduleState(mod.main_controller()));
            p.setFont(detailFont);
            p.setPen(muted);
            p.drawText(QRectF(10, y + 23, w - 16, 17), Qt::AlignLeft | Qt::AlignVCenter, detail);
        }
        y += height + gap;
    }
}
