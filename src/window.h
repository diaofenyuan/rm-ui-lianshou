#pragma once
#include "match_state.h"
#include "robot_link_pool.h"
#include "ui/console_page.h"
#include "ui/operator_page.h"
#include "video.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVector>

// 主窗口：全局比赛状态（MatchState）与两条链路的唯一持有者，负责配置栏、日志、
// 链路接线与两个模式页面的切换。两个模式共用同一条主连接与同一份状态模型。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString ffmpeg);
    void startConnection();
    void switchView();
    bool selectRobot(int id);
    bool saveEvidence(const QString &path);
    bool runUiChecks(const QString &evidencePrefix = {});
    QJsonObject metrics() const;
private:
    RobotLinkPool linkPool;
    VideoReceiver video;
    MatchState match;
    QElapsedTimer clock;
    qint64 lastData = -1, lastFrame = -1, fpsSampleAt = 0;
    quint64 messages = 0, fpsSampleFrames = 0;
    // focusMode = 专注（全屏 + 全部 HUD）；pureFullScreen = 纯全屏（无 HUD）。两者互斥。
    bool mqttReady = false, active = false, focusMode = false, pureFullScreen = false;
    bool simulation = true;                   // 当前画面来源：本地模拟 / 实机
    int connectedRobotId = 0;
    QString lastEvent;
    QComboBox *mode, *team, *robotRole;
    QLineEdit *host, *bindIp, *robotId, *ffmpegPath;
    QSpinBox *mqttPort, *udpPort;
    QPushButton *connectButton, *stopButton, *copyButton, *viewButton;
    QPushButton *settingsButton;
    QToolButton *advancedToggle, *logToggle;
    QCheckBox *pauseLog;
    QLabel *connection, *dataState, *videoState, *sourceBadge, *formError;
    QLabel *messageCount, *dataAge, *frameRate, *dropCount, *packetInfo, *lastUpdate;
    QLabel *operatorIdentity, *profileHint;
    QLabel *statusBadge, *statusMeta, *statusWarning;
    QVector<QLabel *> statusValues;
    QWidget *advanced, *logBody, *diagnostics, *logPanel, *statusPanel;
    QScrollArea *sidebar;
    QObject *wheelGuard;
    QStackedWidget *pages;
    ConsolePage *consolePage;
    OperatorPage *operatorPage;
    QTabWidget *logTabs;
    QPlainTextEdit *log, *eventLog;
    QTimer ticker;
    bool hasGameData() const;                 // 是否收到过 GameStatus
    bool dataStale() const;                   // 比赛信息是否过期（沿用 1.5 秒口径）
    bool videoStale() const;                  // 图传是否过期
    bool validateForm();
    void addEvent(const QString &text);
    void appendLog(QPlainTextEdit *target, const QString &text);
    void toggleFullScreen();
    void leaveFullscreenOrFocus();            // Esc 语义：先退专注，再退纯全屏
    void toggleMap();                         // M 语义：按当前模式切各自的地图
    void setFocusMode(bool enabled);
    void updateHudVisibility();
    void updateProfile();
    void recordMatchChanges(const rm::GameStatus &value);
    void refreshStatusDetails();
    void refresh();
    void updatePageChrome();
};
