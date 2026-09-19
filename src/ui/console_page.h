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
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

// 总控模式页面：顶部指挥条 + 我方/敌方机器人列表 + 中央战术地图 + 单路完整图传。
// 低频事件、分析、复活和连接诊断默认收进支援抽屉；数据域信号与 200ms 时效轮询都会触发本页刷新。
class ConsolePage : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePage(MatchState *state, RobotLinkPool *pool, QWidget *parent = nullptr);
    void setRobot(int id);
    void selectFocus(int id);
    int focusedRobot() const { return focusId; }
    int sourceRobot() const { return sourceId; }
    void cycleFocus();
    // M 键在总控模式进入/退出地图聚焦，保留地图并收起两侧低频信息。
    void setMapVisible(bool visible);
    void toggleMap();
    bool mapVisible() const;
    bool mapFocusMode() const { return mapFocus; }
    void setMapFocus(bool enabled);
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
    QComboBox *focusSelector() const { return focusCombo; }
    QWidget *supportDrawer() const { return drawer; }
    void openSupport(int index);
    void closeSupport();
signals:
    void reconnectRequested(int id);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    MatchState *match;
    ScoreBar *bar;
    FleetPanel *fleet;
    RobotListPanel *enemy;
    LinkPoolPanel *linkPanel;
    MinimapPanel *minimap;
    QWidget *mapPanel;
    QWidget *workArea = nullptr;
    QWidget *allyPanel = nullptr;
    QWidget *rightColumn = nullptr;
    QWidget *enemyPanel = nullptr;
    QWidget *videoContainer = nullptr;
    QWidget *commandBar = nullptr;
    QWidget *supportBar = nullptr;
    QWidget *drawer = nullptr;
    QStackedWidget *drawerStack = nullptr;
    QVector<QToolButton *> supportButtons;
    QToolButton *reconnectButton = nullptr;
    QToolButton *allyToggle = nullptr;
    QToolButton *enemyToggle = nullptr;
    QComboBox *focusCombo = nullptr;
    QLabel *focusMeta = nullptr;
    QLabel *videoMeta = nullptr;
    QLabel *supportSummary = nullptr;
    QLabel *healthSummary = nullptr;
    RobotLinkPool *pool = nullptr;
    int sourceId = 0, focusId = 0;
    bool mapFocus = false;
    RespawnPanel *respawnPanel;
    EventTimelinePanel *timeline;
    AnalysisPanel *analysisPanel;
    VideoPreviewPanel *videoPanel;
    QLabel *allySummary, *enemySummary, *analysisSummary, *linkSummary;
    QTimer ticker;
    void refresh();
    void updateFocusUi();
    void layoutWorkArea();
};
