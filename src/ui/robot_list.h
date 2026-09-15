#pragma once
#include "match_state.h"
#include <QWidget>

// 我方/敌方机器人列表。血量来自 GlobalUnitStatus 的协议固定槽位（1/2/3/4/7 号）；
// 我方当前操作位所在行补充 RobotStatic/Dynamic/ModuleStatus 的热量、弹量、等级与主控状态，
// 操作位不在槽位内（如 5/6 号）时追加“本机”行。除本机外协议不提供血量上限，比例条仅对本机绘制。
class RobotListPanel : public QWidget {
public:
    RobotListPanel(MatchState *state, bool enemySide, QWidget *parent = nullptr);
    void setOwnRobot(int id);            // 0 表示未连接
    QString summaryText() const;         // 面板标题行的存活摘要
    int rowCount() const;
    QSize minimumSizeHint() const override { return {215, 170}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
    bool enemy;
    int ownNumber = 0;
    bool allyBlue = false;
    QColor teamColor() const;
};
