#pragma once
#include "match_state.h"
#include "operator_hud.h"
#include "video_stage.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

// 单兵模式页面：全屏图传 + 半透明 HUD。
// 本页持有单兵模式的业务状态（阵营、当前操作位、操作位铭牌、数据来源），
// 图传画面本身由 VideoStage 负责，HUD 由 OperatorHud 负责，两者职责不重叠。
class OperatorPage : public QWidget {
    Q_OBJECT
public:
    // 两条链路的时效口径：与 MatchState 的域阈值无关，只描述"界面现在能不能信这份数据"。
    struct LinkState {
        bool active = false;        // 是否已发起连接
        bool mqttReady = false;     // MQTT 已订阅
        bool dataStale = true;      // 比赛信息过期或从未收到
        bool videoStale = true;     // 图传过期或从未收到
        bool hasData = false;       // 是否收到过比赛信息
    };

    explicit OperatorPage(MatchState *state, QWidget *parent = nullptr);

    void setRobot(int id);
    void setFrame(const QImage &frame, bool stale);
    void setSimulation(bool simulation);
    void setLink(const LinkState &state);
    // 接收统计控件由 MainWindow 持有（标签内容与图传统计耦合），页面只负责摆放位置。
    void setDiagnostics(QWidget *widget);

    void setHudVisible(bool visible);
    bool hudVisible() const;
    void setMapVisible(bool visible);
    bool mapVisible() const;
    void toggleMap();
    void setFocused(bool focused);
    void refresh();

    VideoStage *stage() const { return videoStage; }
    OperatorHud *hud() const { return hudWidget; }
    StatusStrip *strip() const { return hudWidget->strip(); }
    TeammatePanel *teammates() const { return hudWidget->teammates(); }
    MinimapPanel *map() const { return hudWidget->map(); }
    QCheckBox *overlayToggle() const { return overlay; }
    QPushButton *focusButton() const { return focus; }
    QPushButton *fullScreenButton() const { return fullScreen; }
    QLabel *liveBadge() const { return live; }
    QLabel *notice() const { return noticeLabel; }
    QWidget *diagnostics() const { return diagnosticsPanel; }
    QString operatorName() const { return operatorLabel; }
    bool allyBlue() const { return ally; }
    int robotId() const { return currentRobotId; }

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    MatchState *match;
    VideoStage *videoStage;
    QWidget *stageRow;                 // 只负责把定尺寸的图传横向居中
    OperatorHud *hudWidget;
    QWidget *diagnosticsPanel = nullptr;
    QCheckBox *overlay;
    QPushButton *focus, *fullScreen;
    QLabel *live, *sourcePlate, *operatorPlate, *videoInfo, *noticeLabel;
    QHBoxLayout *headerRow, *toolRow;
    QVBoxLayout *body;

    int currentRobotId = 0;
    bool ally = false;
    bool simulation = true;
    bool focused = false;
    QString operatorLabel;
    LinkState link;

    int chromeHeight() const;
    void applyStageWidth();
    void updateOperatorPlate();
};
