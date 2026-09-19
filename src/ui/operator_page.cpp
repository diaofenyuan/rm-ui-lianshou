#include "operator_page.h"
#include "operator_profile.h"
#include "status.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &text, const char *role = nullptr) {
    auto *value = new QLabel(text);
    if (role) value->setProperty("role", role);
    return value;
}
// 与成员 liveBadge 区分：这里是自由函数，不能与本类成员同名。
void setBadge(QLabel *target, const QString &text, const char *tone = "neutral") {
    target->setText(text);
    if (target->property("tone").toString() == tone) return;
    target->setProperty("tone", tone);
    target->style()->unpolish(target);
    target->style()->polish(target);
}
}

OperatorPage::OperatorPage(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setObjectName("operatorPage");
    setAccessibleName("单兵模式：全屏图传与信息叠加");
    body = new QVBoxLayout(this);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);

    headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    headerRow->addWidget(label("主视角", "section"));
    live = label("等待画面", "badge");
    headerRow->addWidget(live, 0, Qt::AlignVCenter);
    headerRow->addStretch();
    focus = new QPushButton("专注  F10");
    focus->setCheckable(true);
    focus->setToolTip("全屏并隐藏配置栏、接收统计与日志；信息叠加全部保留");
    headerRow->addWidget(focus);
    fullScreen = new QPushButton("全屏  F11");
    fullScreen->setToolTip("F11 切换纯全屏（无信息叠加），Esc 退出");
    headerRow->addWidget(fullScreen);
    body->addLayout(headerRow);

    videoStage = new VideoStage;
    hudWidget = new OperatorHud(match);
    videoStage->setOverlay(hudWidget);
    // 地图开关属于操作手偏好，跨次启动保留；默认开启以保证首次进入仍能看到态势。
    QSettings settings;
    hudWidget->setMapVisible(settings.value("ui/operator_map_visible", true).toBool());
    // 图传尺寸由 applyStageWidth() 按可用高度反推为精确 16:9，再用两侧弹簧横向居中。
    // 不给控件加 alignment：那样会让它在布局里失去伸展能力，反推出来的宽度会被丢弃。
    stageRow = new QWidget;
    auto *stageLayout = new QHBoxLayout(stageRow);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    stageLayout->addStretch(1);
    stageLayout->addWidget(videoStage);
    stageLayout->addStretch(1);
    body->addWidget(stageRow);

    noticeLabel = label("比赛信息：等待连接", "notice");
    noticeLabel->setWordWrap(true);
    body->addWidget(noticeLabel);

    toolRow = new QHBoxLayout;
    toolRow->setSpacing(10);
    overlay = new QCheckBox("信息叠加");
    overlay->setToolTip("开关整个信息叠加层；关闭后只剩纯图传画面");
    // 默认开启：HUD 的初始可见状态必须与复选框一致，否则首帧就对不上，
    // 而且 setChecked(false) 在状态未变化时不会发信号，"关掉 HUD"会失效。
    overlay->setChecked(true);
    toolRow->addWidget(overlay);
    sourcePlate = label("本地模拟 / 非真实比赛画面", "badge");
    toolRow->addWidget(sourcePlate);
    operatorPlate = label("未选择操作位", "badge");
    toolRow->addWidget(operatorPlate);
    toolRow->addStretch();
    videoInfo = label("HEVC 图传 · 等待输入", "muted");
    toolRow->addWidget(videoInfo);
    // 来源与操作位铭牌放在画面之外，不常驻遮挡图传内容。
    body->addLayout(toolRow);
    body->addStretch();

    connect(overlay, &QCheckBox::toggled, this, [this](bool value) { setHudVisible(value); });
    // M 键由 MainWindow 统一分发（需先判断焦点是否在文本控件内），这里只提供开关能力。
    applyStageWidth();
}

void OperatorPage::setRobot(int id) {
    if (currentRobotId == id) return;
    currentRobotId = id;
    ally = id > 100;
    hudWidget->setRobot(id);
    updateOperatorPlate();
    refresh();
}

// 注意：本函数会连带刷新 HUD，**不要**放进逐帧路径。
// 图传帧由 MainWindow 直接投给 VideoStage，HUD 只在 200ms ticker 与数据域信号上刷新，
// 这样信息叠加不跟图传帧率走（1080p60 下不会因为叠层重绘拖慢解码）。
void OperatorPage::setFrame(const QImage &frame, bool stale) {
    videoStage->setFrame(frame, stale);
    refresh();
}

void OperatorPage::setSimulation(bool value) {
    if (simulation == value) return;
    simulation = value;
    videoStage->setPlaceholder(value);
    refresh();
}

void OperatorPage::setLink(const LinkState &state) {
    link = state;
    refresh();
}

void OperatorPage::setDiagnostics(QWidget *widget) {
    if (!widget || diagnosticsPanel) return;
    diagnosticsPanel = widget;
    // 插在末尾的弹簧之前：接收统计始终贴在页面底部。
    body->insertWidget(body->count() - 1, widget);
    applyStageWidth();
}

void OperatorPage::setHudVisible(bool visible) {
    // 地图自身的显隐由 Qt 逐控件记忆：隐藏 HUD 不会重置 M 键状态，重新显隐后保持原样。
    hudWidget->setVisible(visible);
    if (visible) hudWidget->raise();
}
bool OperatorPage::hudVisible() const { return hudWidget->isVisible(); }

void OperatorPage::setMapVisible(bool visible) {
    hudWidget->setMapVisible(visible);
    QSettings settings;
    settings.setValue("ui/operator_map_visible", visible);
}
bool OperatorPage::mapVisible() const { return hudWidget->mapVisible(); }
void OperatorPage::toggleMap() { setMapVisible(!hudWidget->mapVisible()); }

void OperatorPage::setFocused(bool value) {
    focused = value;
    hudWidget->setFocused(value);
}

void OperatorPage::updateOperatorPlate() {
    operatorLabel = currentRobotId
        ? profile::name(currentRobotId) + QString(" / ID %1").arg(currentRobotId)
        : QString();
    setBadge(operatorPlate, operatorLabel.isEmpty() ? "未选择操作位" : operatorLabel, "neutral");
}

int OperatorPage::chromeHeight() const {
    // 页面内图传之外的全部固定行高度之和：header / 提示 / 工具行 / 接收统计 / 行间距与外边距。
    int total = 0;
    const auto margins = body->contentsMargins();
    total += margins.top() + margins.bottom();
    total += headerRow->sizeHint().height();
    total += noticeLabel->sizeHint().height();
    total += toolRow->sizeHint().height();
    if (diagnosticsPanel) total += diagnosticsPanel->sizeHint().height();
    // 页面里除弹簧外共 6 个条目，故有 5 段行间距。
    total += body->spacing() * 5;
    return total;
}

void OperatorPage::applyStageWidth() {
    if (!videoStage) return;
    // 图传是固定 16:9，高度只能由宽度决定，故反过来按"页面高度减去其它行"求宽度上限；
    // 该估算只与页面高度有关，与图传自身尺寸无关，不会形成来回抖动。
    // 留出比布局四舍五入更宽的余量：窄窗口下提示条可能因换行比 sizeHint 多一行，
    // 宁可让两侧弹簧吃掉一点宽度，也不让定尺寸图传与工具行发生垂直重叠。
    const int available = qMax(160, height() - chromeHeight() - 24);
    const int stageWidth = qMin(width(), qMax(320, qRound(available * 16.0 / 9.0)));
    videoStage->setFixedSize(stageWidth, qRound(stageWidth * 9.0 / 16.0));
    hudWidget->setScale(theme::hud::scaleFor(videoStage->height()));
}

void OperatorPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    applyStageWidth();
}

void OperatorPage::refresh() {
    if (!videoStage) return;
    const bool hasFrame = videoStage->hasFrame();
    const QString videoText = !link.active ? QString("未连接")
        : link.videoStale ? (hasFrame ? QString("图传中断") : QString("等待画面")) : QString("正在播放");
    const char *videoTone = !link.active ? "neutral" : link.videoStale ? "warning" : "good";
    setBadge(live, videoText, videoTone);
    setBadge(sourcePlate, simulation ? "本地模拟 / 非真实比赛画面" : "实机图传 / RM2026",
             simulation ? "warning" : "good");

    QString note;
    const char *tone = "neutral";
    if (!link.active) note = link.hasData ? "已断开连接 · 保留上次接收的比赛信息与最后一帧" : "选择操作位并连接，开始接收比赛信息与图传";
    else if (link.dataStale) {
        note = link.hasData ? "比赛信息已过期 · 倒计时保留最后接收值" : "等待比赛信息 · 请检查服务端和机器人编号";
        tone = "warning";
    } else if (link.videoStale) {
        note = hasFrame ? "图传已中断 · 当前画面为最后一帧" : "比赛信息正常 · 等待图传输入";
        tone = "warning";
    } else if (match->game.has_is_paused() && match->game.is_paused()) {
        note = "比赛已暂停 · 倒计时以裁判系统最新数据为准";
        tone = "warning";
    } else {
        note = "比赛信息实时更新 · 图传接收正常";
    }
    setBadge(noticeLabel, note, tone);
    noticeLabel->setAccessibleDescription(note);

    videoInfo->setText(hasFrame
        ? QString("%1 × %2 · HEVC").arg(videoStage->frame().width()).arg(videoStage->frame().height())
        : QString("HEVC 图传 · 等待输入"));

    hudWidget->setLinkState(link.mqttReady, link.mqttReady && !link.dataStale && link.active,
                            link.active && hasFrame && !link.videoStale);
    hudWidget->setScale(theme::hud::scaleFor(videoStage->height()));
    hudWidget->refresh();
}
