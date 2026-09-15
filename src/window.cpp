#include "window.h"
#include "theme.h"
#include "operator_profile.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextStream>

namespace {
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
void fitText(QPainter &p, const QRect &rect, const QString &text, int pixels, bool numeric = false) {
    auto font = theme::font(pixels, true, numeric);
    // 异常大数值仍保留原值；先缩小字号，再省略超长内容，避免越过相邻比分。
    while (pixels > 13 && QFontMetrics(font).horizontalAdvance(text) > rect.width()) {
        font.setPixelSize(--pixels);
    }
    p.setFont(font);
    p.drawText(rect, Qt::AlignCenter, QFontMetrics(font).elidedText(text, Qt::ElideRight, rect.width()));
}
}

MatchSummary::MatchSummary(const VideoCanvas *source) : source(source) {
    setFixedHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName("比赛概览：红蓝得分、阶段和倒计时");
}

void MatchSummary::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto &data = source->data;
    const int side = qMin(160, width()/4), middle = width()-2*side;
    p.setPen(Qt::NoPen); p.setBrush(QColor("#F4F7FA")); p.drawRoundedRect(rect(), 8, 8);
    p.setBrush(QColor("#FCEEF1")); p.drawRoundedRect(QRect(0, 0, side, 70), 8, 8);
    p.setBrush(QColor("#EDF4FD")); p.drawRoundedRect(QRect(width()-side, 0, side, 70), 8, 8);
    p.setPen(theme::red); p.setFont(theme::font(12, true));
    p.drawText(QRect(8, 8, side-16, 20), Qt::AlignCenter, "红方得分");
    fitText(p, QRect(8, 30, side-16, 38), data.has_red_score() ? QString::number(data.red_score()) : "—", 34, true);
    p.setPen(theme::blue); p.setFont(theme::font(12, true));
    p.drawText(QRect(width()-side+8, 8, side-16, 20), Qt::AlignCenter, "蓝方得分");
    fitText(p, QRect(width()-side+8, 30, side-16, 38), data.has_blue_score() ? QString::number(data.blue_score()) : "—", 34, true);
    QString state = source->hasData && data.has_current_stage() ? status::stage(data.current_stage()) : "等待比赛信息";
    if (source->hasData && source->stale) state += " · 已过期";
    else if (data.has_is_paused()) state += data.is_paused() ? " · 已暂停" : " · 未暂停";
    p.setPen(source->stale ? theme::warning : theme::muted); p.setFont(theme::font(12));
    p.drawText(QRect(side+8, 8, middle-16, 20), Qt::AlignCenter, p.fontMetrics().elidedText(state, Qt::ElideRight, middle-16));
    const bool urgent = !source->stale && data.has_current_stage() && data.current_stage() == 4
        && data.has_is_paused() && !data.is_paused() && data.has_stage_countdown_sec()
        && data.stage_countdown_sec() >= 0 && data.stage_countdown_sec() <= 10;
    p.setPen(urgent ? theme::red : theme::text);
    fitText(p, QRect(side+8, 29, middle-16, 42), data.has_stage_countdown_sec() ? status::duration(data.stage_countdown_sec()) : "--:--", 38, true);
    const QString round = QString("第 %1 / %2 局").arg(data.has_current_round() ? QString::number(data.current_round()) : "—")
        .arg(data.has_total_rounds() ? QString::number(data.total_rounds()) : "—");
    QString detail = round + "     阶段已过 " + (data.has_stage_elapsed_sec() ? status::duration(data.stage_elapsed_sec()) : "--:--");
    if (source->hasData && !data.has_is_paused()) detail += " · 暂停状态未提供";
    if (data.has_current_stage() && data.current_stage() == 5) {
        detail = round + " · " + (data.has_game_result() ? status::result(data.game_result()) : "胜者未提供")
            + " / " + (data.has_end_reason() ? status::reason(data.end_reason()) : "原因未提供");
    }
    p.setPen(theme::muted); p.setFont(theme::font(11));
    p.drawText(QRect(12, 74, width()-24, 22), Qt::AlignCenter, p.fontMetrics().elidedText(detail, Qt::ElideRight, width()-24));
}

VideoCanvas::VideoCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(440, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("比赛图传与比分");
}

void VideoCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 8, 8);
    p.setClipPath(clip);
    p.fillRect(rect(), QColor("#142330"));
    if (!image.isNull()) {
        const auto size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((width()-size.width())/2, (height()-size.height())/2), size), image);
    } else {
        p.setPen(QColor("#203440"));
        for (int x = 0; x < width(); x += 40) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 40) p.drawLine(0, y, width(), y);
        const QPoint center(width()/2, height()/2 + (height() < 240 ? -30 : 12));
        p.setPen(QPen(QColor("#94ACA9"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRect(center.x()-22, center.y()-34, 34, 27), 5, 5);
        p.drawPolyline(QPolygon({QPoint(center.x()+12, center.y()-26), QPoint(center.x()+24, center.y()-32),
                                QPoint(center.x()+24, center.y()-9), QPoint(center.x()+12, center.y()-15)}));
        p.setPen(QColor("#EDF4F7")); p.setFont(theme::font(19, true));
        p.drawText(QRect(16, center.y()+4, width()-32, 30), Qt::AlignCenter, "准备接收图传");
        p.setPen(QColor("#AEBDC8")); p.setFont(theme::font(12));
        p.drawText(QRect(16, center.y()+39, width()-32, 24), Qt::AlignCenter,
                   simulation ? "启动本地演示后，连接数据与图传" : "检查图传接线与本机监听 IP，然后连接");
    }

    p.setPen(Qt::NoPen); p.setBrush(QColor(16, 30, 41, 240));
    p.drawRoundedRect(QRect(14, height()-39, simulation ? 222 : 189, 26), 5, 5);
    p.setPen(simulation ? QColor("#F4D197") : QColor("#8EE2C6")); p.setFont(theme::font(11));
    p.drawText(QRect(24, height()-39, 220, 26), Qt::AlignVCenter,
               simulation ? "本地模拟 / 非真实比赛画面" : "实机图传 / RM2026");
    if (!operatorName.isEmpty()) {
        const int nameWidth = qMin(width()-270, p.fontMetrics().horizontalAdvance(operatorName)+24);
        p.setPen(Qt::NoPen); p.setBrush(QColor(16, 30, 41, 240));
        p.drawRoundedRect(QRect(width()-nameWidth-14, height()-39, nameWidth, 26), 5, 5);
        p.setPen(QColor("#EDF4F7"));
        p.drawText(QRect(width()-nameWidth-4, height()-39, nameWidth-20, 26), Qt::AlignCenter,
            p.fontMetrics().elidedText(operatorName, Qt::ElideRight, nameWidth-20));
    }

    if (videoStale && !image.isNull()) {
        p.fillRect(QRect(0, height()/2-23, width(), 46), QColor(32, 28, 23, 235));
        p.setPen(QColor("#F4D197")); p.setFont(theme::font(13));
        p.drawText(QRect(0, height()/2-23, width(), 46), Qt::AlignCenter, "图传已中断 · 当前为最后一帧");
    }
    if (!overlay || (!hasData && image.isNull())) return;
    const bool compact = height() < 280;
    const int w = qMin(width()-32, 540), x = (width()-w)/2;
    const int side = w/4;
    const int captionY = compact ? 17 : 24, valueY = compact ? 36 : 44, valueHeight = compact ? 31 : 38;
    p.setPen(QColor(210, 220, 229, 230)); p.setBrush(QColor(255, 255, 255, 245));
    p.drawRoundedRect(QRect(x, compact ? 12 : 16, w, compact ? 76 : 104), 10, 10);
    p.setPen(Qt::NoPen); p.setBrush(theme::red); p.drawRoundedRect(QRect(x+12, captionY+7, 3, 36), 1, 1);
    p.setBrush(theme::blue); p.drawRoundedRect(QRect(x+w-15, captionY+7, 3, 36), 1, 1);
    p.setFont(theme::font(11)); p.setPen(theme::red);
    p.drawText(QRect(x+20, captionY, side-24, 20), Qt::AlignCenter, "红方得分");
    p.setPen(theme::blue); p.drawText(QRect(x+w-side+4, captionY, side-24, 20), Qt::AlignCenter, "蓝方得分");
    p.setPen(theme::red);
    fitText(p, QRect(x+20, valueY, side-24, valueHeight), data.has_red_score() ? QString::number(data.red_score()) : "—", compact ? 26 : 32, true);
    p.setPen(theme::blue);
    fitText(p, QRect(x+w-side+4, valueY, side-24, valueHeight), data.has_blue_score() ? QString::number(data.blue_score()) : "—", compact ? 26 : 32, true);
    QString phase = hasData && data.has_current_stage() ? status::stage(data.current_stage()) : "等待比赛信息";
    if (hasData && data.has_is_paused()) phase += data.is_paused() ? " · 已暂停" : " · 未暂停";
    p.setPen(hasData && data.has_is_paused() && data.is_paused() ? theme::warning : theme::muted);
    p.setFont(theme::font(11));
    p.drawText(QRect(x+side, captionY, w-2*side, 20), Qt::AlignCenter, p.fontMetrics().elidedText(phase, Qt::ElideRight, w-2*side));
    p.setPen(theme::text);
    fitText(p, QRect(x+side, valueY, w-2*side, valueHeight), data.has_stage_countdown_sec() ? status::duration(data.stage_countdown_sec()) : "--:--", compact ? 26 : 32, true);
    const QString round = hasData ? QString("第 %1 / %2 局")
        .arg(data.has_current_round() ? QString::number(data.current_round()) : "—")
        .arg(data.has_total_rounds() ? QString::number(data.total_rounds()) : "—") : "GameStatus";
    p.setPen(theme::muted); p.setFont(theme::font(11));
    p.drawText(QRect(x+12, compact ? 66 : 87, w-24, compact ? 20 : 23), Qt::AlignCenter, round);

    QString note;
    if (hasData) {
        note = stale ? "比赛信息已过期 · 等待更新" : "已过 " + (data.has_stage_elapsed_sec() ? status::duration(data.stage_elapsed_sec()) : "--:--");
        if (data.has_current_stage() && data.current_stage() == 5) {
            note += " · " + (data.has_game_result() ? status::result(data.game_result()) : "胜者未提供");
            note += " / " + (data.has_end_reason() ? status::reason(data.end_reason()) : "原因未提供");
        } else if (!data.has_is_paused()) {
            note += " · 暂停状态未提供";
        }
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 245));
        p.drawRoundedRect(QRect(x, compact ? 93 : 126, w, compact ? 23 : 27), 5, 5);
        p.setPen(stale ? theme::warning : theme::muted); p.setFont(theme::font(11));
        p.drawText(QRect(x+10, compact ? 93 : 126, w-20, compact ? 23 : 27), Qt::AlignCenter, p.fontMetrics().elidedText(note, Qt::ElideRight, w-20));
    }
}

MainWindow::MainWindow(QString ffmpeg) {
    setWindowTitle("RoboMaster · 单兵客户端");
    setMinimumSize(1000, 720); resize(1360, 840);
    setStyleSheet(theme::stylesheet());
    auto *root = new QWidget; root->setObjectName("root"); setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(22, 18, 22, 18); layout->setSpacing(16);
    auto *header = new QHBoxLayout; header->setSpacing(12); layout->addLayout(header);
    auto *brand = label("RM"); brand->setObjectName("brand"); brand->setAlignment(Qt::AlignCenter); brand->setFixedSize(44, 44); header->addWidget(brand);
    auto *titles = new QVBoxLayout; titles->setSpacing(3); header->addLayout(titles);
    titles->addWidget(label("RoboMaster 单兵客户端", "title"));
    operatorIdentity = label("红方 · 3 号步兵 / 待连接", "muted"); titles->addWidget(operatorIdentity);
    header->addStretch();
    viewButton = new QPushButton("单兵视角  Ctrl+Tab");
    viewButton->setToolTip("在总控台与单兵视角之间切换");
    header->addWidget(viewButton, 0, Qt::AlignVCenter);
    sourceBadge = label("本地模拟", "badge"); header->addWidget(sourceBadge, 0, Qt::AlignVCenter);
    header->addWidget(label("RM2026 · V2.0.0", "muted"));

    auto *body = new QHBoxLayout; body->setSpacing(16); layout->addLayout(body, 1);
    pages = new QStackedWidget; body->addWidget(pages, 1);
    consolePage = new ConsolePage(&match); pages->addWidget(consolePage);
    auto *viewer = panel(); pages->addWidget(viewer);
    auto *viewLayout = new QVBoxLayout(viewer); viewLayout->setContentsMargins(16, 14, 16, 16); viewLayout->setSpacing(10);
    auto *viewHeader = new QHBoxLayout; viewLayout->addLayout(viewHeader);
    viewHeader->addWidget(label("主视角", "section")); liveBadge = label("等待画面", "badge"); viewHeader->addWidget(liveBadge, 0, Qt::AlignVCenter); viewHeader->addStretch();
    focusButton = new QPushButton("专注  F10"); focusButton->setCheckable(true);
    focusButton->setToolTip("隐藏配置、接收统计和日志，保留图传、比赛概览与链路提醒"); viewHeader->addWidget(focusButton);
    fullScreenButton = new QPushButton("全屏  F11"); fullScreenButton->setToolTip("F11 切换全屏，Esc 退出全屏"); viewHeader->addWidget(fullScreenButton);
    canvas = new VideoCanvas;
    matchSummary = new MatchSummary(canvas); viewLayout->addWidget(matchSummary);
    viewLayout->addWidget(canvas, 1);
    auto *videoTools = new QHBoxLayout; viewLayout->addLayout(videoTools);
    overlay = new QCheckBox("画内叠加"); overlay->setToolTip("在图传内额外叠加比分；关闭后上方比赛概览仍可查看");
    videoTools->addWidget(overlay); videoTools->addStretch();
    videoInfo = label("HEVC 图传 · 等待输入", "muted"); videoTools->addWidget(videoInfo);
    matchNotice = label("比赛信息：等待连接", "notice"); matchNotice->setWordWrap(true); viewLayout->addWidget(matchNotice);
    diagnostics = new QWidget; auto *stats = new QHBoxLayout(diagnostics); stats->setContentsMargins(0, 0, 0, 0);
    stats->setSpacing(16); viewLayout->addWidget(diagnostics);
    auto metric = [stats](const QString &caption, const QString &value) {
        auto *column = new QVBoxLayout; column->setSpacing(4); stats->addLayout(column, 1);
        column->addWidget(label(caption, "muted")); auto *number = label(value, "metric"); column->addWidget(number); return number;
    };
    messageCount = metric("已收比赛信息", "0"); dataAge = metric("距最近更新", "—");
    dataAge->setToolTip("自最近一次收到 GameStatus 起经过的时间，不代表网络延迟");
    frameRate = metric("解码帧率", "0 fps"); dropCount = metric("丢弃视频帧", "0");

    sidebar = new QScrollArea; sidebar->setObjectName("sidebar"); sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setWidgetResizable(true); sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); sidebar->setFixedWidth(312); body->addWidget(sidebar);
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
        if (!active && canvas->image.isNull() && !canvas->hasData) canvas->simulation = i == 0;
        mode->setToolTip(active ? "参数将在重新连接后生效" : "选择本地模拟或实机链路"); refresh();
    });
    connect(team, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateProfile);
    connect(robotRole, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateProfile);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::startConnection);
    connect(stopButton, &QPushButton::clicked, this, [this] {
        active = false; receiver.stop(); video.stop(); mqttReady = false; lastData = lastFrame = -1; match.reset();
        stopButton->setEnabled(false); frameRate->setText("0 fps"); updateProfile(); refresh();
    });
    connect(overlay, &QCheckBox::toggled, this, [this](bool value) { canvas->overlay = value; canvas->update(); });
    connect(&receiver, &StatusReceiver::stateChanged, this, [this](const QString &text, bool ready) {
        connection->setText(text); mqttReady = ready; addEvent(text); refresh();
    });
    connect(&receiver, &StatusReceiver::received, this, [this](const rm::GameStatus &value) {
        match.applyGame(value);
        recordMatchChanges(value);
        for (const auto &issue : status::warnings(value)) addEvent("协议警告：" + issue);
        canvas->data = value; canvas->hasData = true; lastData = clock.elapsed(); ++messages;
        auto object = status::json(value); object["received_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const auto text = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
        appendLog(log, text); QTextStream(stdout) << text << Qt::endl;
        lastUpdate->setText("最近接收 " + QDateTime::currentDateTime().toString("HH:mm:ss"));
        canvas->setAccessibleDescription(QString("%1，红方 %2，蓝方 %3，剩余 %4")
            .arg(value.has_current_stage() ? status::stage(value.current_stage()) : "阶段未提供")
            .arg(value.has_red_score() ? QString::number(value.red_score()) : "未提供")
            .arg(value.has_blue_score() ? QString::number(value.blue_score()) : "未提供")
            .arg(value.has_stage_countdown_sec() ? status::duration(value.stage_countdown_sec()) : "未提供"));
        matchSummary->setAccessibleDescription(canvas->accessibleDescription());
        refresh();
    });
    connect(&video, &VideoReceiver::frameReady, this, [this](QImage frame) {
        canvas->image = frame; lastFrame = clock.elapsed(); canvas->videoStale = false; canvas->update();
    });
    // 总控台数据域：全部进入 MatchState；慢速/触发式域同时落 JSON 日志，10Hz 动态域只进模型。
    const auto appendDomain = [this](QJsonObject object) {
        object["received_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        appendLog(log, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    };
    connect(&receiver, &StatusReceiver::receivedUnitStatus, this, [this, appendDomain](const rm::GlobalUnitStatus &value) {
        match.applyUnitStatus(value); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedLogistics, this, [this, appendDomain](const rm::GlobalLogisticsStatus &value) {
        match.applyLogistics(value); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedSpecialMechanism, &match, &MatchState::applySpecialMechanism);
    connect(&receiver, &StatusReceiver::receivedEvent, this, [this, appendDomain](const rm::Event &value) {
        match.applyEvent(value); addEvent(status::eventText(value)); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedInjury, this, [this, appendDomain](const rm::RobotInjuryStat &value) {
        match.applyInjury(value); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedRespawn, this, [this, appendDomain](const rm::RobotRespawnStatus &value) {
        match.applyRespawn(value); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedStatic, this, [this, appendDomain](const rm::RobotStaticStatus &value) {
        match.applyStatic(value); appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedDynamic, &match, &MatchState::applyDynamic);
    connect(&receiver, &StatusReceiver::receivedModule, &match, &MatchState::applyModule);
    connect(&receiver, &StatusReceiver::receivedPosition, &match, &MatchState::applyPosition);
    connect(&receiver, &StatusReceiver::receivedBuff, &match, &MatchState::applyBuff);
    connect(&receiver, &StatusReceiver::receivedPenalty, this, [this, appendDomain](const rm::PenaltyInfo &value) {
        match.applyPenalty(value);
        addEvent(QString("判罚：%1").arg(value.has_penalty_type() ? status::penaltyType(value.penalty_type()) : "未提供"));
        appendDomain(status::json(value));
    });
    connect(&receiver, &StatusReceiver::receivedRadar, &match, &MatchState::applyRadar);
    connect(&video, &VideoReceiver::problem, this, &MainWindow::addEvent);
    connect(focusButton, &QPushButton::toggled, this, &MainWindow::setFocusMode);
    connect(new QShortcut(QKeySequence("F10"), this), &QShortcut::activated, this, [this] { setFocusMode(!focusMode); });
    connect(fullScreenButton, &QPushButton::clicked, this, &MainWindow::toggleFullScreen);
    connect(new QShortcut(QKeySequence("F11"), this), &QShortcut::activated, this, &MainWindow::toggleFullScreen);
    connect(viewButton, &QPushButton::clicked, this, &MainWindow::switchView);
    connect(new QShortcut(QKeySequence("Ctrl+Tab"), this), &QShortcut::activated, this, &MainWindow::switchView);
    connect(new QShortcut(QKeySequence("Esc"), this), &QShortcut::activated, this, [this] {
        if (isFullScreen()) toggleFullScreen(); else if (focusMode) setFocusMode(false);
    });
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

void MainWindow::updateProfile() {
    const int id = profile::robotId(team->currentIndex() == 1, robotRole->currentData().toInt());
    robotId->setText(QString::number(id));
    const bool pending = active && id != connectedRobotId;
    profileHint->setText(QString("机器人 ID %1 · %2").arg(id).arg(pending ? "点击下方按钮应用切换" : "需与官方选手端登录编号一致"));
    connectButton->setText(pending ? "应用兵种并重连" : active ? "重新连接当前机器人" : "连接所选机器人");
    const bool previous = !active && (canvas->hasData || !canvas->image.isNull());
    const int displayedId = active || previous ? connectedRobotId : id;
    operatorIdentity->setText(QString("%1 / ID %2 · %3").arg(profile::name(displayedId)).arg(displayedId)
        .arg(active ? "当前连接" : previous ? "已断开，保留上次画面" : "待连接"));
    // 编辑下一次连接的操作位时，不改写旧画面的来源身份。
    canvas->operatorName = profile::name(displayedId) + QString(" / ID %1").arg(displayedId);
    canvas->update();
}

void MainWindow::setFocusMode(bool enabled) {
    focusMode = enabled;
    const QSignalBlocker block(focusButton);
    focusButton->setChecked(enabled); focusButton->setText(enabled ? "退出专注  F10" : "专注  F10");
    sidebar->setVisible(!enabled); logPanel->setVisible(!enabled);
    diagnostics->setVisible(!enabled && !logToggle->isChecked());
    if (enabled) focusButton->setFocus();
}

void MainWindow::recordMatchChanges(const rm::GameStatus &value) {
    const bool continuous = canvas->hasData && mqttReady && lastData >= 0 && clock.elapsed()-lastData <= 1500;
    if (!continuous) {
        addEvent(QString("%1：%2").arg(canvas->hasData ? "比赛快照恢复" : "首次收到比赛信息")
            .arg(value.has_current_stage() ? status::stage(value.current_stage()) : "阶段未提供"));
        return;
    }
    for (const auto &text : status::changes(canvas->data, value)) addEvent(text);
}

void MainWindow::refreshStatusDetails() {
    if (statusValues.size() != 10) return;
    const auto &v = canvas->data;
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
    const QString state = !canvas->hasData ? "未接收" : canvas->stale ? "已过期" : issues.isEmpty() ? "实时" : "协议警告";
    badge(statusBadge, state, !canvas->hasData ? "neutral" : canvas->stale || !issues.isEmpty() ? "warning" : "good");
    statusMeta->setText(QString("协议 RM2026-V2.0.0 · 已提供 %1 / 10 字段%2")
        .arg(present).arg(canvas->stale && canvas->hasData ? " · 快照已过期" : ""));
    if (issues.isEmpty()) statusWarning->hide();
    else { statusWarning->setText("协议警告：" + issues.join("；")); statusWarning->show(); }
    statusPanel->setAccessibleDescription(statusMeta->text() + (issues.isEmpty() ? "" : "；" + statusWarning->text()));
}

void MainWindow::switchView() {
    const bool toConsole = pages->currentWidget() != consolePage;
    pages->setCurrentIndex(toConsole ? 0 : 1);
    // 按钮文案始终显示切换目标：当前是总控台时提示可切到单兵视角。
    viewButton->setText(toConsole ? "单兵视角  Ctrl+Tab" : "总控台  Ctrl+Tab");
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
    receiver.stop(); video.stop(); messages = 0; lastData = lastFrame = -1; mqttReady = false;
    match.reset();
    fpsSampleAt = clock.elapsed(); fpsSampleFrames = 0; frameRate->setText("0 fps");
    canvas->data.Clear(); canvas->hasData = false; canvas->image = {}; canvas->setAccessibleDescription("等待比赛信息");
    matchSummary->setAccessibleDescription("等待比赛信息");
    canvas->simulation = mode->currentIndex() == 0; active = true; connectedRobotId = robotId->text().toInt();
    consolePage->setRobot(connectedRobotId);
    updateProfile(); addEvent("连接操作位：" + canvas->operatorName);
    stopButton->setEnabled(true); lastUpdate->setText("最近接收 —");
    const bool mqttStarted = receiver.start(host->text().trimmed(), mqttPort->value(), robotId->text().trimmed());
    const bool videoStarted = video.start(bindIp->text().trimmed(), quint16(udpPort->value()), ffmpegPath->text().trimmed());
    if (!mqttStarted || !videoStarted) {
        // 任一链路无法创建时立即回滚，避免界面显示“已连接”但后台仍残留半条链路。
        receiver.stop(); video.stop(); active = false; mqttReady = false;
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
void MainWindow::toggleFullScreen() {
    if (isFullScreen()) showNormal(); else showFullScreen();
    fullScreenButton->setText(isFullScreen() ? "退出全屏  Esc" : "全屏  F11");
}
void MainWindow::refresh() {
    if (!clock.isValid()) return;
    const auto now = clock.elapsed();
    canvas->stale = !mqttReady || lastData < 0 || now-lastData > 1500;
    canvas->videoStale = lastFrame < 0 || now-lastFrame > 1500;
    badge(sourceBadge, canvas->simulation ? "本地模拟" : "实机链路", canvas->simulation ? "warning" : "neutral");
    badge(dataState, !active ? "未连接" : canvas->stale ? (lastData < 0 ? "等待数据" : "数据过期") : "实时更新",
          !active ? "neutral" : canvas->stale ? "warning" : "good");
    const QString videoText = !active ? "未连接" : canvas->videoStale ? (lastFrame < 0 ? "等待画面" : "图传中断") : "正在播放";
    const char *videoTone = !active ? "neutral" : canvas->videoStale ? "warning" : "good";
    badge(videoState, videoText, videoTone); badge(liveBadge, videoText, videoTone);
    QString notice;
    const char *noticeTone = "neutral";
    if (!active) notice = canvas->hasData ? "已断开连接 · 比赛概览保留上次快照" : "选择操作位并连接，开始接收比赛信息与图传";
    else if (canvas->stale) {
        notice = lastData < 0 ? "等待比赛信息 · 请检查服务端和机器人编号" : "比赛信息已过期 · 倒计时保留最后接收值";
        noticeTone = "warning";
    } else if (canvas->videoStale) {
        notice = lastFrame < 0 ? "比赛信息正常 · 等待图传输入" : "图传已中断 · 当前画面为最后一帧";
        noticeTone = "warning";
    } else if (canvas->data.has_is_paused() && canvas->data.is_paused()) {
        notice = "比赛已暂停 · 倒计时以裁判系统最新数据为准"; noticeTone = "warning";
    } else notice = "比赛信息实时更新 · 图传接收正常";
    badge(matchNotice, notice, noticeTone);
    matchNotice->setAccessibleDescription(notice);
    messageCount->setText(QString::number(messages)); dropCount->setText(QString::number(video.dropped()));
    dataAge->setText(lastData < 0 ? "—" : now-lastData < 1000 ? QString("%1 ms").arg(now-lastData) : QString("%1 s").arg((now-lastData)/1000.0, 0, 'f', 1));
    if (now-fpsSampleAt >= 1000) {
        frameRate->setText(QString("%1 fps").arg((video.decoded-fpsSampleFrames)*1000.0/(now-fpsSampleAt), 0, 'f', 0));
        fpsSampleFrames = video.decoded; fpsSampleAt = now;
    }
    videoInfo->setText(canvas->image.isNull() ? "HEVC 图传 · 等待输入" : QString("%1 × %2 · HEVC").arg(canvas->image.width()).arg(canvas->image.height()));
    const QString base = video.sliceBase() < 0 ? "未判定" : QString::number(video.sliceBase());
    packetInfo->setText(QString("UDP %1 包 · 无效包 %2 · 分片基数 %3").arg(video.packets).arg(video.invalid()).arg(base));
    logToggle->setText(QString("接收日志 · %1 条比赛信息").arg(messages)); canvas->update(); matchSummary->update();
    refreshStatusDetails();
}
QJsonObject MainWindow::metrics() const {
    return {{"messages", double(messages)}, {"decoded_frames", double(video.decoded)}, {"udp_packets", double(video.packets)},
        {"robot_id", connectedRobotId}, {"selected_robot_id", robotId->text().toInt()}, {"operator", profile::name(connectedRobotId)},
        {"data_stale", canvas->stale}, {"video_stale", canvas->videoStale},
        {"mqtt_received", double(receiver.receivedMessages)}, {"mqtt_malformed", double(receiver.malformedMessages)},
        {"last_payload_bytes", receiver.lastPayloadBytes}, {"last_qos", receiver.lastQos},
        {"video_slice_base", video.sliceBase()}, {"video_zero_based_frames", double(video.zeroBasedFrames())},
        {"video_one_based_frames", double(video.oneBasedFrames())}, {"console_page", double(pages->currentIndex())},
        {"status", status::json(canvas->data)}};
}
bool MainWindow::saveEvidence(const QString &path) { return grab().save(path); }
bool MainWindow::runUiChecks(const QString &evidencePrefix) {
    const auto originalSize = size();
    const bool originalOverlay = overlay->isChecked();
    const int originalSelection = robotId->text().toInt();
    const int originalConnection = connectedRobotId;
    const QString originalIdentity = canvas->operatorName;
    auto checkpoint = [](bool passed, const char *name) {
        if (!passed) QTextStream(stderr) << "UI 检查失败：" << name << Qt::endl;
    };
    overlay->setChecked(false); bool okay = !canvas->overlay;
    overlay->setChecked(true); okay = okay && canvas->overlay;
    overlay->setChecked(originalOverlay);
    for (const bool blue : {false, true}) for (const auto &role : profile::roles) {
        const int id = profile::robotId(blue, role.number);
        okay = selectRobot(id) && robotId->text().toInt() == id && okay;
        if (active) okay = connectedRobotId == originalConnection && canvas->operatorName == originalIdentity && okay;
    }
    okay = !selectRobot(100) && !selectRobot(10) && okay;
    selectRobot(originalSelection);
    checkpoint(okay, "兵种选择与待应用身份");
    refreshStatusDetails();
    okay = statusPanel->isVisible() && statusValues.size() == 10 && okay;
    if (canvas->hasData) {
        okay = statusValues[4]->text().contains("比赛") && statusMeta->text().contains("/ 10") && okay;
    }
    checkpoint(okay, "GameStatus 十字段面板");
    const int originalPage = pages->currentIndex();
    pages->setCurrentWidget(consolePage);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    okay = pages->currentWidget() == consolePage && consolePage->scoreBar()->isVisible()
        && consolePage->allyList()->isVisible() && consolePage->enemyList()->isVisible()
        && consolePage->allyList()->rowCount() == 5 && consolePage->enemyList()->rowCount() == 5 && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-console.png") && okay;
    checkpoint(okay, "总控台页面");
    pages->setCurrentIndex(1);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const int normalWidth = canvas->width();
    setFocusMode(true); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    okay = sidebar->isHidden() && logPanel->isHidden() && diagnostics->isHidden()
        && matchSummary->isVisible() && matchNotice->isVisible() && canvas->width() > normalWidth && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-focus.png") && okay;
    setFocusMode(false); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    okay = !sidebar->isHidden() && !logPanel->isHidden() && okay;
    checkpoint(okay, "专注模式");
    advancedToggle->setChecked(true); okay = okay && !advanced->isHidden();
    logToggle->setChecked(true); okay = okay && !logBody->isHidden();
    pauseLog->setChecked(true); okay = okay && pauseLog->isChecked(); pauseLog->setChecked(false);
    const QString validHost = host->text(); host->clear(); okay = !validateForm() && !formError->isHidden() && okay;
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-validation.png") && okay;
    host->setText(validHost); okay = validateForm() && okay;
    checkpoint(okay, "日志与表单");
    resize(1024, 720); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    okay = canvas->width() >= 440 && sidebar->geometry().right() < centralWidget()->width()
        && canvas->geometry().bottom() < overlay->mapTo(canvas->parentWidget(), QPoint()).y() && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-compact.png") && okay;
    checkpoint(okay, "紧凑窗口布局");
    resize(minimumSize()); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    okay = canvas->geometry().bottom() < overlay->mapTo(canvas->parentWidget(), QPoint()).y() && okay;
    if (!evidencePrefix.isEmpty()) okay = saveEvidence(evidencePrefix + "-minimum.png") && okay;
    checkpoint(okay, "最小窗口布局");
    advancedToggle->setChecked(false); logToggle->setChecked(false);
    toggleFullScreen(); okay = isFullScreen() && okay; toggleFullScreen(); okay = !isFullScreen() && okay;
    pages->setCurrentIndex(originalPage);
    resize(originalSize); return okay;
}
