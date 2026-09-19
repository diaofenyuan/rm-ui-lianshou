#include "console_page.h"
#include "console_panel.h"
#include "operator_profile.h"
#include <QGridLayout>
#include <QEvent>
#include <QSignalBlocker>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString robotLabel(int id) {
    const int number = id % 100;
    if (number < 1 || number > int(profile::roles.size())) return "未选择";
    return QString("%1方 · %2号 %3").arg(id > 100 ? "蓝" : "红").arg(number)
        .arg(QString::fromUtf8(profile::roles[number - 1].name));
}
}

ConsolePage::ConsolePage(MatchState *state, RobotLinkPool *pool, QWidget *parent)
    : QWidget(parent), match(state), pool(pool) {
    setObjectName("consolePage");
    setAccessibleName("总控模式：顶部指挥条、我方与敌方列表、中央战术地图和完整图传");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    commandBar = new QFrame;
    commandBar->setProperty("role", "panel");
    commandBar->setFixedWidth(248);
    auto *commandLayout = new QVBoxLayout(commandBar);
    commandLayout->setContentsMargins(10, 5, 10, 5);
    commandLayout->setSpacing(2);
    focusCombo = new QComboBox;
    focusCombo->setAccessibleName("当前观察机器人");
    focusCombo->setMinimumWidth(150);
    focusCombo->setToolTip("当前观察 · Ctrl+Tab 轮换；仅改变观察焦点");
    focusCombo->setStyleSheet("QComboBox { padding: 3px 22px 3px 6px; min-height: 20px; }");
    commandLayout->addWidget(focusCombo);
    focusMeta = new QLabel("当前观察 · Ctrl+Tab 轮换");
    focusMeta->setProperty("role", "muted");
    commandLayout->addWidget(focusMeta);
    reconnectButton = new QToolButton;
    reconnectButton->setText("应用并重连");
    reconnectButton->setStyleSheet("QToolButton { padding: 1px 4px; }");
    reconnectButton->setToolTip("将观察对象应用为主连接身份，并重新连接");
    commandLayout->addWidget(reconnectButton);
    connect(reconnectButton, &QToolButton::clicked, this, [this] { emit reconnectRequested(focusId); });
    bar = new ScoreBar(match);
    auto *commandRow = new QHBoxLayout;
    commandRow->setSpacing(8);
    commandRow->addWidget(bar, 1);
    commandRow->addWidget(commandBar);
    layout->addLayout(commandRow);

    fleet = new FleetPanel(match);
    enemy = new RobotListPanel(match, true);
    linkPanel = new LinkPoolPanel(pool);
    allySummary = new QLabel;
    allySummary->setProperty("role", "muted");
    enemySummary = new QLabel;
    enemySummary->setProperty("role", "muted");

    workArea = new QWidget;
    workArea->setMinimumSize(0, 0);
    workArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    workArea->installEventFilter(this);
    layout->addWidget(workArea, 1);

    allyPanel = new ConsolePanel("我方机器人", fleet, nullptr, workArea);
    qobject_cast<QVBoxLayout *>(allyPanel->layout())->addWidget(allySummary);
    allyPanel->setMinimumWidth(0);
    rightColumn = new QWidget(workArea);
    enemyPanel = new ConsolePanel("敌方机器人", enemy, enemySummary, rightColumn);
    enemyPanel->setMinimumWidth(0);
    minimap = new MinimapPanel(match);
    auto *source = new QToolButton;
    source->setText("地图聚焦 · M");
    connect(source, &QToolButton::clicked, this, &ConsolePage::toggleMap);
    source->setProperty("role", "muted");
    mapPanel = new ConsolePanel("战术地图", minimap, source, workArea);
    mapPanel->setMinimumWidth(0);
    respawnPanel = new RespawnPanel(match);

    timeline = new EventTimelinePanel(match);
    auto *eventSummary = new QLabel;
    eventSummary->setProperty("role", "muted");
    auto *events = new ConsolePanel("战场事件", timeline, eventSummary);
    connect(match, &MatchState::timelineChanged, eventSummary, [eventSummary, this] {
        eventSummary->setText(QString("共 %1 条").arg(match->timeline().size()));
    });
    analysisPanel = new AnalysisPanel(match);
    analysisSummary = new QLabel;
    analysisSummary->setProperty("role", "muted");
    auto *analysis = new ConsolePanel("数据分析", analysisPanel, analysisSummary);

    videoPanel = new VideoPreviewPanel;
    videoMeta = new QLabel;
    videoMeta->setProperty("role", "muted");
    videoContainer = new ConsolePanel("主图传", videoPanel, nullptr, rightColumn);
    videoMeta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    videoMeta->setMinimumWidth(0);
    qobject_cast<QVBoxLayout *>(videoContainer->layout())->addWidget(videoMeta);

    linkSummary = new QLabel;
    linkSummary->setProperty("role", "muted");
    auto *links = new ConsolePanel("连接与诊断", linkPanel, linkSummary);

    supportBar = new QFrame;
    supportBar->setProperty("role", "panel");
    auto *supportLayout = new QHBoxLayout(supportBar);
    supportLayout->setContentsMargins(12, 6, 12, 6);
    supportLayout->setSpacing(8);
    allyToggle = new QToolButton;
    allyToggle->setText("我方");
    allyToggle->setCheckable(true);
    enemyToggle = new QToolButton;
    enemyToggle->setText("敌方");
    enemyToggle->setCheckable(true);
    supportLayout->addWidget(allyToggle);
    supportLayout->addWidget(enemyToggle);
    for (auto *button : {allyToggle, enemyToggle})
        connect(button, &QToolButton::toggled, this, [this] { layoutWorkArea(); });
    supportSummary = new QLabel("等待战场事件");
    supportSummary->setProperty("role", "muted");
    supportSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    supportLayout->addWidget(supportSummary, 1);
    healthSummary = new QLabel;
    healthSummary->setProperty("role", "muted");
    supportLayout->addWidget(healthSummary);
    // 抽屉覆盖工作区底部，不挤压图传或把地图推出首屏。
    drawerStack = new QStackedWidget(workArea);
    drawerStack->addWidget(events);
    drawerStack->addWidget(analysis);
    drawerStack->addWidget(new ConsolePanel("复活状态", respawnPanel));
    drawerStack->addWidget(links);
    drawerStack->setMinimumSize(0, 0);
    drawer = drawerStack;
    drawer->setObjectName("consoleSupportDrawer");
    drawer->hide();
    const QStringList supportLabels{"事件", "分析", "复活", "连接与诊断"};
    for (int i = 0; i < supportLabels.size(); ++i) {
        auto *button = new QToolButton;
        button->setText(supportLabels.at(i));
        button->setCheckable(true);
        button->setAccessibleName(QString("展开%1").arg(supportLabels.at(i)));
        supportButtons.append(button);
        supportLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, i] {
            if (drawer->isVisible() && drawerStack->currentIndex() == i) closeSupport();
            else openSupport(i);
        });
    }
    layout->addWidget(supportBar, 0);

    connect(focusCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) selectFocus(focusCombo->itemData(index).toInt());
    });

    const auto refreshNow = [this] { refresh(); };
    connect(match, &MatchState::gameChanged, this, refreshNow);
    connect(match, &MatchState::unitStatusChanged, this, refreshNow);
    connect(match, &MatchState::logisticsChanged, this, refreshNow);
    connect(match, &MatchState::respawnChanged, this, refreshNow);
    connect(match, &MatchState::robotStaticChanged, this, refreshNow);
    connect(match, &MatchState::robotDynamicChanged, this, refreshNow);
    connect(match, &MatchState::robotModuleChanged, this, refreshNow);
    connect(match, &MatchState::robotsChanged, this, refreshNow);
    connect(match, &MatchState::positionChanged, this, refreshNow);
    connect(match, &MatchState::radarChanged, this, refreshNow);
    connect(match, &MatchState::timelineChanged, this, refreshNow);
    connect(match, &MatchState::stateReset, this, refreshNow);
    if (pool) connect(pool, &RobotLinkPool::linkChanged, this, refreshNow);
    ticker.setInterval(200);
    connect(&ticker, &QTimer::timeout, this, refreshNow);
    ticker.start();

    setMapFocus(false);
    refresh();
}

void ConsolePage::setMapVisible(bool visible) {
    // 保留该接口供自检与外部调用；M 键本身使用 setMapFocus，不再隐藏核心地图。
    mapPanel->setVisible(visible);
}

void ConsolePage::toggleMap() { setMapFocus(!mapFocus); }

bool ConsolePage::mapVisible() const { return mapPanel->isVisible(); }

void ConsolePage::setMapFocus(bool enabled) {
    mapFocus = enabled;
    mapPanel->setVisible(true);
    if (enabled) closeSupport();
    layoutWorkArea();
}

void ConsolePage::openSupport(int index) {
    if (index < 0 || index >= drawerStack->count()) return;
    drawerStack->setCurrentIndex(index);
    drawer->show();
    drawer->raise();
    for (int i = 0; i < supportButtons.size(); ++i) supportButtons[i]->setChecked(i == index);
    layoutWorkArea();
}

void ConsolePage::closeSupport() {
    drawer->hide();
    for (auto *button : supportButtons) button->setChecked(false);
}

bool ConsolePage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == workArea && event->type() == QEvent::Resize) layoutWorkArea();
    return QWidget::eventFilter(watched, event);
}

void ConsolePage::layoutWorkArea() {
    if (!videoContainer || !drawer) return;
    const int w = workArea->width(), h = workArea->height(), gap = 8;
    const bool compact = width() < 1106;
    allyToggle->setVisible(compact && !mapFocus);
    enemyToggle->setVisible(compact && !mapFocus);
    const int rightWidth = qBound(308, int(w * .235), 480);
    const int leftWidth = compact ? 0 : qMin(320, int(w * .19));
    const int leftOffset = compact ? 0 : leftWidth + gap;
    mapPanel->setGeometry(mapFocus ? QRect(0, 0, w, h)
        : QRect(leftOffset, 0, qMax(0, w - leftOffset - rightWidth - gap), h));
    allyPanel->setVisible(!mapFocus && (!compact || allyToggle->isChecked()));
    allyPanel->setGeometry(0, 0, compact ? 240 : leftWidth, h);
    if (compact) allyPanel->raise();
    rightColumn->setVisible(!mapFocus);
    rightColumn->setGeometry(w - rightWidth, 0, rightWidth, h);

    const auto *videoLayout = videoContainer->layout();
    const auto margins = videoLayout->contentsMargins();
    const int videoWidth = rightWidth - margins.left() - margins.right();
    const int chrome = margins.top() + margins.bottom() + videoLayout->spacing() * 2
        + videoLayout->itemAt(0)->sizeHint().height() + videoMeta->sizeHint().height();
    const int videoHeight = qCeil(videoWidth * 9.0 / 16.0) + chrome;
    videoContainer->setGeometry(0, qMax(0, h - videoHeight), rightWidth, videoHeight);
    enemyPanel->setVisible(!compact || enemyToggle->isChecked());
    enemyPanel->setGeometry(0, 0, rightWidth, qMax(0, h - videoHeight - gap));
    const int drawerHeight = qMin(220, h);
    drawer->setGeometry(0, h - drawerHeight, w, drawerHeight);
    if (!drawer->isHidden()) drawer->raise();
}

void ConsolePage::cycleFocus() {
    if (focusCombo && focusCombo->count() > 0)
        focusCombo->setCurrentIndex((focusCombo->currentIndex() + 1) % focusCombo->count());
}

void ConsolePage::setRobot(int id) {
    if (!profile::valid(id)) return;
    sourceId = id;
    bar->setAllyBlue(id > 100);
    analysisPanel->setAllyBlue(id > 100);
    fleet->setOwnRobot(id);
    enemy->setOwnRobot(id);
    minimap->setOwnRobot(id);

    const bool blue = id > 100;
    if (focusCombo->count() == 0 || focusCombo->itemData(0).toInt() / 100 != (blue ? 1 : 0)) {
        const QSignalBlocker blocker(focusCombo);
        focusCombo->clear();
        for (int number = 1; number <= 7; ++number) {
            const int robotId = (blue ? 100 : 0) + number;
            focusCombo->addItem(robotLabel(robotId), robotId);
        }
    }
    if (focusCombo->findData(id) < 0) {
        const QSignalBlocker blocker(focusCombo);
        focusCombo->addItem(robotLabel(id), id);
    }
    selectFocus(id);
}

void ConsolePage::selectFocus(int id) {
    const int index = focusCombo->findData(id);
    if (index < 0) return;
    focusId = id;
    const QSignalBlocker blocker(focusCombo);
    focusCombo->setCurrentIndex(index);
    fleet->setSelectedRobot(id);
    minimap->setSelectedRobot(id);
    refresh();
}

void ConsolePage::updateFocusUi() {
    if (!focusCombo || focusCombo->currentIndex() < 0) return;
    const int id = focusCombo->currentData().toInt();
    const bool pending = id != sourceId;
    focusMeta->setText(pending ? "观察已切换 · 连接身份待应用" : "当前观察 · Ctrl+Tab 轮换");
    reconnectButton->setVisible(pending);
    const QString state = !videoPanel->hasFrame() ? "等待图传"
        : videoPanel->isStale() ? "最后一帧" : "图传正常";
    // 当前仅有一条 UDP 接收链路；观察切换不能将旧帧重新归属为新机器人。
    videoMeta->setText(QString("%1 · %2").arg(robotLabel(sourceId), state));
    videoMeta->setToolTip(pending ? QString("当前画面仍来自连接操作位 %1；观察焦点为 %2，尚未切换图传源。")
        .arg(robotLabel(sourceId), robotLabel(id)) : videoMeta->text());
}

void ConsolePage::refresh() {
    allySummary->setText(fleet->summaryText());
    enemySummary->setText(enemy->summaryText());
    analysisSummary->setText(analysisPanel->statusText());
    linkSummary->setText(linkPanel->summaryText());
    allySummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    allySummary->setToolTip(allySummary->text());
    const qint64 age = match->ageMs(MatchState::Domain::Game);
    healthSummary->setText(QString("%1 · %2").arg(linkPanel->summaryText(),
        age < 0 ? "比赛等待数据" : match->isStale(MatchState::Domain::Game) ? "比赛数据过期" : "比赛在线"));
    updateFocusUi();
    if (!match->timeline().isEmpty()) {
        const auto &entry = match->timeline().constLast();
        supportSummary->setText(QString("%1 · %2").arg(entry.text, entry.stamp));
    } else {
        supportSummary->setText("等待战场事件");
    }
    supportSummary->setToolTip(supportSummary->text());
    bar->update();
    fleet->update();
    enemy->update();
    minimap->update();
    respawnPanel->update();
    timeline->update();
    analysisPanel->update();
    videoPanel->update();
}
