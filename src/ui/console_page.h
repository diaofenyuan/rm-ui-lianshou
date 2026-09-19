#pragma once
#include "analysis_panel.h"
#include "event_timeline.h"
#include "match_state.h"
#include "minimap.h"
#include "fleet_panel.h"
#include "link_pool_panel.h"
#include "respawn_panel.h"
#include "robot_list.h"
#include "score_bar.h"
#include "video_preview.h"
#include <QLabel>
#include <QTimer>
#include <QWidget>

// 总控模式页面：顶栏比分条 + 我方/敌方机器人列表 + 中央战术地图与复活状态 + 底部事件、分析、图传与连接池。
// 数据域信号与 200ms 时效轮询都会触发本页刷新；图传帧由 MainWindow 投递。
class ConsolePage : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePage(MatchState *state, RobotLinkPool *pool, QWidget *parent = nullptr);
    void setRobot(int id);
    // M 键在本模式的落点：开关中央战术地图（单兵模式的 M 键切右上角 HUD 地图）。
    void setMapVisible(bool visible);
    void toggleMap();
    bool mapVisible() const;
    ScoreBar *scoreBar() const { return bar; }
    FleetPanel *allyList() const { return fleet; }
    FleetPanel *fleetPanel() const { return fleet; }
    LinkPoolPanel *linkPoolPanel() const { return linkPanel; }
    RobotListPanel *enemyList() const { return enemy; }
    MinimapPanel *map() const { return minimap; }
    RespawnPanel *respawnState() const { return respawnPanel; }
    EventTimelinePanel *events() const { return timeline; }
    AnalysisPanel *analysis() const { return analysisPanel; }
    VideoPreviewPanel *videoPreview() const { return videoPanel; }
private:
    MatchState *match;
    ScoreBar *bar;
    FleetPanel *fleet;
    RobotListPanel *enemy;
    LinkPoolPanel *linkPanel;
    MinimapPanel *minimap;
    QWidget *mapPanel;                   // 包裹 minimap 的面板容器：M 键隐藏的是整块，不是只藏图
    RespawnPanel *respawnPanel;
    EventTimelinePanel *timeline;
    AnalysisPanel *analysisPanel;
    VideoPreviewPanel *videoPanel;
    QLabel *allySummary, *enemySummary, *analysisSummary, *linkSummary;
    QTimer ticker;
    void refresh();
};
