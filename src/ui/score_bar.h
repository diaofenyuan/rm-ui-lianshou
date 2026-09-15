#pragma once
#include "match_state.h"
#include <QWidget>

// 总控台顶栏：比分、局数、阶段与倒计时、双方基地血量（含护盾）、当前经济。
// 数据全部来自 MatchState；基地血量上限协议未提供，按规则手册 5000 绘制比例条。
class ScoreBar : public QWidget {
public:
    explicit ScoreBar(MatchState *state, QWidget *parent = nullptr);
    void setAllyBlue(bool blue);
    QSize minimumSizeHint() const override { return {500, 86}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
    bool allyBlue = false;
};
