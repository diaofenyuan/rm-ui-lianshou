#pragma once
#include <QString>
#include <array>

namespace profile {
struct Role { int number; const char *name; };
// 编号取自 RM2026 V2.0.0 附录二；MQTT 使用机器人编号，不使用选手端十六进制 ID。
inline constexpr std::array<Role, 9> roles{{
    {1, "英雄"}, {2, "工程"}, {3, "步兵"}, {4, "步兵"}, {5, "步兵"},
    {6, "空中"}, {7, "哨兵"}, {8, "飞镖"}, {9, "雷达"}
}};
inline bool valid(int id) {
    return (id >= 1 && id <= 9) || (id >= 101 && id <= 109);
}
inline int robotId(bool blue, int number) {
    return number >= 1 && number <= 9 ? (blue ? 100 : 0) + number : 0;
}
inline QString name(int id) {
    if (!valid(id)) return "未选择操作位";
    const int number = id % 100;
    return QString("%1 · %2 号%3").arg(id > 100 ? "蓝方" : "红方").arg(number)
        .arg(QString::fromUtf8(roles[number - 1].name));
}
}
