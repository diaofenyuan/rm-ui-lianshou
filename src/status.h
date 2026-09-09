#pragma once
#include "game_status.pb.h"
#include <QJsonObject>
#include <QString>

namespace status {
QString stage(quint32 value);
QString result(quint32 value);
QString reason(quint32 value);
QString duration(qint32 seconds);
QJsonObject json(const rm::GameStatus &value);
}
Q_DECLARE_METATYPE(rm::GameStatus)
