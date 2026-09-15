#pragma once
#include "match_state.h"
#include "robot_list.h"
#include "score_bar.h"
#include <QLabel>
#include <QTimer>
#include <QWidget>

// 总控台页面：顶栏比分条 + 我方/敌方机器人列表 + 中央与底部占位面板。
// 数据域信号与 200ms 时效轮询都会触发本页刷新；小地图、事件、分析、预览按路线图后续接入。
class ConsolePage : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePage(MatchState *state, QWidget *parent = nullptr);
    void setRobot(int id);
    ScoreBar *scoreBar() const { return bar; }
    RobotListPanel *allyList() const { return ally; }
    RobotListPanel *enemyList() const { return enemy; }
private:
    MatchState *match;
    ScoreBar *bar;
    RobotListPanel *ally, *enemy;
    QLabel *allySummary, *enemySummary;
    QTimer ticker;
    void refresh();
};
