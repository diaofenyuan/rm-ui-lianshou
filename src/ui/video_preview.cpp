#include "video_preview.h"
#include "theme.h"
#include <QPainter>

VideoPreviewPanel::VideoPreviewPanel(QWidget *parent) : QWidget(parent) {
    setAccessibleName("图传预览：主图传最新帧缩略图");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoPreviewPanel::setFrame(const QImage &image, bool videoStale) {
    stale = videoStale;
    if (image.isNull()) {
        if (frame.isNull()) return;
        frame = QImage();
        scaledFrame = -1;
    } else if (image.cacheKey() != frame.cacheKey()) {
        frame = image;               // QImage 隐式共享，拷贝开销可忽略
    }
}

void VideoPreviewPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QRectF area = rect();
    p.fillRect(area, QColor("#0E1620"));
    if (frame.isNull()) {
        p.setFont(theme::font(11));
        p.setPen(QColor("#9FB2C0"));
        p.drawText(area, Qt::AlignCenter, "等待图传");
        return;
    }
    const QSize key = area.size().toSize();
    if (scaledKey != key || scaledFrame != frame.cacheKey()) {
        scaledKey = key;
        scaledFrame = frame.cacheKey();
        scaled = QPixmap::fromImage(frame.scaled(key, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    p.drawPixmap(QPointF(area.left() + (area.width() - scaled.width()) / 2.0,
                         area.top() + (area.height() - scaled.height()) / 2.0), scaled);
    if (stale) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(14, 22, 32, 170));
        const QRectF badge(area.left() + 6, area.bottom() - 24, 132, 18);
        p.drawRoundedRect(badge, 4, 4);
        p.setFont(theme::font(10));
        p.setPen(QColor("#FFD28B"));
        p.drawText(badge, Qt::AlignCenter, "图传中断 · 最后一帧");
    }
}
