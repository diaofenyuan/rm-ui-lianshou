#include "event_timeline.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>

namespace {
constexpr int kRowHeight = 20;

QColor categoryColor(MatchState::Category category) {
    switch (category) {
    case MatchState::Category::Match: return theme::blue;
    case MatchState::Category::Robot: return theme::green;
    case MatchState::Category::Mechanism: return theme::warning;
    case MatchState::Category::Penalty: return theme::red;
    case MatchState::Category::Event: return theme::muted;
    }
    return theme::muted;
}
}

EventTimelinePanel::EventTimelinePanel(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("战场事件时间线");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

int EventTimelinePanel::rowCount() const {
    const int rows = qMax(1, int(height()) / kRowHeight);
    return qMin(rows, match->timeline().size());
}

void EventTimelinePanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &entries = match->timeline();
    const int rows = qMax(1, int(height()) / kRowHeight);
    if (entries.isEmpty()) {
        p.setFont(theme::font(11));
        p.setPen(theme::muted);
        p.drawText(rect(), Qt::AlignCenter, "暂无事件");
        return;
    }
    const QFont stampFont = theme::font(10, false, true);
    const QFont textFont = theme::font(11);
    const int stampWidth = 62;
    for (int row = 0; row < rows; ++row) {
        const int index = entries.size() - 1 - row;   // 最新的排在最上方
        if (index < 0) break;
        const auto &entry = entries[index];
        const QRectF line(0, row * kRowHeight, width(), kRowHeight - 3);
        if (entry.alert) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(190, 52, 81, 18));
            p.drawRoundedRect(line, 4, 4);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(categoryColor(entry.category));
        p.drawRoundedRect(QRectF(line.left() + 2, line.top() + 4, 3, line.height() - 8), 1.5, 1.5);
        p.setFont(stampFont);
        p.setPen(theme::muted);
        p.drawText(QRectF(line.left() + 10, line.top(), stampWidth, line.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, entry.stamp);
        p.setFont(entry.alert ? theme::font(11, true) : textFont);
        p.setPen(entry.alert ? theme::red : theme::text);
        const QRectF text(line.left() + 10 + stampWidth, line.top(),
                          line.width() - stampWidth - 16, line.height());
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(p.font()).elidedText(entry.text, Qt::ElideRight, int(text.width())));
    }
}
