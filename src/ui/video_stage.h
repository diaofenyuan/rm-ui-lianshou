#pragma once
#include <QImage>
#include <QSize>
#include <QWidget>

// 纯图传绘制层：只负责画面本身，不持有任何业务状态（身份、比分、链路一律由
// 上层面板承担）。固定 16:9，使画面矩形可预测，HUD 才能稳定锚定在图传之内。
class VideoStage : public QWidget {
public:
    explicit VideoStage(QWidget *parent = nullptr);

    void setFrame(const QImage &frame, bool stale);
    void setStale(bool stale);              // 只更新断流标记，不重设画面
    void setPlaceholder(bool simulation);   // 空态文案随数据来源切换
    // HUD 层：作为本控件子控件存在，几何在 resizeEvent 中对齐到 imageRect()，
    // 不走布局叠放（布局会把 HUD 撑成窗口大小，锚点随之失效）。
    void setOverlay(QWidget *overlay);

    QRect imageRect() const;                // 画面实际绘制矩形，等于本控件矩形
    bool hasFrame() const { return !image.isNull(); }
    const QImage &frame() const { return image; }
    bool stale() const { return frameStale; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qRound(width * 9.0 / 16.0); }
    QSize sizeHint() const override { return {960, 540}; }
    QSize minimumSizeHint() const override { return {480, 270}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QImage image;
    QWidget *overlay = nullptr;
    bool frameStale = true;
    bool simulation = true;
};
