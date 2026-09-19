#pragma once
#include "match_state.h"
#include <QStringList>
#include <QWidget>

// 单兵模式左上的队友状态面板：按协议固定槽位 1/2/3/4/7 出 5 行。
// 血量取自全局域 GlobalUnitStatus.robot_health（无需连接池即可得），存活/阵亡由血量推导，
// 位置时效点取自雷达的己方槽位。热量/弹量/等级等单机域字段需要各自连接，
// 由批次 C 的连接池补齐后在本面板追加列（见改造计划 D4/D15），当前不预留空列。
class TeammatePanel : public QWidget {
public:
    explicit TeammatePanel(MatchState *state, QWidget *parent = nullptr);

    void setRobot(int id);               // 0 表示未连接
    void setScale(qreal value);
    void setOpacity(qreal value);
    int rowCount() const;                // 实际绘制的行数（固定 5 个协议槽位）
    QStringList rowTexts() const;        // 供界面自检

protected:
    void paintEvent(QPaintEvent *) override;

private:
    MatchState *match;
    int ownNumber = 0;
    bool allyBlue = false;
    qreal scale = 1.0, opacity = 1.0;
};

// 单兵模式左下角的「本机卡」：血量/热量（含上限）、弹量、等级、主控与累计发弹。
// 全部来自本机单机域（RobotStatic/Dynamic/ModuleStatus），无需连接池。
class OwnStatusCard : public QWidget {
public:
    explicit OwnStatusCard(MatchState *state, QWidget *parent = nullptr);

    void setRobot(int id);
    void setScale(qreal value);
    void setOpacity(qreal value);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    MatchState *match;
    int robotId = 0;
    qreal scale = 1.0, opacity = 1.0;
};
