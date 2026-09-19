#pragma once
#include "match_state.h"
#include <QWidget>

// 单兵模式顶部信息条：本机身份 / 存活 · 阶段与倒计时 · 局数、特殊机制、链路时效。
// 与总控台的比分条职责分离：本控件**不显示比分**（单兵模式不呈现比分，见改造计划 C7）。
// 暂停与结算时整条换色；倒计时 ≤10 秒且比赛中时转为紧急色。
class StatusStrip : public QWidget {
public:
    explicit StatusStrip(MatchState *state, QWidget *parent = nullptr);

    void setRobot(int id);                       // 0 表示未连接
    void setLinkState(bool ready, bool dataAlive, bool imageAlive);
    void setScale(qreal value);
    void setOpacity(qreal value);
    QString summaryText() const;                 // 供界面自检：本控件实际呈现的内容

protected:
    void paintEvent(QPaintEvent *) override;

private:
    MatchState *match;
    int ownNumber = 0;
    bool allyBlue = false;
    bool mqttReady = false, dataAlive = false, imageAlive = false;
    qreal scale = 1.0, opacity = 1.0;
};
