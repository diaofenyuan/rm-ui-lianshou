#pragma once
#include "match_state.h"
#include <QWidget>

// 复活状态面板：读条进度、免费复活与买活金币。数据缺失或过期时只标注状态，不推算剩余时间。
class RespawnPanel : public QWidget {
public:
    explicit RespawnPanel(MatchState *state, QWidget *parent = nullptr);
    QString statusText() const;         // 供界面自检使用
    QSize minimumSizeHint() const override { return {220, 68}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    MatchState *match;
};
