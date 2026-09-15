#pragma once
#include "match_state.h"
#include <QWidget>

// 战场事件时间线：读取 MatchState 的统一时间线，最新一条显示在最上方。
// 条目来自协议事件、判罚、特殊机制与由状态变化推导的条目（比赛、机器人、模块、复活）。
class EventTimelinePanel : public QWidget {
public:
    explicit EventTimelinePanel(MatchState *state, QWidget *parent = nullptr);
    int rowCount() const;               // 当前可见条目数，供界面自检使用
    QSize minimumSizeHint() const override { return {260, 104}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
};
