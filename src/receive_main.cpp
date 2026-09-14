#include "receiver.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <QTextStream>
#include <limits>

namespace {
bool readSeconds(const QString &text, int *value) {
    bool ok = false;
    const qint64 seconds = text.toLongLong(&ok);
    if (!ok || seconds < 0 || seconds > std::numeric_limits<int>::max() / 1000) return false;
    *value = int(seconds);
    return true;
}
bool readCount(const QString &text, int *value) {
    bool ok = false;
    const qint64 count = text.toLongLong(&ok);
    if (!ok || count < 0 || count > std::numeric_limits<int>::max()) return false;
    *value = int(count);
    return true;
}
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("rm_receive");
    QCommandLineParser p;p.addHelpOption();
    p.addOptions({{"host","MQTT 服务器","host","127.0.0.1"},{"port","MQTT 端口","port","3333"},
        {"robot-id","机器人编号 / MQTT Client ID","id","3"},{"count","收到指定条数后退出，0 表示持续运行","count","0"},
        {"timeout","超时秒数，0 表示不限制","seconds","0"},
        {"output","JSONL 输出文件（省略则输出到标准输出）","file"}});p.process(app);
    bool valid=false;int port=p.value("port").toInt(&valid);if(!valid||port<1||port>65535){QTextStream(stderr)<<"端口无效\n";return 2;}
    const QString host = p.value("host").trimmed();
    const QString robotId = p.value("robot-id").trimmed();
    if (host.isEmpty()) { QTextStream(stderr) << "MQTT 服务器不能为空\n"; return 2; }
    if (robotId.isEmpty()) { QTextStream(stderr) << "机器人 ID 不能为空\n"; return 2; }
    int expectedCount = 0, timeoutSeconds = 0;
    if (!readCount(p.value("count"), &expectedCount)) { QTextStream(stderr) << "count 无效\n"; return 2; }
    if (!readSeconds(p.value("timeout"), &timeoutSeconds)) { QTextStream(stderr) << "timeout 无效\n"; return 2; }
    QFile outputFile;
    QTextStream fileStream;
    if (p.isSet("output")) {
        const QString outputPath = p.value("output").trimmed();
        if (outputPath.isEmpty()) { QTextStream(stderr) << "output 不能为空\n"; return 2; }
        const QFileInfo info(outputPath);
        if (!info.absolutePath().isEmpty() && !QDir().mkpath(info.absolutePath())) {
            QTextStream(stderr) << "无法创建输出目录：" << info.absolutePath() << Qt::endl; return 2;
        }
        outputFile.setFileName(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream(stderr) << "无法打开输出文件：" << outputPath << "（" << outputFile.errorString() << "）" << Qt::endl;
            return 2;
        }
        fileStream.setDevice(&outputFile);
    }
    StatusReceiver receiver;
    QTextStream stdoutStream(stdout);
    int count=0;
    QObject::connect(&receiver,&StatusReceiver::stateChanged,[](QString text,bool){QTextStream(stderr)<<text<<Qt::endl;});
    QObject::connect(&receiver,&StatusReceiver::receivedInfo,[&](const rm::GameStatus &v, int payloadBytes, int qos){
        auto o=status::json(v);o["received_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        o["topic"] = "GameStatus"; o["qos"] = qos; o["payload_bytes"] = payloadBytes;
        o["received_index"] = ++count; o["protocol"] = "RM2026-V2.0.0";
        const QString line = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
        QTextStream &sink = outputFile.isOpen() ? fileStream : stdoutStream;
        sink << line << Qt::endl; sink.flush();
        if(expectedCount>0&&count>=expectedCount)app.quit();
    });
    QTextStream(stderr) << "订阅 GameStatus：tcp://" << host << ":" << port
                        << "，Client ID=" << robotId;
    if (outputFile.isOpen()) QTextStream(stderr) << "，输出=" << outputFile.fileName();
    QTextStream(stderr) << '\n';
    if (!receiver.start(host,port,robotId)) return 2;
    QTimer timeoutTimer;
    if(timeoutSeconds>0) {
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer,&QTimer::timeout,&app,[&]{
            QTextStream(stderr) << "等待 GameStatus 超时（" << timeoutSeconds << " 秒）" << Qt::endl;
            app.exit(2);
        });
        timeoutTimer.start(timeoutSeconds*1000);
    }
    return app.exec();
}
