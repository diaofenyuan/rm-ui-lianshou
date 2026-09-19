#include "video_stage.h"
#include "theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

VideoStage::VideoStage(QWidget *parent) : QWidget(parent) {
    setAccessibleName("图传画面");
    // 页面会用 setFixedSize 按 16:9 直接给定尺寸；这里同时打开策略上的 heightForWidth，
    // 使"固定 16:9"这一约束在两个层面成立，将来页面改为自适应布局也不会破坏比例。
    QSizePolicy policy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    // 本控件每帧完整重绘自身，不依赖背景擦除；同时避免视频刷新时的整片闪烁。
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void VideoStage::setFrame(const QImage &frame, bool staleFrame) {
    image = frame;
    frameStale = staleFrame;
    update();
}

void VideoStage::setStale(bool stale) {
    if (frameStale == stale) return;
    frameStale = stale;
    update();
}

void VideoStage::setPlaceholder(bool isSimulation) {
    if (simulation == isSimulation) return;
    simulation = isSimulation;
    if (image.isNull()) update();
}

void VideoStage::setOverlay(QWidget *widget) {
    overlay = widget;
    if (!overlay) return;
    overlay->setParent(this);
    overlay->setGeometry(imageRect());
    overlay->raise();
}

// 控件本身固定 16:9（OperatorPage 通过最大宽度保证这一点），故画面矩形即控件矩形。
// 保留本函数是为了让 HUD 与空态提示只依赖一个几何来源，将来若允许非 16:9 窗口时
// 只需改这里，HUD 自动跟随。
QRect VideoStage::imageRect() const { return rect(); }

void VideoStage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (overlay) overlay->setGeometry(imageRect());
}

void VideoStage::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 本控件声明为不透明重绘，圆角之外的像素必须自己补上，否则会留下未初始化内容。
    p.fillRect(rect(), theme::pageBackground);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 8, 8);
    p.setClipPath(clip);
    p.fillRect(rect(), QColor("#142330"));

    if (!image.isNull()) {
        // 控件已是 16:9，画面按填充绘制即可；此处仍按矩形缩放，兼容上游传入的异常尺寸帧。
        p.drawImage(imageRect(), image);
    } else {
        p.setPen(QColor("#203440"));
        for (int x = 0; x < width(); x += 40) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 40) p.drawLine(0, y, width(), y);
        const QPoint center(width()/2, height()/2 + (height() < 240 ? -30 : 12));
        p.setPen(QPen(QColor("#94ACA9"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRect(center.x()-22, center.y()-34, 34, 27), 5, 5);
        p.drawPolyline(QPolygon({QPoint(center.x()+12, center.y()-26), QPoint(center.x()+24, center.y()-32),
                                QPoint(center.x()+24, center.y()-9), QPoint(center.x()+12, center.y()-15)}));
        p.setPen(QColor("#EDF4F7")); p.setFont(theme::font(19, true));
        p.drawText(QRect(16, center.y()+4, width()-32, 30), Qt::AlignCenter, "准备接收图传");
        p.setPen(QColor("#AEBDC8")); p.setFont(theme::font(12));
        p.drawText(QRect(16, center.y()+39, width()-32, 24), Qt::AlignCenter,
                   simulation ? "启动本地演示后，连接数据与图传" : "检查图传接线与本机监听 IP，然后连接");
    }

    if (frameStale && !image.isNull()) {
        // 断流提示贴在画面左上角，尽量少压画面内容；HUD 顶部条本身留出下沿空档。
        const QString text = "图传已中断 · 当前为最后一帧";
        p.setFont(theme::font(12));
        const int pillWidth = qMin(width()-24, p.fontMetrics().horizontalAdvance(text)+28);
        const QRect pill(12, 12, pillWidth, 30);
        p.setPen(Qt::NoPen); p.setBrush(QColor(32, 28, 23, 235));
        p.drawRoundedRect(pill, 6, 6);
        p.setPen(QColor("#F4D197"));
        p.drawText(pill, Qt::AlignCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, pillWidth-16));
    }
}
