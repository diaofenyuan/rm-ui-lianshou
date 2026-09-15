#pragma once
#include "match_state.h"
#include <QWidget>

// 数据分析面板：经济、敌我总伤害对比、本机累计发弹与分类受伤。
// 只展示协议字段，缺失显示"未提供"，不做估算；数值来源见 docs/console-design.md。
class AnalysisPanel : public QWidget {
public:
    explicit AnalysisPanel(MatchState *state, QWidget *parent = nullptr);
    void setAllyBlue(bool blue);
    QString statusText() const;         // 面板标题行摘要，同时供界面自检使用
    QSize minimumSizeHint() const override { return {260, 104}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
    bool allyBlue = false;
};
