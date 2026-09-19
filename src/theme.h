#pragma once
#include <QColor>
#include <QFont>
#include <QString>

namespace theme {
inline const QColor text{"#182B3A"};
inline const QColor muted{"#596C7A"};
inline const QColor green{"#087F68"};
inline const QColor red{"#BE3451"};
inline const QColor blue{"#2469BC"};
inline const QColor warning{"#956017"};
// 窗口底色，与 stylesheet() 中 QWidget#root 的背景一致；自绘控件需要不透明重绘时用它打底。
inline const QColor pageBackground{"#F5F7F9"};

// 单兵模式 HUD 专用配色：HUD 直接叠在深色图传之上，必须自带宽底与浅色前景，
// 不能复用上面的浅色主题（浅色主题仍供窗口其余部分与总控模式使用）。
namespace hud {
inline const QColor card{14, 24, 33, 208};        // 常规半透明卡底
inline const QColor cardStrong{11, 19, 27, 232};  // 顶部信息条等需要压住画面的区域
inline const QColor cardSoft{14, 24, 33, 168};    // 专注模式下略降不透明度
inline const QColor edge{255, 255, 255, 40};      // 卡边框
inline const QColor track{255, 255, 255, 46};     // 血条 / 热量条底槽
inline const QColor text{"#EAF2F7"};
inline const QColor muted{"#A6BAC7"};
inline const QColor good{"#4ED2A8"};
inline const QColor warn{"#F0C069"};
inline const QColor bad{"#F2758D"};
inline const QColor accent{"#7FC4FF"};
inline const QColor paused{128, 82, 18, 150};      // 暂停态整条底
inline const QColor settled{22, 62, 104, 168};     // 结算态整条底

// HUD 元素尺寸随图传实际高度缩放：1080p 与 720p 窗口下保持同一套视觉比例。
inline qreal scaleFor(int imageHeight) {
    return qBound(0.62, imageHeight / 720.0, 2.0);
}
}

inline QFont font(int pixels, bool bold = false, bool numeric = false) {
    QFont value(numeric ? "Consolas" : "Microsoft YaHei UI");
    value.setPixelSize(pixels);
    value.setBold(bold);
    return value;
}

inline QString stylesheet() {
    return QString::fromUtf8(R"(
QMainWindow, QWidget#root { background: #F5F7F9; }
QWidget { color: #182B3A; font-family: 'Microsoft YaHei UI'; font-size: 13px; }
QFrame[role="panel"] { background: #FFFFFF; border: 1px solid #DAE2E8; border-radius: 12px; }
QWidget#side, QScrollArea#sidebar, QScrollArea#sidebar > QWidget > QWidget { background: transparent; }
QLabel { background: transparent; border: none; }
QLabel[role="title"] { font-size: 21px; font-weight: 700; }
QLabel[role="section"] { font-size: 15px; font-weight: 700; }
QLabel[role="muted"] { color: #596C7A; font-size: 12px; }
QLabel[role="metric"] { font-family: Consolas; font-size: 25px; font-weight: 700; }
QLabel[role="notice"] { background: #EDF1F4; color: #596C7A; border-radius: 5px; padding: 6px 10px; font-size: 12px; }
QLabel[role="badge"] { border-radius: 5px; background: #EDF1F4; color: #596C7A; padding: 5px 9px; font-size: 12px; }
QLabel[tone="good"] { background: #E4F4ED; color: #087258; }
QLabel[tone="warning"] { background: #FFF3DF; color: #895710; }
QLabel[tone="error"] { color: #B22D47; }
QLabel#brand { background: #087F68; color: white; border-radius: 10px; font: bold 19px Consolas; }
QLabel#formError { color: #B22D47; font-size: 12px; }
QLineEdit, QSpinBox, QComboBox { background: #FFFFFF; border: 1px solid #BDCBD5; border-radius: 6px; padding: 8px; min-height: 20px; selection-background-color: #D6EEE7; selection-color: #182B3A; }
QLineEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: #718A9B; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #087F68; background: #FAFFFD; }
QLineEdit[invalid="true"] { border-color: #B22D47; background: #FFF9FA; }
QLineEdit[readOnly="true"] { background: #F3F5F7; color: #596C7A; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow, QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0; height: 0; }
QSpinBox { padding-right: 24px; }
QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 22px; border-left: 1px solid #BDCBD5; }
QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; border-left: 1px solid #BDCBD5; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #E4F4ED; }
QComboBox QAbstractItemView { background: white; color: #182B3A; selection-background-color: #E4F4ED; selection-color: #075B4A; padding: 5px; border: 1px solid #BDCBD5; }
QPushButton { background: #FFFFFF; border: 1px solid #BDCBD5; border-radius: 6px; padding: 8px 13px; min-height: 20px; font-weight: 600; }
QPushButton:hover { background: #F0F5F7; border-color: #718A9B; }
QPushButton:pressed { background: #E4EDF1; }
QPushButton:checked { background: #E4F4ED; color: #075B4A; border-color: #087F68; }
QPushButton:focus, QToolButton:focus { border: 1px solid #087F68; }
QPushButton[role="primary"] { background: #087F68; border-color: #087F68; color: white; }
QPushButton[role="primary"]:hover { background: #076B58; border-color: #076B58; }
QPushButton[role="primary"]:pressed { background: #055845; }
QPushButton[role="primary"]:focus { border: 2px solid #182B3A; padding: 7px 12px; }
QPushButton:disabled { background: #F3F5F7; border-color: #E1E6EB; color: #7C8B96; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 7px 4px; text-align: left; color: #405968; }
QToolButton:hover { background: #EDF4F3; }
QCheckBox { spacing: 7px; padding: 5px 0; }
QCheckBox::indicator { width: 17px; height: 17px; }
QCheckBox:focus { color: #075B4A; outline: 1px solid #087F68; }
QPlainTextEdit { background: #F8FAFB; border: 1px solid #DAE2E8; border-radius: 6px; padding: 8px; font: 12px Consolas; selection-background-color: #CCE9E1; selection-color: #182B3A; }
QPlainTextEdit:focus { border-color: #087F68; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: #596C7A; padding: 9px 13px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #087258; border-bottom-color: #087F68; font-weight: 600; }
QTabBar::tab:hover { background: #EFF6F3; }
QScrollBar:vertical { background: #EDF1F4; width: 10px; margin: 0; border-radius: 5px; }
QScrollBar::handle:vertical { background: #AEBDC8; min-height: 28px; border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: #718A9B; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: #182B3A; color: white; border: none; padding: 6px; }
)");
}
}
