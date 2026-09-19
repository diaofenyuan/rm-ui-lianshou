#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

// 总控台面板容器：复用 QFrame[role=panel] 样式，标题行 + 可选行内摘要 + 内容区。
class ConsolePanel : public QFrame {
public:
    ConsolePanel(const QString &title, QWidget *content, QWidget *headerExtra = nullptr, QWidget *parent = nullptr)
        : QFrame(parent), contentWidget(content) {
        setProperty("role", "panel");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 10, 14, 12);
        layout->setSpacing(6);
        auto *header = new QHBoxLayout;
        header->setSpacing(8);
        auto *titleLabel = new QLabel(title);
        titleLabel->setProperty("role", "section");
        header->addWidget(titleLabel);
        header->addStretch();
        if (headerExtra) {
            // 摘要不能用文本自然宽度挤占主内容；完整内容仍可通过提示查看。
            headerExtra->setMinimumWidth(0);
            headerExtra->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            header->addWidget(headerExtra, 1);
        }
        layout->addLayout(header);
        if (content) layout->addWidget(content, 1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    bool hasHeightForWidth() const override {
        return contentWidget && contentWidget->hasHeightForWidth();
    }
    int heightForWidth(int width) const override {
        if (!contentWidget || !contentWidget->hasHeightForWidth()) return QFrame::heightForWidth(width);
        const auto margins = layout()->contentsMargins();
        const int contentWidth = qMax(1, width - margins.left() - margins.right());
        const int headerHeight = layout()->itemAt(0)->sizeHint().height();
        return margins.top() + headerHeight + layout()->spacing()
            + contentWidget->heightForWidth(contentWidth) + margins.bottom();
    }
private:
    QWidget *contentWidget = nullptr;
};
