#include "link_pool_panel.h"
#include "theme.h"
#include <QFontMetrics>
#include <QPainter>

LinkPoolPanel::LinkPoolPanel(RobotLinkPool *value, QWidget *parent) : QWidget(parent), pool(value) {
    setAccessibleName("机器人连接池状态");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

int LinkPoolPanel::rowCount() const { return pool ? pool->robotIds().size() : 0; }

QString LinkPoolPanel::summaryText() const {
    if (!pool || pool->robotIds().isEmpty()) return "未建链";
    int ready = 0;
    for (const int id : pool->robotIds()) if (pool->linkStatus(id).ready) ++ready;
    return QString("%1/%2 在线 · %3 topics").arg(ready).arg(pool->robotIds().size()).arg(pool->subscribedTopicCount());
}

void LinkPoolPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::text, muted = theme::muted, good = theme::green, warning = theme::warning;
    const QFont nameFont = theme::font(11, true);
    const QFont detailFont = theme::font(10, false, true);
    const auto ids = pool ? pool->robotIds() : QVector<int>();
    const int gap = 2;
    const int rowHeight = ids.isEmpty() ? 26 : qMax(20, (height() - gap * (ids.size() - 1) - 2) / ids.size());
    for (int index = 0; index < ids.size(); ++index) {
        const int id = ids.at(index);
        const auto status = pool->linkStatus(id);
        const QRectF row(1, 1 + index * (rowHeight + gap), width() - 2, rowHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(status.ready ? QColor("#F1FAF7") : QColor("#F7F9FB"));
        painter.drawRoundedRect(row, 5, 5);
        painter.setFont(nameFont);
        painter.setPen(status.ready ? good : muted);
        const QString title = QString("ID %1%2").arg(id).arg(id == pool->primaryRobotId() ? " · 主" : " · 队友");
        painter.drawText(QRectF(row.left() + 8, row.top(), row.width() * .35, row.height()),
            Qt::AlignLeft | Qt::AlignVCenter, title);
        painter.setFont(detailFont);
        painter.setPen(ink);
        const QString age = status.lastMessageAgeMs < 0 ? "—" : status.lastMessageAgeMs < 1000
            ? QString("%1ms").arg(status.lastMessageAgeMs) : QString("%1s").arg(status.lastMessageAgeMs / 1000.0, 0, 'f', 1);
        const QString detail = QString("%1 topics · %2 条 · 最近 %3 · 重连 %4")
            .arg(status.subscribedTopics).arg(status.receivedMessages).arg(age).arg(status.reconnectCount);
        painter.drawText(QRectF(row.left() + row.width() * .34, row.top(), row.width() * .64 - 8, row.height()),
            Qt::AlignRight | Qt::AlignVCenter, QFontMetrics(detailFont).elidedText(detail, Qt::ElideLeft, int(row.width() * .64 - 8)));
    }
}
