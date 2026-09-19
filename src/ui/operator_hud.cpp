#include "operator_hud.h"
#include "theme.h"
#include <QResizeEvent>

namespace {
int clampInt(int value, int low, int high) { return qBound(low, value, qMax(low, high)); }
}

OperatorHud::OperatorHud(MatchState *state, QWidget *parent) : QWidget(parent), match(state) {
    setAccessibleName("单兵模式信息叠加：顶部信息条、队友状态、本机状态与全场地图");
    // 不绘制自身：卡底由各信息件自己画，避免在这一层重复合成。
    setAttribute(Qt::WA_NoSystemBackground, true);
    statusStrip = new StatusStrip(match, this);
    teammatePanel = new TeammatePanel(match, this);
    ownCardPanel = new OwnStatusCard(match, this);
    mapPanel = new MinimapPanel(match, this);
    mapPanel->setHudMode(true);
}

void OperatorHud::setRobot(int id) {
    statusStrip->setRobot(id);
    teammatePanel->setRobot(id);
    ownCardPanel->setRobot(id);
    mapPanel->setOwnRobot(id);
}

void OperatorHud::setLinkState(bool ready, bool dataAlive, bool imageAlive) {
    statusStrip->setLinkState(ready, dataAlive, imageAlive);
}

void OperatorHud::setScale(qreal value) {
    scale = value;
    statusStrip->setScale(value);
    teammatePanel->setScale(value);
    ownCardPanel->setScale(value);
    resizeEvent(nullptr);
    update();
}

void OperatorHud::setFocused(bool value) {
    if (focused == value) return;
    focused = value;
    // 专注模式仍保留全部 HUD，只把卡底调淡一点，让画面更透。
    const qreal opacity = focused ? 0.88 : 1.0;
    statusStrip->setOpacity(opacity);
    teammatePanel->setOpacity(opacity);
    ownCardPanel->setOpacity(opacity);
}

void OperatorHud::setMapVisible(bool visible) {
    mapPanel->setVisible(visible);
    if (visible) mapPanel->raise();
}
bool OperatorHud::mapVisible() const { return mapPanel->isVisible(); }

void OperatorHud::refresh() {
    statusStrip->update();
    teammatePanel->update();
    ownCardPanel->update();
    mapPanel->update();
}

void OperatorHud::resizeEvent(QResizeEvent *) {
    if (!statusStrip) return;
    const qreal s = scale;
    const int margin = clampInt(qRound(14 * s), 6, 40);
    const int gap = clampInt(qRound(10 * s), 4, 24);
    const int stripHeight = clampInt(qRound(58 * s), 40, 78);
    // 本机卡是 3 列 × 2 行，低于约 84 px 时"字段名 + 数值"两行会互相压住，故下限取 84。
    const int ownHeight = clampInt(qRound(104 * s), 84, 200);

    statusStrip->setGeometry(margin, margin, qMax(80, width() - 2 * margin), stripHeight);

    int mapWidth = clampInt(qRound(width() * 0.24), 140, qRound(420 * s));
    int cardWidth = clampInt(qRound(width() * 0.30), 140, qRound(300 * s));
    // 左右两块必须同时放下：地图是可读性下限更低的一侧，先压缩队友面板。
    const int budget = width() - 2 * margin - gap;
    if (cardWidth + mapWidth > budget) cardWidth = qMax(120, budget - mapWidth);
    if (cardWidth + mapWidth > budget) mapWidth = qMax(110, budget - cardWidth);
    const int mapHeight = qRound(mapWidth * 9.0 / 16.0);

    const int top = margin + stripHeight + gap;
    const int bottom = height() - margin;
    const int ownTop = bottom - ownHeight;

    ownCardPanel->setGeometry(margin, ownTop, cardWidth, ownHeight);
    teammatePanel->setGeometry(margin, top, cardWidth, qMax(60, ownTop - gap - top));
    mapPanel->setGeometry(width() - margin - mapWidth, top, mapWidth, mapHeight);
    if (mapPanel->isVisible()) mapPanel->raise();
}
