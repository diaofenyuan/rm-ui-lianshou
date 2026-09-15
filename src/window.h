#pragma once
#include "match_state.h"
#include "receiver.h"
#include "ui/console_page.h"
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

class VideoCanvas : public QWidget {
public:
    explicit VideoCanvas(QWidget *parent = nullptr);
    QImage image;
    rm::GameStatus data;
    bool hasData = false, stale = true, videoStale = true, overlay = false, simulation = true;
    QString operatorName;
protected:
    void paintEvent(QPaintEvent *) override;
};

class MatchSummary : public QWidget {
public:
    explicit MatchSummary(const VideoCanvas *source);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    const VideoCanvas *source;
};

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
    StatusReceiver receiver;
    VideoReceiver video;
    MatchState match;
    QElapsedTimer clock;
    qint64 lastData = -1, lastFrame = -1, fpsSampleAt = 0;
    quint64 messages = 0, fpsSampleFrames = 0;
    bool mqttReady = false, active = false, focusMode = false;
    int connectedRobotId = 0;
    QString lastEvent;
    QComboBox *mode, *team, *robotRole;
    QLineEdit *host, *bindIp, *robotId, *ffmpegPath;
    QSpinBox *mqttPort, *udpPort;
    QPushButton *connectButton, *stopButton, *fullScreenButton, *copyButton, *focusButton;
    QToolButton *advancedToggle, *logToggle;
    QCheckBox *overlay, *pauseLog;
    QLabel *connection, *dataState, *videoState, *liveBadge, *sourceBadge, *formError;
    QLabel *messageCount, *dataAge, *frameRate, *dropCount, *videoInfo, *packetInfo, *lastUpdate;
    QLabel *operatorIdentity, *profileHint, *matchNotice;
    QLabel *statusBadge, *statusMeta, *statusWarning;
    QVector<QLabel *> statusValues;
    QWidget *advanced, *logBody, *diagnostics, *logPanel, *statusPanel;
    QScrollArea *sidebar;
    QStackedWidget *pages;
    ConsolePage *consolePage;
    QPushButton *viewButton;
    QTabWidget *logTabs;
    QPlainTextEdit *log, *eventLog;
    VideoCanvas *canvas;
    MatchSummary *matchSummary;
    QTimer ticker;
    bool validateForm();
    void addEvent(const QString &text);
    void appendLog(QPlainTextEdit *target, const QString &text);
    void toggleFullScreen();
    void setFocusMode(bool enabled);
    void updateProfile();
    void recordMatchChanges(const rm::GameStatus &value);
    void refreshStatusDetails();
    void refresh();
};
