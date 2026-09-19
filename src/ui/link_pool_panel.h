#pragma once

#include "robot_link_pool.h"
#include <QWidget>

// 总控模式链路池状态面板：显示每个 Client ID 的订阅数、接收计数、最近消息年龄和绑定状态。
class LinkPoolPanel : public QWidget {
public:
    explicit LinkPoolPanel(RobotLinkPool *pool, QWidget *parent = nullptr);
    QString summaryText() const;
    int rowCount() const;
    QSize minimumSizeHint() const override { return {250, 150}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    RobotLinkPool *pool;
};
