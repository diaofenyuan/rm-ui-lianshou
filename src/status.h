#pragma once
#include "game_status.pb.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace status {
QString stage(quint32 value);
QString result(quint32 value);
QString reason(quint32 value);
QString duration(qint32 seconds);
bool isSettlement(const rm::GameStatus &value);
QStringList warnings(const rm::GameStatus &value);
QJsonObject json(const rm::GameStatus &value);
QStringList changes(const rm::GameStatus &before, const rm::GameStatus &after);
}
Q_DECLARE_METATYPE(rm::GameStatus)
