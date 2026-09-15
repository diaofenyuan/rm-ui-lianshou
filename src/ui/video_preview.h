#pragma once
#include <QImage>
#include <QPixmap>
#include <QWidget>

// 图传预览：显示主图传最新解码帧的缩略图，不建立第二条接收链路。
// 帧由 MainWindow 在刷新时投递；中断时保留最后一帧并标注。
class VideoPreviewPanel : public QWidget {
public:
    explicit VideoPreviewPanel(QWidget *parent = nullptr);
    void setFrame(const QImage &image, bool stale);
    bool hasFrame() const { return !frame.isNull(); }
    QSize minimumSizeHint() const override { return {220, 104}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QImage frame;
    bool stale = true;
    QPixmap scaled;                  // 按控件尺寸缓存的缩略图
    QSize scaledKey;
    qint64 scaledFrame = -1;
};
