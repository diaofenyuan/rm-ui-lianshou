#pragma once
#include "event_timeline.h"
#include "match_state.h"
#include "minimap.h"
#include "respawn_panel.h"
#include "robot_list.h"
#include "score_bar.h"
#include <QLabel>
#include <QTimer>
#include <QWidget>

// 总控台页面：顶栏比分条 + 我方/敌方机器人列表 + 中央战术地图与复活状态 + 底部事件、分析、图传预览。
// 数据域信号与 200ms 时效轮询都会触发本页刷新；分析与预览按路线图后续接入。
class ConsolePage : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePage(MatchState *state, QWidget *parent = nullptr);
    void setRobot(int id);
    ScoreBar *scoreBar() const { return bar; }
    RobotListPanel *allyList() const { return ally; }
    RobotListPanel *enemyList() const { return enemy; }
    MinimapPanel *map() const { return minimap; }
    RespawnPanel *respawnState() const { return respawnPanel; }
    EventTimelinePanel *events() const { return timeline; }
private:
    MatchState *match;
    ScoreBar *bar;
    RobotListPanel *ally, *enemy;
    MinimapPanel *minimap;
    RespawnPanel *respawnPanel;
    EventTimelinePanel *timeline;
    QLabel *allySummary, *enemySummary;
    QTimer ticker;
    void refresh();
};
