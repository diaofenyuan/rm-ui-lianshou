#pragma once
#include "match_state.h"
#include <QPointF>
#include <QPixmap>
#include <QVector>
#include <QWidget>

// 战术地图：官方场地底图 + 双方点位。
// 点位来源为雷达（触发式，双方槽位）与本机 RobotPosition（含朝向），
// 坐标口径与降级规则见 docs/console-design.md。协议未提供的数据一律不绘制。
class MinimapPanel : public QWidget {
public:
    explicit MinimapPanel(MatchState *state, QWidget *parent = nullptr);
    void setOwnRobot(int id);            // 0 表示未连接；蓝方视角在 id > 100 时启用
    // HUD 模式：叠在单兵模式图传之上时改用深色半透明卡底、紧凑页脚与浅色文字。
    // 底图与坐标换算与总控模式完全共用，不因显示场景改变口径。
    void setHudMode(bool enabled);
    bool hudMode() const { return hud; }
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
    bool hud = false;
    int ownNumber = 0;
    mutable QPixmap bitmap;              // 延迟加载的底图原图
    mutable QPixmap rendered;            // 按控件尺寸与视角缓存的底图
    mutable QSize renderedKey;
    mutable bool renderedBlue = false;
};
