#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

// 总控台面板容器：复用 QFrame[role=panel] 样式，标题行 + 可选行内摘要 + 内容区。
class ConsolePanel : public QFrame {
public:
    ConsolePanel(const QString &title, QWidget *content, QWidget *headerExtra = nullptr, QWidget *parent = nullptr)
        : QFrame(parent) {
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
        if (headerExtra) header->addWidget(headerExtra);
        layout->addLayout(header);
        if (content) layout->addWidget(content, 1);
    }
};
