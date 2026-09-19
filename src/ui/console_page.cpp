#include "console_page.h"
#include "console_panel.h"
#include <QGridLayout>
#include <QSettings>
#include <QVBoxLayout>

namespace {
QWidget *hint(const QString &text) {
    auto *value = new QLabel(text);
    value->setProperty("role", "muted");
    value->setAlignment(Qt::AlignCenter);
    value->setWordWrap(true);
    return value;
}
}

ConsolePage::ConsolePage(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setObjectName("consolePage");
    setAccessibleName("总控模式：比分条、我方与敌方列表、中央战术地图与底部事件、分析、图传预览");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    bar = new ScoreBar(match);
    layout->addWidget(bar);

    ally = new RobotListPanel(match, false);
    enemy = new RobotListPanel(match, true);
    allySummary = new QLabel;
    allySummary->setProperty("role", "muted");
    enemySummary = new QLabel;
    enemySummary->setProperty("role", "muted");

    QSettings settings;
    const bool mapVisible = settings.value("ui/console_map_visible", true).toBool();

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    layout->addLayout(grid, 1);

    auto *allyPanel = new ConsolePanel("我方机器人", ally, allySummary);
    allyPanel->setMinimumWidth(200);
    auto *enemyPanel = new ConsolePanel("敌方机器人", enemy, enemySummary);
    enemyPanel->setMinimumWidth(200);
    minimap = new MinimapPanel(match);
    auto *source = new QLabel("点位：雷达 / 本机测速模块");
    source->setProperty("role", "muted");
    mapPanel = new ConsolePanel("战术地图（M 键开关）", minimap, source);
    mapPanel->setMinimumWidth(230);
    respawnPanel = new RespawnPanel(match);
    auto *respawn = new ConsolePanel("复活状态", respawnPanel);
    auto *center = new QVBoxLayout;
    center->setSpacing(12);
    center->addWidget(mapPanel, 1);
    center->addWidget(respawn);
    grid->addLayout(center, 0, 1);
    grid->addWidget(allyPanel, 0, 0);
    grid->addWidget(enemyPanel, 0, 2);

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
    auto *video = new ConsolePanel("图传预览", videoPanel);
    for (auto *panel : {events, analysis, video}) panel->setMinimumHeight(104);
    grid->addWidget(events, 1, 0);
    grid->addWidget(analysis, 1, 1);
    grid->addWidget(video, 1, 2);
    mapPanel->setVisible(mapVisible);

    grid->setColumnStretch(0, 5);
    grid->setColumnStretch(1, 6);
    grid->setColumnStretch(2, 5);
    grid->setRowStretch(0, 5);
    grid->setRowStretch(1, 2);

    const auto refreshNow = [this] { refresh(); };
    connect(match, &MatchState::gameChanged, this, refreshNow);
    connect(match, &MatchState::unitStatusChanged, this, refreshNow);
    connect(match, &MatchState::logisticsChanged, this, refreshNow);
    connect(match, &MatchState::respawnChanged, this, refreshNow);
    connect(match, &MatchState::robotStaticChanged, this, refreshNow);
    connect(match, &MatchState::robotDynamicChanged, this, refreshNow);
    connect(match, &MatchState::robotModuleChanged, this, refreshNow);
    connect(match, &MatchState::positionChanged, this, refreshNow);
    connect(match, &MatchState::radarChanged, this, refreshNow);
    connect(match, &MatchState::timelineChanged, this, refreshNow);
    connect(match, &MatchState::stateReset, this, refreshNow);
    ticker.setInterval(200);
    connect(&ticker, &QTimer::timeout, this, refreshNow);
    ticker.start();
    refresh();
}

void ConsolePage::setMapVisible(bool visible) {
    mapPanel->setVisible(visible);
    if (visible) mapPanel->raise();
    QSettings settings;
    settings.setValue("ui/console_map_visible", visible);
}
void ConsolePage::toggleMap() { setMapVisible(!mapPanel->isVisible()); }
bool ConsolePage::mapVisible() const { return mapPanel->isVisible(); }

void ConsolePage::setRobot(int id) {
    bar->setAllyBlue(id > 100);
    analysisPanel->setAllyBlue(id > 100);
    ally->setOwnRobot(id);
    enemy->setOwnRobot(id);
    minimap->setOwnRobot(id);
    refresh();
}

void ConsolePage::refresh() {
    allySummary->setText(ally->summaryText());
    enemySummary->setText(enemy->summaryText());
    analysisSummary->setText(analysisPanel->statusText());
    bar->update();
    ally->update();
    enemy->update();
    minimap->update();
    respawnPanel->update();
    timeline->update();
    analysisPanel->update();
    videoPanel->update();
}
