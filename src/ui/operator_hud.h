#pragma once
#include "match_state.h"
#include "minimap.h"
#include "status_strip.h"
#include "teammate_panel.h"
#include <QWidget>

// 单兵模式 HUD 根控件：本身不绘制，只负责把四块信息件锚定在图传画面之内。
// 四锚：顶部信息条全宽 / 队友面板贴左 / 本机卡贴左下 / 全场地图贴右上。
// 几何在 resizeEvent 中手动定位（不走布局），因为布局会把 HUD 撑成整个父控件，
// 锚点随之失效；HUD 的父控件是 VideoStage，故本控件矩形恒等于画面矩形。
class OperatorHud : public QWidget {
    Q_OBJECT
public:
    explicit OperatorHud(MatchState *state, QWidget *parent = nullptr);

    void setRobot(int id);
    void setLinkState(bool ready, bool dataAlive, bool imageAlive);
    void setScale(qreal value);
    void setFocused(bool focused);          // 专注模式：整体略降不透明度，仍全部保留

    void setMapVisible(bool visible);
    bool mapVisible() const;
    void refresh();

    StatusStrip *strip() const { return statusStrip; }
    TeammatePanel *teammates() const { return teammatePanel; }
    OwnStatusCard *ownCard() const { return ownCardPanel; }
    MinimapPanel *map() const { return mapPanel; }

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    MatchState *match;
    StatusStrip *statusStrip;
    TeammatePanel *teammatePanel;
    OwnStatusCard *ownCardPanel;
    MinimapPanel *mapPanel;
    qreal scale = 1.0;
    bool focused = false;
};
