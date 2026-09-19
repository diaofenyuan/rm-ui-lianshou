#pragma once

#include "match_state.h"
#include <QVector>
#include <QWidget>

// 总控模式我方全队列表：每行绑定一个机器人编号，单机域字段只从该编号的快照读取。
// 没有链路、已建链未收到、已过期三种状态明确区分，不用其他机器人的值填空。
class FleetPanel : public QWidget {
public:
    explicit FleetPanel(MatchState *state, QWidget *parent = nullptr);
    void setTeamBlue(bool blue);
    void setOwnRobot(int id);
    QString summaryText() const;
    int rowCount() const { return robotIds.size(); }
    QSize minimumSizeHint() const override { return {300, 390}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
    QVector<int> robotIds;
    int ownRobotId = 0;
    bool blue = false;
    QColor teamColor() const;
    static int healthSlot(int number);
};
