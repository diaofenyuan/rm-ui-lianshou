#include "window.h"
#include "theme.h"
#include "operator_profile.h"
#include <QApplication>
#include <QAbstractSpinBox>
#include <QClipboard>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QPainter>
#include <QScrollBar>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

namespace {
// 比赛信息与图传的过期阈值：两者都用 1.5 秒口径，与 MatchState 的快速域一致。
constexpr qint64 kStaleMs = 1500;

void chevron(QPainter &p, const QPointF &center, bool up = false) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::muted, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal direction = up ? -1 : 1;
    p.drawPolyline(QPolygonF({center+QPointF(-4, -2*direction), center+QPointF(0, 2*direction), center+QPointF(4, -2*direction)}));
}
QIcon disclosureIcon(bool open) {
    QPixmap pixels(32, 32); pixels.setDevicePixelRatio(2); pixels.fill(Qt::transparent);
    QPainter p(&pixels);
    if (!open) { p.translate(8, 8); p.rotate(-90); p.translate(-8, -8); }
    chevron(p, QPointF(8, 8)); return QIcon(pixels);
}
// 原生控件保留键盘和无障碍行为，仅补绘样式表下不稳定的系统箭头。
class ModeCombo final : public QComboBox {
protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event); QPainter p(this); chevron(p, QPointF(width()-15, height()/2.0));
    }
};
class PortSpinBox final : public QSpinBox {
protected:
    void paintEvent(QPaintEvent *event) override {
        QSpinBox::paintEvent(event); QPainter p(this);
        chevron(p, QPointF(width()-11, height()/4.0), true); chevron(p, QPointF(width()-11, height()*3/4.0));
    }
};
// 滚轮停在组合框或端口框上会直接改值，而配置栏本身要滚动才能看到端口、FFmpeg 路径等内容：
// 这里把落在这些控件上的滚轮一律转交给配置栏滚动，控件仍可通过点击展开与方向键修改。
class WheelGuard final : public QObject {
public:
    explicit WheelGuard(QAbstractScrollArea *area, QObject *parent = nullptr) : QObject(parent), area(area) {}
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::Wheel || !area) return false;
        QApplication::sendEvent(area->verticalScrollBar(), event);
        return true;
    }
private:
    QAbstractScrollArea *area;
};
QLabel *label(const QString &text, const char *role = nullptr) {
    auto *value = new QLabel(text);
    if (role) value->setProperty("role", role);
    return value;
}
QFrame *panel() {
    auto *frame = new QFrame;
    frame->setProperty("role", "panel");
    return frame;
}
void badge(QLabel *target, const QString &text, const char *tone = "neutral") {
    target->setText(text);
    if (target->property("tone").toString() == tone) return;
    target->setProperty("tone", tone);
    target->style()->unpolish(target);
    target->style()->polish(target);
}
void field(QVBoxLayout *layout, const QString &text, QWidget *control) {
    auto *caption = label(text);
    caption->setBuddy(control);
    control->setAccessibleName(text);
    layout->addWidget(caption);
    layout->addWidget(control);
}
}

MainWindow::MainWindow(QString ffmpeg) {
    setWindowTitle("RoboMaster · 单兵客户端");
    // 最小高度需保证图传不被下方工具行与日志压住（自检项"最小窗口布局"），
    // 宽度按"配置栏 + 单兵模式 HUD"的最小需求给。
    setMinimumSize(1024, 720);
    // 默认按可用屏幕取景：1366×768 / 1280×720 上不再把配置栏底部（端口、FFmpeg 路径）顶到屏幕外。
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1360, int(available.width() * 0.94)), qMin(840, int(available.height() * 0.94)));
    setStyleSheet(theme::stylesheet());
    auto *root = new QWidget; root->setObjectName("root"); setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(22, 18, 22, 18); layout->setSpacing(16);
    auto *header = new QHBoxLayout; header->setSpacing(12); layout->addLayout(header);
    auto *brand = label("RM"); brand->setObjectName("brand"); brand->setAlignment(Qt::AlignCenter); brand->setFixedSize(44, 44); header->addWidget(brand);
    auto *titles = new QVBoxLayout; titles->setSpacing(3); header->addLayout(titles);
    titles->addWidget(label("RoboMaster 单兵客户端", "title"));
    operatorIdentity = label("红方 · 3 号步兵 / 待连接", "muted"); titles->addWidget(operatorIdentity);
    header->addStretch();
    viewButton = new QPushButton("单兵模式  Ctrl+Tab");
    viewButton->setToolTip("在总控模式与单兵模式之间切换");
    header->addWidget(viewButton, 0, Qt::AlignVCenter);
    sourceBadge = label("本地模拟", "badge"); header->addWidget(sourceBadge, 0, Qt::AlignVCenter);
    header->addWidget(label("RM2026 · V2.0.0", "muted"));

    auto *body = new QHBoxLayout; body->setSpacing(16); layout->addLayout(body, 1);
    pages = new QStackedWidget; body->addWidget(pages, 1);
    consolePage = new ConsolePage(&match, &linkPool); pages->addWidget(consolePage);
    operatorPage = new OperatorPage(&match); pages->addWidget(operatorPage);
    // 接收统计仍由主窗口持有（内容与图传/接收计数耦合），但摆在单兵模式页面内部，
    // 这样专注模式隐藏它时不会连带影响总控模式。
    diagnostics = new QWidget; auto *stats = new QHBoxLayout(diagnostics); stats->setContentsMargins(0, 0, 0, 0);
    stats->setSpacing(16);
    auto metric = [stats](const QString &caption, const QString &value) {
        auto *column = new QVBoxLayout; column->setSpacing(4); stats->addLayout(column, 1);
        column->addWidget(label(caption, "muted")); auto *number = label(value, "metric"); column->addWidget(number); return number;
    };
    messageCount = metric("已收比赛信息", "0"); dataAge = metric("距最近更新", "—");
    dataAge->setToolTip("自最近一次收到 GameStatus 起经过的时间，不代表网络延迟");
    frameRate = metric("解码帧率", "0 fps"); dropCount = metric("丢弃视频帧", "0");
    operatorPage->setDiagnostics(diagnostics);

    sidebar = new QScrollArea; sidebar->setObjectName("sidebar"); sidebar->setFrameShape(QFrame::NoFrame);
    // 固定 312 会把总控模式顶出窄屏；改为可自适应，横向滚动条仍关闭。
    sidebar->setWidgetResizable(true); sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setMinimumWidth(280); sidebar->setMaximumWidth(360);
    sidebar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    body->addWidget(sidebar);
    auto *side = new QWidget; side->setObjectName("side"); sidebar->setWidget(side);
    auto *sideLayout = new QVBoxLayout(side); sideLayout->setContentsMargins(0, 0, 4, 0); sideLayout->setSpacing(14);
    auto *settings = panel(); sideLayout->addWidget(settings);
    auto *form = new QVBoxLayout(settings); form->setContentsMargins(16, 16, 16, 16); form->setSpacing(8);
    form->addWidget(label("选择操作位", "section"));
    auto *hint = label("选择阵营和兵种，连接对应机器人。", "muted"); hint->setWordWrap(true); form->addWidget(hint);
    team = new ModeCombo; team->addItems({"红方", "蓝方"}); field(form, "所属阵营", team);
    robotRole = new ModeCombo;
    for (const auto &role : profile::roles) robotRole->addItem(QString("%1 号 · %2").arg(role.number).arg(QString::fromUtf8(role.name)), role.number);
    robotRole->setCurrentIndex(2); field(form, "兵种 / 编号", robotRole);
    profileHint = label("", "muted"); profileHint->setWordWrap(true); form->addWidget(profileHint);
    mode = new ModeCombo; mode->addItems({"本地模拟", "连接实机"}); field(form, "数据来源", mode);
    host = new QLineEdit("127.0.0.1"); host->setPlaceholderText("服务端 IP 或主机名");
    robotId = new QLineEdit("3"); robotId->setReadOnly(true); robotId->setToolTip("根据阵营和兵种自动生成，需与官方选手端登录编号一致");
    bindIp = new QLineEdit("127.0.0.1"); ffmpegPath = new QLineEdit(ffmpeg);
    mqttPort = new PortSpinBox; mqttPort->setRange(1, 65535); mqttPort->setValue(3333);
    udpPort = new PortSpinBox; udpPort->setRange(1, 65535); udpPort->setValue(3334);
    advancedToggle = new QToolButton; advancedToggle->setText("连接参数"); advancedToggle->setCheckable(true);
    advancedToggle->setIcon(disclosureIcon(false)); advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); form->addWidget(advancedToggle);
    advanced = new QWidget; auto *advancedLayout = new QVBoxLayout(advanced); advancedLayout->setContentsMargins(0, 0, 0, 4); advancedLayout->setSpacing(9);
    field(advancedLayout, "MQTT 服务端", host); field(advancedLayout, "机器人 ID（自动生成）", robotId);
    auto *ports = new QHBoxLayout; advancedLayout->addLayout(ports);
    auto *mqttColumn = new QVBoxLayout; auto *udpColumn = new QVBoxLayout; ports->addLayout(mqttColumn); ports->addLayout(udpColumn);
    field(mqttColumn, "MQTT 端口", mqttPort); field(udpColumn, "图传端口", udpPort);
    field(advancedLayout, "本机监听 IP", bindIp); field(advancedLayout, "FFmpeg 程序", ffmpegPath);
    ffmpegPath->setToolTip(ffmpeg); auto *browse = new QPushButton("选择程序…"); advancedLayout->addWidget(browse);
    form->addWidget(advanced); advanced->hide();
    formError = label(""); formError->setObjectName("formError"); formError->setWordWrap(true); form->addWidget(formError); formError->hide();
    connectButton = new QPushButton("连接所选机器人"); connectButton->setProperty("role", "primary"); form->addWidget(connectButton);
    stopButton = new QPushButton("断开连接"); stopButton->setEnabled(false); form->addWidget(stopButton);

    auto *health = panel(); sideLayout->addWidget(health);
    auto *healthLayout = new QVBoxLayout(health); healthLayout->setContentsMargins(16, 16, 16, 16); healthLayout->setSpacing(8);
    healthLayout->addWidget(label("链路状态", "section"));
    auto healthRow = [healthLayout](const QString &caption) {
        auto *row = new QHBoxLayout; healthLayout->addLayout(row); row->addWidget(label(caption)); row->addStretch();
        auto *value = label("未连接", "badge"); row->addWidget(value); return value;
    };
    dataState = healthRow("比赛信息"); videoState = healthRow("图传画面");
    connection = label("等待连接", "muted"); connection->setWordWrap(true); healthLayout->addWidget(connection);
    packetInfo = label("UDP 0 包 · 无效包 0", "muted"); packetInfo->setWordWrap(true); healthLayout->addWidget(packetInfo);
    lastUpdate = label("最近接收 —", "muted"); healthLayout->addWidget(lastUpdate); sideLayout->addStretch();

    statusPanel = panel(); statusPanel->setAccessibleName("GameStatus 比赛全局信息");
    sideLayout->insertWidget(sideLayout->count() - 1, statusPanel);
    auto *statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(16, 16, 16, 16); statusLayout->setSpacing(8);
    auto *statusHeader = new QHBoxLayout; statusLayout->addLayout(statusHeader);
    statusHeader->addWidget(label("GameStatus 全局信息", "section"));
    statusBadge = label("未接收", "badge"); statusHeader->addWidget(statusBadge, 0, Qt::AlignVCenter); statusHeader->addStretch();
    statusMeta = label("协议 RM2026-V2.0.0 · 已提供 0 / 10 字段", "muted");
    statusMeta->setWordWrap(true); statusLayout->addWidget(statusMeta);
    auto *statusGrid = new QGridLayout; statusGrid->setHorizontalSpacing(12); statusGrid->setVerticalSpacing(8);
    const QStringList statusCaptions = {"当前局号", "总局数", "红方得分", "蓝方得分", "当前阶段",
        "阶段倒计时", "阶段已过", "是否暂停", "当局胜者", "结束原因"};
    for (int i = 0; i < statusCaptions.size(); ++i) {
        const int row = i / 2, col = i % 2;
        // 字段名在上、值在下：值文本不参与最小宽度，长枚举不会把侧栏撑出视口。
        auto *cell = new QVBoxLayout; cell->setSpacing(2);
        auto *value = label("未提供");
        value->setFont(theme::font(12, true)); value->setMinimumWidth(1);
        value->setToolTip(statusCaptions[i] + "（原始 GameStatus 字段）");
        cell->addWidget(label(statusCaptions[i], "muted")); cell->addWidget(value);
        statusGrid->addLayout(cell, row, col);
        statusValues.append(value);
    }
    statusLayout->addLayout(statusGrid);
    statusWarning = label("", "notice"); statusWarning->setWordWrap(true); statusWarning->hide(); statusLayout->addWidget(statusWarning);
    // 该面板放在链路状态下方，始终保留十个原始字段的可见落点；日志仍用于查看完整 JSON。
    refreshStatusDetails();

    // 配置栏内的下拉框与端口框不再响应滚轮，滚轮一律用于滚动配置栏本身。
    wheelGuard = new WheelGuard(sidebar, this);
    for (auto *control : side->findChildren<QComboBox *>()) control->installEventFilter(wheelGuard);
    for (auto *control : side->findChildren<QSpinBox *>()) control->installEventFilter(wheelGuard);

    logPanel = panel(); logPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum); layout->addWidget(logPanel);
    auto *logLayout = new QVBoxLayout(logPanel); logLayout->setContentsMargins(12, 5, 12, 5); logLayout->setSpacing(4);
    auto *logHeader = new QHBoxLayout; logLayout->addLayout(logHeader);
    logToggle = new QToolButton; logToggle->setText("接收日志"); logToggle->setCheckable(true);
    logToggle->setIcon(disclosureIcon(false)); logToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); logHeader->addWidget(logToggle);
    logHeader->addWidget(label("GameStatus / 运行事件", "muted")); logHeader->addStretch();
    pauseLog = new QCheckBox("暂停滚动"); pauseLog->setToolTip("只暂停自动滚动，比赛信息仍持续接收"); logHeader->addWidget(pauseLog); pauseLog->hide();
    copyButton = new QPushButton("复制当前日志"); logHeader->addWidget(copyButton); copyButton->hide();
    logBody = new QWidget; auto *logBodyLayout = new QVBoxLayout(logBody); logBodyLayout->setContentsMargins(0, 0, 0, 7);
    logTabs = new QTabWidget; logTabs->setFixedHeight(120); logBodyLayout->addWidget(logTabs);
    log = new QPlainTextEdit; eventLog = new QPlainTextEdit;
    for (auto *item : {log, eventLog}) { item->setReadOnly(true); item->setMaximumBlockCount(100); item->setFixedHeight(78); }
    log->setPlaceholderText("连接后显示收到的完整 GameStatus JSON，最近保留 100 条。");
    eventLog->setPlaceholderText("比赛阶段、暂停、比分变化以及连接事件会显示在这里。");
    log->setAccessibleName("比赛信息 JSON 日志"); eventLog->setAccessibleName("运行事件日志");
    logTabs->addTab(log, "比赛信息 · JSON"); logTabs->addTab(eventLog, "比赛动态 / 运行事件");
    logLayout->addWidget(logBody); logBody->hide();

    connect(advancedToggle, &QToolButton::toggled, this, [this](bool open) {
        advanced->setVisible(open); advancedToggle->setIcon(disclosureIcon(open));
    });
    connect(logToggle, &QToolButton::toggled, this, [this](bool open) {
        logBody->setVisible(open); pauseLog->setVisible(open); copyButton->setVisible(open);
        diagnostics->setVisible(!open && !focusMode);
        logToggle->setIcon(disclosureIcon(open));
    });
    connect(pauseLog, &QCheckBox::toggled, this, [this](bool paused) {
        if (!paused) for (auto *item : {log, eventLog}) item->verticalScrollBar()->setValue(item->verticalScrollBar()->maximum());
    });
    connect(copyButton, &QPushButton::clicked, this, [this] {
        auto *current = qobject_cast<QPlainTextEdit *>(logTabs->currentWidget());
        QApplication::clipboard()->setText(current->toPlainText()); copyButton->setText("已复制");
        QTimer::singleShot(1500, copyButton, [this] { copyButton->setText("复制当前日志"); });
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "选择 FFmpeg 程序", ffmpegPath->text(), "可执行程序 (*.exe);;所有文件 (*)");
        if (!path.isEmpty()) { ffmpegPath->setText(path); ffmpegPath->setToolTip(path); }
    });
    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        host->setText(i ? "192.168.12.1" : "127.0.0.1"); bindIp->setText(i ? "192.168.12.2" : "127.0.0.1");
        // 来源标签跟随正在使用的连接，避免只改下拉框就把模拟帧标为实机。
        if (!active && !operatorPage->stage()->hasFrame() && !hasGameData()) {
            simulation = i == 0;
            operatorPage->setSimulation(simulation);
        }
        mode->setToolTip(active ? "参数将在重新连接后生效" : "选择本地模拟或实机链路"); refresh();
    });
    connect(team, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateProfile);
    connect(robotRole, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateProfile);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::startConnection);
    connect(stopButton, &QPushButton::clicked, this, [this] {
        active = false; linkPool.stop(); video.stop(); mqttReady = false; lastData = lastFrame = -1; match.reset();
        stopButton->setEnabled(false); frameRate->setText("0 fps"); updateProfile(); refresh();
    });
    connect(operatorPage->overlayToggle(), &QCheckBox::toggled, this, [this](bool) { updateHudVisibility(); refresh(); });
    connect(&linkPool, &RobotLinkPool::stateChanged, this, [this](const QString &text, bool ready) {
        connection->setText(text); mqttReady = ready; addEvent(text); refresh();
    });
    connect(&linkPool, &RobotLinkPool::linkChanged, this,
        [this](int id, bool ready, const QString &text, int subscribedTopics) {
            match.noteRobotLink(id, QString("mqtt://%1").arg(id));
            if (id != connectedRobotId && !text.isEmpty())
                addEvent(QString("机器人 %1：%2（%3 topics）").arg(id).arg(text).arg(subscribedTopics));
            Q_UNUSED(ready);
        });
    connect(&linkPool, &RobotLinkPool::received, this, [this](const rm::GameStatus &value) {
        // 先与上一份快照比较再写入，否则变化检测会把新值和新值比。
        recordMatchChanges(value);
        match.applyGame(value);
        for (const auto &issue : status::warnings(value)) addEvent("协议警告：" + issue);
        lastData = clock.elapsed(); ++messages;
        auto object = status::json(value); object["received_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const auto text = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
        appendLog(log, text); QTextStream(stdout) << text << Qt::endl;
        lastUpdate->setText("最近接收 " + QDateTime::currentDateTime().toString("HH:mm:ss"));
        // 单兵模式的无障碍描述也不能泄漏比分；比分只属于总控模式的 ScoreBar。
        operatorPage->strip()->setAccessibleDescription(QString("%1，剩余 %2")
            .arg(value.has_current_stage() ? status::stage(value.current_stage()) : "阶段未提供")
            .arg(value.has_stage_countdown_sec() ? status::duration(value.stage_countdown_sec()) : "未提供"));
        refresh();
    });
    connect(&video, &VideoReceiver::frameReady, this, [this](QImage frame) {
        lastFrame = clock.elapsed();
        operatorPage->stage()->setFrame(frame, false);
    });
    // 数据域：全部进入 MatchState；慢速/触发式域同时落 JSON 日志，10Hz 动态域只进模型。
    const auto appendDomain = [this](QJsonObject object) {
        object["received_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        appendLog(log, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    };
    connect(&linkPool, &RobotLinkPool::receivedUnitStatus, this, [this, appendDomain](const rm::GlobalUnitStatus &value) {
        match.applyUnitStatus(value); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedLogistics, this, [this, appendDomain](const rm::GlobalLogisticsStatus &value) {
        match.applyLogistics(value); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedSpecialMechanism, &match, &MatchState::applySpecialMechanism);
    connect(&linkPool, &RobotLinkPool::receivedEvent, this, [this, appendDomain](const rm::Event &value) {
        match.applyEvent(value); addEvent(status::eventText(value)); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedInjury, this, [this, appendDomain](int id, const rm::RobotInjuryStat &value) {
        match.applyInjury(id, value); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedRespawn, this, [this, appendDomain](int id, const rm::RobotRespawnStatus &value) {
        match.applyRespawn(id, value); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedStatic, this, [this, appendDomain](int id, const rm::RobotStaticStatus &value) {
        match.applyStatic(id, value); appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedDynamic, this, [this](int id, const rm::RobotDynamicStatus &value) {
        match.applyDynamic(id, value);
    });
    connect(&linkPool, &RobotLinkPool::receivedModule, this, [this](int id, const rm::RobotModuleStatus &value) {
        match.applyModule(id, value);
    });
    connect(&linkPool, &RobotLinkPool::receivedPosition, this, [this](int id, const rm::RobotPosition &value) {
        match.applyPosition(id, value);
    });
    connect(&linkPool, &RobotLinkPool::receivedBuff, this, [this](int id, const rm::Buff &value) {
        match.applyBuff(id, value);
    });
    connect(&linkPool, &RobotLinkPool::receivedPenalty, this, [this, appendDomain](const rm::PenaltyInfo &value) {
        match.applyPenalty(value);
        addEvent(QString("判罚：%1").arg(value.has_penalty_type() ? status::penaltyType(value.penalty_type()) : "未提供"));
        appendDomain(status::json(value));
    });
    connect(&linkPool, &RobotLinkPool::receivedRadar, &match, &MatchState::applyRadar);
    connect(&video, &VideoReceiver::problem, this, &MainWindow::addEvent);
    connect(operatorPage->focusButton(), &QPushButton::toggled, this, &MainWindow::setFocusMode);
    connect(new QShortcut(QKeySequence("F10"), this), &QShortcut::activated, this, [this] { setFocusMode(!focusMode); });
    connect(operatorPage->fullScreenButton(), &QPushButton::clicked, this, &MainWindow::toggleFullScreen);
    connect(new QShortcut(QKeySequence("F11"), this), &QShortcut::activated, this, &MainWindow::toggleFullScreen);
    connect(viewButton, &QPushButton::clicked, this, &MainWindow::switchView);
    connect(new QShortcut(QKeySequence("Ctrl+Tab"), this), &QShortcut::activated, this, &MainWindow::switchView);
    connect(new QShortcut(QKeySequence("Esc"), this), &QShortcut::activated, this, &MainWindow::leaveFullscreenOrFocus);
    connect(new QShortcut(QKeySequence("M"), this), &QShortcut::activated, this, &MainWindow::toggleMap);
    updateProfile();
    clock.start(); ticker.setInterval(200); connect(&ticker, &QTimer::timeout, this, &MainWindow::refresh); ticker.start(); refresh();
}

bool MainWindow::selectRobot(int id) {
    if (!profile::valid(id)) return false;
    const QSignalBlocker blockTeam(team), blockRole(robotRole);
    team->setCurrentIndex(id > 100 ? 1 : 0);
    robotRole->setCurrentIndex(robotRole->findData(id % 100));
    updateProfile();
    return true;
}

bool MainWindow::hasGameData() const { return match.ageMs(MatchState::Domain::Game) >= 0; }
bool MainWindow::dataStale() const {
    const qint64 age = match.ageMs(MatchState::Domain::Game);
    return !mqttReady || age < 0 || age > kStaleMs;
}
bool MainWindow::videoStale() const { return lastFrame < 0 || clock.elapsed() - lastFrame > kStaleMs; }

void MainWindow::updateProfile() {
    const int id = profile::robotId(team->currentIndex() == 1, robotRole->currentData().toInt());
    robotId->setText(QString::number(id));
    const bool pending = active && id != connectedRobotId;
    profileHint->setText(QString("机器人 ID %1 · %2").arg(id).arg(pending ? "点击下方按钮应用切换" : "需与官方选手端登录编号一致"));
    connectButton->setText(pending ? "应用兵种并重连" : active ? "重新连接当前机器人" : "连接所选机器人");
    const bool previous = !active && (hasGameData() || operatorPage->stage()->hasFrame());
    const int displayedId = active || previous ? connectedRobotId : id;
    operatorIdentity->setText(QString("%1 / ID %2 · %3").arg(profile::name(displayedId)).arg(displayedId)
        .arg(active ? "当前连接" : previous ? "已断开，保留上次画面" : "待连接"));
    // 编辑下一次连接的操作位时，不改写旧画面的来源身份。
    operatorPage->setRobot(displayedId);
    operatorPage->refresh();
}

void MainWindow::setFocusMode(bool enabled) {
    focusMode = enabled;
    // 专注态本身包含全屏，进入专注即退出纯全屏，两者互斥。
    if (enabled) pureFullScreen = false;
    const QSignalBlocker block(operatorPage->focusButton());
    operatorPage->focusButton()->setChecked(enabled);
    operatorPage->focusButton()->setText(enabled ? "退出专注  F10" : "专注  F10");
    sidebar->setVisible(!enabled); logPanel->setVisible(!enabled);
    diagnostics->setVisible(!enabled && !logToggle->isChecked());
    operatorPage->setFocused(enabled);
    if (enabled) { showFullScreen(); operatorPage->focusButton()->setFocus(); }
    else showNormal();
    // 专注模式保留全部信息叠加，只把卡底调淡；HUD 显隐仍由「信息叠加」与纯全屏共同决定。
    updateHudVisibility();
    refresh();
}

void MainWindow::toggleFullScreen() {
    // F11 切"纯全屏（无 HUD）"：与专注态互斥，从专注态按 F11 会转为纯全屏。
    if (focusMode) setFocusMode(false);
    if (isFullScreen()) { showNormal(); pureFullScreen = false; }
    else { showFullScreen(); pureFullScreen = true; }
    operatorPage->fullScreenButton()->setText(isFullScreen() ? "退出全屏  Esc" : "全屏  F11");
    updateHudVisibility();
    refresh();
}

void MainWindow::leaveFullscreenOrFocus() {
    // 专注态含全屏，必须先判专注：否则会出现"退了全屏但仍在专注态"的中间错误状态。
    if (focusMode) setFocusMode(false);
    else if (isFullScreen()) toggleFullScreen();
}

void MainWindow::toggleMap() {
    // 底部日志是 QPlainTextEdit，焦点在其中时按 M 会被当作文本输入。
    // QShortcut 默认 WindowShortcut 上下文仍会抢键，必须显式判断焦点控件。
    QWidget *focused = QApplication::focusWidget();
    if (qobject_cast<QPlainTextEdit *>(focused) || qobject_cast<QLineEdit *>(focused)
        || qobject_cast<QAbstractSpinBox *>(focused) || qobject_cast<QComboBox *>(focused)) return;
    if (pages->currentWidget() == operatorPage) operatorPage->toggleMap();
    else consolePage->toggleMap();
    refresh();
}

void MainWindow::updateHudVisibility() {
    operatorPage->setHudVisible(operatorPage->overlayToggle()->isChecked() && !pureFullScreen);
}

void MainWindow::recordMatchChanges(const rm::GameStatus &value) {
    const bool hadData = hasGameData();
    const bool continuous = hadData && mqttReady && lastData >= 0 && clock.elapsed()-lastData <= kStaleMs;
    if (!continuous) {
        addEvent(QString("%1：%2").arg(hadData ? "比赛快照恢复" : "首次收到比赛信息")
            .arg(value.has_current_stage() ? status::stage(value.current_stage()) : "阶段未提供"));
        return;
    }
    for (const auto &text : status::changes(match.game, value)) addEvent(text);
}

void MainWindow::refreshStatusDetails() {
    if (statusValues.size() != 10) return;
    const auto &v = match.game;
    const auto number = [](bool present, quint32 value) { return present ? QString::number(value) : QString("未提供"); };
    const auto seconds = [](bool present, qint32 value) { return present ? status::duration(value) : QString("未提供"); };
    statusValues[0]->setText(number(v.has_current_round(), v.current_round()));
    statusValues[1]->setText(number(v.has_total_rounds(), v.total_rounds()));
    statusValues[2]->setText(number(v.has_red_score(), v.red_score()));
    statusValues[3]->setText(number(v.has_blue_score(), v.blue_score()));
    statusValues[4]->setText(v.has_current_stage()
        ? QString("%1  [%2]").arg(status::stage(v.current_stage())).arg(v.current_stage()) : "未提供");
    statusValues[5]->setText(seconds(v.has_stage_countdown_sec(), v.stage_countdown_sec()));
    statusValues[6]->setText(seconds(v.has_stage_elapsed_sec(), v.stage_elapsed_sec()));
    statusValues[7]->setText(!v.has_is_paused() ? "未提供"
        : v.is_paused() ? "已暂停  [true]" : "未暂停  [false]");
    if (!v.has_game_result()) statusValues[8]->setText("未提供");
    else if (status::isSettlement(v)) statusValues[8]->setText(QString("%1  [%2]").arg(status::result(v.game_result())).arg(v.game_result()));
    else statusValues[8]->setText(QString("%1  [非结算]").arg(v.game_result()));
    if (!v.has_end_reason()) statusValues[9]->setText("未提供");
    else if (status::isSettlement(v)) statusValues[9]->setText(QString("%1  [%2]").arg(status::reason(v.end_reason())).arg(v.end_reason()));
    else statusValues[9]->setText(QString("%1  [非结算]").arg(v.end_reason()));

    int present = 0;
    present += v.has_current_round() ? 1 : 0; present += v.has_total_rounds() ? 1 : 0;
    present += v.has_red_score() ? 1 : 0; present += v.has_blue_score() ? 1 : 0;
    present += v.has_current_stage() ? 1 : 0; present += v.has_stage_countdown_sec() ? 1 : 0;
    present += v.has_stage_elapsed_sec() ? 1 : 0; present += v.has_is_paused() ? 1 : 0;
    present += v.has_game_result() ? 1 : 0; present += v.has_end_reason() ? 1 : 0;
    const auto issues = status::warnings(v);
    const bool hadData = hasGameData();
    const bool stale = dataStale();
    const QString state = !hadData ? "未接收" : stale ? "已过期" : issues.isEmpty() ? "实时" : "协议警告";
    badge(statusBadge, state, !hadData ? "neutral" : stale || !issues.isEmpty() ? "warning" : "good");
    statusMeta->setText(QString("协议 RM2026-V2.0.0 · 已提供 %1 / 10 字段%2")
        .arg(present).arg(stale && hadData ? " · 快照已过期" : ""));
    if (issues.isEmpty()) statusWarning->hide();
    else { statusWarning->setText("协议警告：" + issues.join("；")); statusWarning->show(); }
    statusPanel->setAccessibleDescription(statusMeta->text() + (issues.isEmpty() ? "" : "；" + statusWarning->text()));
}

void MainWindow::switchView() {
    const bool toConsole = pages->currentWidget() != consolePage;
    pages->setCurrentIndex(toConsole ? 0 : 1);
    // 按钮文案始终显示切换目标：当前是总控模式时提示可切到单兵模式。
    viewButton->setText(toConsole ? "单兵模式  Ctrl+Tab" : "总控模式  Ctrl+Tab");
    refresh();
}

bool MainWindow::validateForm() {
    formError->hide();
    for (auto *input : {host, robotId, bindIp, ffmpegPath}) {
        input->setProperty("invalid", false); input->style()->unpolish(input); input->style()->polish(input);
    }
    QLineEdit *invalid = nullptr; QString message;
    if (host->text().trimmed().isEmpty()) { invalid = host; message = "请填写 MQTT 服务端 IP 或主机名。"; }
    else if (robotId->text().trimmed().isEmpty()) { invalid = robotId; message = "请填写机器人 ID。"; }
    else if (QHostAddress(bindIp->text().trimmed()).isNull()) { invalid = bindIp; message = "请填写有效的本机监听 IP。"; }
    else if (ffmpegPath->text().trimmed().isEmpty()) { invalid = ffmpegPath; message = "请选择 FFmpeg 程序。"; }
    if (!invalid) return true;
    setFocusMode(false); advancedToggle->setChecked(true);
    invalid->setProperty("invalid", true); invalid->style()->unpolish(invalid); invalid->style()->polish(invalid);
    auto *fieldLayout = qobject_cast<QVBoxLayout *>(invalid->parentWidget()->layout());
    fieldLayout->insertWidget(fieldLayout->indexOf(invalid)+1, formError);
    invalid->setFocus(); formError->setText(message); formError->show();
    QTimer::singleShot(0, this, [this] { sidebar->ensureWidgetVisible(formError); });
    return false;
}

void MainWindow::startConnection() {
    // 参数错误不打断已有链路；先验证，再替换连接。
    if (!validateForm()) return;
    linkPool.stop(); video.stop(); messages = 0; lastData = lastFrame = -1; mqttReady = false;
    match.reset();
    fpsSampleAt = clock.elapsed(); fpsSampleFrames = 0; frameRate->setText("0 fps");
    operatorPage->stage()->setFrame(QImage(), true);
    operatorPage->strip()->setAccessibleDescription("等待比赛信息");
    simulation = mode->currentIndex() == 0; active = true; connectedRobotId = robotId->text().toInt();
    match.setPrimaryRobotId(connectedRobotId);
    operatorPage->setSimulation(simulation);
    consolePage->setRobot(connectedRobotId);
    updateProfile(); addEvent("连接操作位：" + operatorPage->operatorName());
    stopButton->setEnabled(true); lastUpdate->setText("最近接收 —");
    // 总控模式的全队单机域使用同一阵营的 1–7 号编号；把选中的操作位放在首位作为主链路。
    QVector<int> fleetIds{connectedRobotId};
    const int teamBase = connectedRobotId > 100 ? 100 : 0;
    for (int number = 1; number <= 7; ++number) {
        const int id = teamBase + number;
        if (!fleetIds.contains(id)) fleetIds.append(id);
    }
    const bool mqttStarted = linkPool.start(host->text().trimmed(), mqttPort->value(), fleetIds);
    const bool videoStarted = video.start(bindIp->text().trimmed(), quint16(udpPort->value()), ffmpegPath->text().trimmed());
    if (!mqttStarted || !videoStarted) {
        // 任一链路无法创建时立即回滚，避免界面显示"已连接"但后台仍残留半条链路。
        linkPool.stop(); video.stop(); active = false; mqttReady = false;
        stopButton->setEnabled(false); addEvent("连接未启动：请修正 MQTT 或图传参数");
    }
    refresh();
}

void MainWindow::appendLog(QPlainTextEdit *target, const QString &text) {
    const int position = target->verticalScrollBar()->value();
    target->appendPlainText(text);
    target->verticalScrollBar()->setValue(pauseLog->isChecked() ? position : target->verticalScrollBar()->maximum());
}
void MainWindow::addEvent(const QString &text) {
    if (text == lastEvent) return;
    lastEvent = text; appendLog(eventLog, QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
}

void MainWindow::refresh() {
    if (!clock.isValid()) return;
    const auto now = clock.elapsed();
    const bool stale = dataStale();
    const bool imageStale = videoStale();
    badge(sourceBadge, simulation ? "本地模拟" : "实机链路", simulation ? "warning" : "neutral");
    badge(dataState, !active ? "未连接" : stale ? (lastData < 0 ? "等待数据" : "数据过期") : "实时更新",
          !active ? "neutral" : stale ? "warning" : "good");
    const QString videoText = !active ? "未连接" : imageStale ? (lastFrame < 0 ? "等待画面" : "图传中断") : "正在播放";
    badge(videoState, videoText, !active ? "neutral" : imageStale ? "warning" : "good");
    messageCount->setText(QString::number(messages)); dropCount->setText(QString::number(video.dropped()));
    dataAge->setText(lastData < 0 ? "—" : now-lastData < 1000 ? QString("%1 ms").arg(now-lastData) : QString("%1 s").arg((now-lastData)/1000.0, 0, 'f', 1));
    if (now-fpsSampleAt >= 1000) {
        frameRate->setText(QString("%1 fps").arg((video.decoded-fpsSampleFrames)*1000.0/(now-fpsSampleAt), 0, 'f', 0));
        fpsSampleFrames = video.decoded; fpsSampleAt = now;
    }
    const QString base = video.sliceBase() < 0 ? "未判定" : QString::number(video.sliceBase());
    packetInfo->setText(QString("UDP %1 包 · 无效包 %2 · 分片基数 %3").arg(video.packets).arg(video.invalid()).arg(base));
    logToggle->setText(QString("接收日志 · %1 条比赛信息").arg(messages));
    // 图传的"过期"随计时变化，需要在每个 tick 同步刷新阶段层，否则断流后提示不会出现。
    operatorPage->stage()->setStale(imageStale);
    operatorPage->setLink({active, mqttReady, stale, imageStale, hasGameData()});
    consolePage->videoPreview()->setFrame(operatorPage->stage()->frame(), imageStale);
    refreshStatusDetails();
}

QJsonObject MainWindow::metrics() const {
    return {{"messages", double(messages)}, {"decoded_frames", double(video.decoded)}, {"udp_packets", double(video.packets)},
        {"robot_id", connectedRobotId}, {"selected_robot_id", robotId->text().toInt()}, {"operator", profile::name(connectedRobotId)},
        {"data_stale", dataStale()}, {"video_stale", videoStale()},
        {"mqtt_received", double(linkPool.receivedMessages())}, {"mqtt_malformed", double(linkPool.malformedMessages())},
        {"last_payload_bytes", linkPool.lastPayloadBytes()}, {"last_qos", linkPool.lastQos()},
        {"mqtt_link_count", linkPool.robotIds().size()}, {"mqtt_subscribed_topics", linkPool.subscribedTopicCount()},
        {"video_slice_base", video.sliceBase()}, {"video_zero_based_frames", double(video.zeroBasedFrames())},
        {"video_one_based_frames", double(video.oneBasedFrames())}, {"console_page", double(pages->currentIndex())},
        // 单兵模式证据：信息叠加的整体显隐、开关状态、队友面板行数、地图点位与专注全屏状态。
        // 可见性三项取"当前时刻"的实际状态（自检结束后会回到总控模式，故为 false），
        // 外部脚本据此只能验证开关口径，结构性问题由 --ui-checks 的返回值为准。
        {"operator_hud_enabled", operatorPage->overlayToggle()->isChecked()},
        {"operator_hud_visible", operatorPage->hudVisible()},
        {"operator_teammate_rows", operatorPage->teammates()->rowCount()},
        {"operator_map_markers", operatorPage->map()->markerCount()},
        {"operator_map_visible", operatorPage->mapVisible()},
        {"operator_focus_fullscreen", focusMode && isFullScreen()},
        // 总控模式证据：面板内容、数据域时效与累计接收计数，供 check_console.py 校验。
        {"console_timeline", double(match.timeline().size())}, {"console_markers", double(consolePage->map()->markerCount())},
        {"console_fleet_rows", consolePage->fleetPanel()->rowCount()}, {"console_fleet_height", consolePage->fleetPanel()->height()},
        {"console_link_rows", consolePage->linkPoolPanel()->rowCount()}, {"console_link_height", consolePage->linkPoolPanel()->height()},
        {"console_video_preview", consolePage->videoPreview()->hasFrame()},
        {"console_analysis", consolePage->analysis()->statusText()},
        {"console_respawn", consolePage->respawnState()->statusText()},
        {"console_position_messages", double(match.positionMessages)}, {"console_radar_messages", double(match.radarMessages)},
        {"console_event_messages", double(match.eventMessages)}, {"console_penalty_messages", double(match.penaltyMessages)},
        {"console_position_age_ms", double(match.ageMs(MatchState::Domain::Position))},
        {"console_radar_age_ms", double(match.ageMs(MatchState::Domain::Radar))},
        {"status", status::json(match.game)}};
}
bool MainWindow::saveEvidence(const QString &path) { return grab().save(path); }
bool MainWindow::runUiChecks(const QString &evidencePrefix) {
    const auto originalSize = size();
    const int originalSelection = robotId->text().toInt();
    const int originalConnection = connectedRobotId;
    const QString originalIdentity = operatorPage->operatorName();
    auto settle = [] { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); };
    auto checkpoint = [](bool passed, const char *name) {
        if (!passed) QTextStream(stderr) << "UI 检查失败：" << name << Qt::endl;
    };
    // M 键必须真的绑在快捷键上，否则"能切地图"只是调用了一个没人触发的函数。
    bool mapShortcutBound = false;
    for (auto *shortcut : findChildren<QShortcut *>())
        if (shortcut->key() == QKeySequence("M")) mapShortcutBound = true;
    bool okay = mapShortcutBound;
    checkpoint(okay, "M 键已绑定");

    // 兵种选择与待应用身份
    for (const bool blue : {false, true}) for (const auto &role : profile::roles) {
        const int id = profile::robotId(blue, role.number);
        okay = selectRobot(id) && robotId->text().toInt() == id && okay;
        if (active) okay = connectedRobotId == originalConnection && operatorPage->operatorName() == originalIdentity && okay;
    }
    okay = !selectRobot(100) && !selectRobot(10) && okay;
    selectRobot(originalSelection);
    checkpoint(okay, "兵种选择与待应用身份");

    // GameStatus 十字段面板
    refreshStatusDetails();
    okay = statusPanel->isVisible() && statusValues.size() == 10 && okay;
    if (hasGameData()) {
        okay = statusValues[4]->text().contains("比赛") && statusMeta->text().contains("/ 10") && okay;
    }
    checkpoint(okay, "GameStatus 十字段面板");

    // 总控模式页面
    const int originalPage = pages->currentIndex();
    pages->setCurrentWidget(consolePage);
    settle();
    const bool consoleMapWasOn = consolePage->mapVisible();
    // 自检不应受上一次用户偏好影响；结束时恢复原始状态，避免测试改变实际 UI 偏好。
    consolePage->setMapVisible(true); settle();
    okay = pages->currentWidget() == consolePage && consolePage->scoreBar()->isVisible()
        && consolePage->allyList()->isVisible() && consolePage->enemyList()->isVisible()
        && consolePage->map()->isVisible() && consolePage->respawnState()->isVisible()
        && consolePage->events()->isVisible() && consolePage->analysis()->isVisible()
        && consolePage->videoPreview()->isVisible()
        && consolePage->allyList()->rowCount() == 7 && consolePage->enemyList()->rowCount() == 5
        && consolePage->linkPoolPanel()->rowCount() == 7 && okay;
    // 有数据时才要求面板出内容，便于在无模拟端时也能跑通自检。
    if (match.ageMs(MatchState::Domain::Radar) >= 0)
        okay = consolePage->map()->markerCount() > 0 && okay;
    if (!match.timeline().isEmpty())
        okay = consolePage->events()->rowCount() > 0 && okay;
    if (match.ageMs(MatchState::Domain::Logistics) >= 0)
        okay = consolePage->analysis()->statusText() != "等待数据" && okay;
    if (match.ageMs(MatchState::Domain::Respawn) >= 0)
        okay = consolePage->respawnState()->statusText() != "未收到复活数据" && okay;
    if (operatorPage->stage()->hasFrame()) okay = consolePage->videoPreview()->hasFrame() && okay;
    // 总控模式中央地图同样由 M 键开关，此时必须切的是总控模式自己的地图。
    consolePage->setMapVisible(false); settle();
    okay = !consolePage->map()->isVisible() && okay;
    consolePage->setMapVisible(true); settle();
    okay = consolePage->map()->isVisible() && okay;
    if (!evidencePrefix.isEmpty()) {
        okay = saveEvidence(evidencePrefix + "-console.png") && okay;
        okay = consolePage->map()->grab().save(evidencePrefix + "-console-minimap.png") && okay;
        resize(1024, 720); settle();
        okay = consolePage->scoreBar()->isVisible() && consolePage->map()->isVisible()
            && consolePage->map()->width() > 100 && consolePage->allyList()->width() > 100 && okay;
        okay = saveEvidence(evidencePrefix + "-console-compact.png") && okay;
        resize(originalSize); settle();
    }
    checkpoint(okay, "总控模式页面");

    // 单兵模式页面：顶部信息条 / 队友面板 / 地图 / 信息叠加 / 专注即全屏
    pages->setCurrentWidget(operatorPage);
    settle();
    const bool hudWasOn = operatorPage->overlayToggle()->isChecked();
    // 复选框必须真的控制整个 HUD：关掉只剩纯图传。
    operatorPage->overlayToggle()->setChecked(false); settle();
    const bool overlayOff = !operatorPage->hudVisible() && !operatorPage->strip()->isVisible();
    operatorPage->overlayToggle()->setChecked(true); settle();
    const bool overlayOn = operatorPage->hudVisible() && operatorPage->strip()->isVisible();
    // 顶部条只呈现身份、阶段、倒计时与局数，任何口径下都不含比分。
    const QString stripText = operatorPage->strip()->summaryText();
    const bool stripOk = operatorPage->strip()->isVisible() && stripText.contains("阶段")
        && !stripText.contains("比分") && !stripText.contains("得分");
    // 队友面板固定 5 个协议槽位（1/2/3/4/7）。
    const bool matesOk = operatorPage->teammates()->isVisible() && operatorPage->teammates()->rowCount() == 5;
    // 地图：HUD 内常驻可见，M 键隐藏后能恢复。
    const bool mapWasOn = operatorPage->mapVisible();
    operatorPage->setMapVisible(true); settle();
    const bool mapOnOk = operatorPage->map()->isVisible();
    operatorPage->toggleMap(); settle();
    const bool mapOffOk = !operatorPage->map()->isVisible();
    operatorPage->toggleMap(); settle();
    const bool mapBackOk = operatorPage->map()->isVisible() && operatorPage->mapVisible();
    // 图传必须是精确 16:9：既不出黑边，HUD 也不会锚到画面之外。
    const int stageWidth = operatorPage->stage()->width(), stageHeight = operatorPage->stage()->height();
    const bool ratioOk = stageWidth > 0 && stageHeight > 0
        && qAbs(double(stageWidth) / stageHeight - 16.0 / 9.0) < 0.02
        && operatorPage->stage()->imageRect() == operatorPage->stage()->rect();
    // HUD 的每一块都必须落在画面矩形内，否则会压到画面之外。
    const QRect frame = operatorPage->stage()->imageRect();
    const bool anchoredOk = frame.contains(operatorPage->strip()->geometry())
        && frame.contains(operatorPage->teammates()->geometry())
        && frame.contains(operatorPage->hud()->ownCard()->geometry())
        && frame.contains(operatorPage->map()->geometry());
    bool group = overlayOff && overlayOn && stripOk && matesOk && mapOnOk && mapOffOk && mapBackOk
        && ratioOk && anchoredOk;
    if (!group) QTextStream(stderr) << "单兵模式断言：叠加关=" << overlayOff << " 叠加开=" << overlayOn
        << " 顶部条=" << stripOk << " 队友=" << matesOk << " 地图开=" << mapOnOk << " 地图关=" << mapOffOk
        << " 地图恢复=" << mapBackOk << " 16:9=" << ratioOk << "(" << stageWidth << "x" << stageHeight
        << ") 锚定=" << anchoredOk << Qt::endl;
    okay = group && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-operator.png") && okay;
    checkpoint(okay, "单兵模式页面");

    // 专注模式 = 全屏 + 隐藏侧栏/日志/诊断 + HUD 全部保留
    const int normalStageWidth = stageWidth;
    setFocusMode(true); settle();
    // 图传变大后 HUD 必须整体重排：四个信息件都要仍旧落在画面矩形内，不能只判"可见"。
    const QRect focusFrame = operatorPage->stage()->imageRect();
    const bool focusAnchored = focusFrame.contains(operatorPage->strip()->geometry())
        && focusFrame.contains(operatorPage->teammates()->geometry())
        && focusFrame.contains(operatorPage->hud()->ownCard()->geometry())
        && focusFrame.contains(operatorPage->map()->geometry());
    if (!focusAnchored) QTextStream(stderr) << "专注态锚定：画面=" << focusFrame.width() << "x" << focusFrame.height()
        << " 顶部条=" << operatorPage->strip()->geometry().bottom()
        << " 队友卡=" << operatorPage->teammates()->geometry().bottom()
        << " 本机卡=" << operatorPage->hud()->ownCard()->geometry().bottom()
        << " 地图=" << operatorPage->map()->geometry().bottom() << Qt::endl;
    okay = sidebar->isHidden() && logPanel->isHidden() && diagnostics->isHidden() && isFullScreen()
        && operatorPage->hudVisible() && operatorPage->strip()->isVisible() && operatorPage->map()->isVisible()
        && operatorPage->stage()->width() > normalStageWidth && focusAnchored && okay;
    // 另存一张只有 HUD 的原尺寸证据：整窗截图缩放后看不出 HUD 内部是否被裁。
    if (!evidencePrefix.isEmpty()) {
        okay = operatorPage->hud()->grab().save(evidencePrefix + "-hud.png") && okay;
        okay = saveEvidence(evidencePrefix + "-focus.png") && okay;
    }
    // Esc 一次退到底：专注态本身含全屏，退专注后应直接回到窗口态。
    leaveFullscreenOrFocus(); settle();
    okay = !isFullScreen() && !focusMode && !sidebar->isHidden() && !logPanel->isHidden()
        && operatorPage->hudVisible() && okay;
    checkpoint(okay, "专注模式与 Esc");
    // 纯全屏与专注态互斥，且纯全屏下不显示 HUD。
    toggleFullScreen(); settle();
    okay = isFullScreen() && !focusMode && !operatorPage->hudVisible() && okay;
    leaveFullscreenOrFocus(); settle();
    okay = !isFullScreen() && operatorPage->hudVisible() && okay;
    operatorPage->overlayToggle()->setChecked(hudWasOn); settle();
    operatorPage->setMapVisible(mapWasOn); settle();
    consolePage->setMapVisible(consoleMapWasOn); settle();
    checkpoint(okay, "纯全屏与 HUD 显隐");

    // 日志与表单
    advancedToggle->setChecked(true); okay = okay && !advanced->isHidden();
    logToggle->setChecked(true); okay = okay && !logBody->isHidden();
    pauseLog->setChecked(true); okay = okay && pauseLog->isChecked(); pauseLog->setChecked(false);
    const QString validHost = host->text(); host->clear(); okay = !validateForm() && !formError->isHidden() && okay;
    settle();
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-validation.png") && okay;
    host->setText(validHost); okay = validateForm() && okay;
    checkpoint(okay, "日志与表单");

    // 紧凑与最小窗口布局：图传不能被下方工具行压住，侧栏不能顶出窗口。
    // 先在推荐运行态（日志折叠）下量图传尺寸，再在最坏态（日志展开）下量不重叠性。
    logToggle->setChecked(false); settle();
    resize(1024, 720); settle();
    // 统一换算到页面坐标比较：图传与工具行各在stageRow / 页面下，彼此不是祖先关系。
    const auto stageBottom = [this] {
        return operatorPage->stage()->mapTo(operatorPage, QPoint(0, operatorPage->stage()->height() - 1)).y();
    };
    const auto toolTop = [this] { return operatorPage->overlayToggle()->mapTo(operatorPage, QPoint()).y(); };
    const bool compactStage = operatorPage->stage()->width() >= 480;
    const bool compactSidebar = sidebar->geometry().right() < centralWidget()->width();
    const bool compactStack = stageBottom() < toolTop();
    const bool compact = compactStage && compactSidebar && compactStack;
    if (!compact) QTextStream(stderr) << "紧凑断言：图传宽=" << operatorPage->stage()->width()
        << "(" << compactStage << ") 侧栏右=" << sidebar->geometry().right()
        << " 窗口宽=" << centralWidget()->width() << "(" << compactSidebar << ") 图传底=" << stageBottom()
        << " 工具行顶=" << toolTop() << "(" << compactStack << ")"
        << " 图传几何=" << QString("(%1,%2 %3x%4)").arg(operatorPage->stage()->x()).arg(operatorPage->stage()->y())
            .arg(operatorPage->stage()->width()).arg(operatorPage->stage()->height())
        << " 提示几何=" << QString("(%1,%2 %3x%4)").arg(operatorPage->notice()->x()).arg(operatorPage->notice()->y())
            .arg(operatorPage->notice()->width()).arg(operatorPage->notice()->height())
        << " 工具控件=" << QString("(%1,%2 %3x%4)").arg(operatorPage->overlayToggle()->x()).arg(operatorPage->overlayToggle()->y())
            .arg(operatorPage->overlayToggle()->width()).arg(operatorPage->overlayToggle()->height()) << Qt::endl;
    okay = compact && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-compact.png") && okay;
    checkpoint(okay, "紧凑窗口布局");
    resize(minimumSize()); settle();
    const bool minimumStack = stageBottom() < toolTop();
    const bool minimumStrip = operatorPage->strip()->isVisible();
    if (!(minimumStack && minimumStrip)) QTextStream(stderr) << "最小窗口断言：图传底=" << stageBottom()
        << " 工具行顶=" << toolTop() << "(" << minimumStack << ") 顶部条可见=" << minimumStrip << Qt::endl;
    okay = minimumStack && minimumStrip && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-minimum.png") && okay;
    // 最坏态：日志展开会吃掉大量高度，图传被压到最小，此时布局仍不许重叠、HUD 仍须留在画面内。
    logToggle->setChecked(true); settle();
    const QRect squeezed = operatorPage->stage()->imageRect();
    const bool squeezedOk = stageBottom() < toolTop() && !squeezed.isEmpty()
        && squeezed.contains(operatorPage->strip()->geometry());
    if (!squeezedOk) QTextStream(stderr) << "日志展开断言：图传底=" << stageBottom()
        << " 工具行顶=" << toolTop() << " 图传=" << squeezed.width() << "x" << squeezed.height() << Qt::endl;
    okay = squeezedOk && okay;
    logToggle->setChecked(false); settle();
    checkpoint(okay, "最小窗口与日志展开布局");

    advancedToggle->setChecked(false); logToggle->setChecked(false);
    pages->setCurrentIndex(originalPage);
    resize(originalSize); settle();
    return okay;
}
