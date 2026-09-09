#pragma once
#include "receiver.h"
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
#include <QTabWidget>
#include <QToolButton>

class VideoCanvas : public QWidget {
public:
    explicit VideoCanvas(QWidget *parent = nullptr);
    QImage image;
    rm::GameStatus data;
    bool hasData = false, stale = true, videoStale = true, overlay = true, simulation = true;
protected:
    void paintEvent(QPaintEvent *) override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString ffmpeg);
    void startConnection();
    bool saveEvidence(const QString &path);
    bool runUiChecks(const QString &evidencePrefix = {});
    QJsonObject metrics() const;
private:
    StatusReceiver receiver;
    VideoReceiver video;
    QElapsedTimer clock;
    qint64 lastData = -1, lastFrame = -1, fpsSampleAt = 0;
    quint64 messages = 0, fpsSampleFrames = 0;
    bool mqttReady = false, active = false;
    QString lastEvent;
    QComboBox *mode;
    QLineEdit *host, *bindIp, *robotId, *ffmpegPath;
    QSpinBox *mqttPort, *udpPort;
    QPushButton *connectButton, *stopButton, *fullScreenButton, *copyButton;
    QToolButton *advancedToggle, *logToggle;
    QCheckBox *overlay, *pauseLog;
    QLabel *connection, *dataState, *videoState, *liveBadge, *sourceBadge, *formError;
    QLabel *messageCount, *dataAge, *frameRate, *dropCount, *videoInfo, *packetInfo, *lastUpdate;
    QWidget *advanced, *logBody;
    QScrollArea *sidebar;
    QTabWidget *logTabs;
    QPlainTextEdit *log, *eventLog;
    VideoCanvas *canvas;
    QTimer ticker;
    bool validateForm();
    void addEvent(const QString &text);
    void appendLog(QPlainTextEdit *target, const QString &text);
    void toggleFullScreen();
    void refresh();
};
