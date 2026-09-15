#pragma once
#include "match_state.h"
#include <QPointF>
#include <QVector>
#include <QWidget>

// 战术地图：自绘示意底图 + 双方点位。
// 点位来源为雷达（触发式，双方槽位）与本机 RobotPosition（含朝向），底图区域为示意画法，
// 坐标口径与降级规则见 docs/console-design.md。协议未提供的数据一律不绘制。
class MinimapPanel : public QWidget {
public:
    explicit MinimapPanel(MatchState *state, QWidget *parent = nullptr);
    void setOwnRobot(int id);            // 0 表示未连接；蓝方视角在 id > 100 时启用
    int markerCount() const;             // 供界面自检与证据使用
    bool hasAnyData() const;             // 是否收到过位置或雷达数据
    QSize minimumSizeHint() const override { return {260, 180}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    struct Marker {
        QPointF normalized;
        int number = 0;
        bool own = false, enemy = false, yaw = false;
        double yawDegrees = 0;
        int highlight = 0;               // 2.2.19 is_high_light：0 否 / 1 是 / 2 是但定位模块离线
    };
    QVector<Marker> markers() const;
    QColor teamColor(bool enemy) const;
    MatchState *match;
    bool allyBlue = false;
    int ownNumber = 0;
};
