#include "receiver.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("rm_receive");
    QCommandLineParser p;p.addHelpOption();
    p.addOptions({{"host","MQTT 服务器","host","127.0.0.1"},{"port","MQTT 端口","port","3333"},
        {"robot-id","机器人编号 / MQTT Client ID","id","3"},{"count","收到指定条数后退出，0 表示持续运行","count","0"},
        {"timeout","超时秒数，0 表示不限制","seconds","0"}});p.process(app);
    bool valid=false;int port=p.value("port").toInt(&valid);if(!valid||port<1||port>65535){QTextStream(stderr)<<"端口无效\n";return 2;}
    StatusReceiver receiver;int count=0;
    QObject::connect(&receiver,&StatusReceiver::stateChanged,[](QString text,bool){QTextStream(stderr)<<text<<Qt::endl;});
    QObject::connect(&receiver,&StatusReceiver::received,[&](const rm::GameStatus &v){
        auto o=status::json(v);o["received_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        QTextStream(stdout)<<QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact))<<Qt::endl;
        if(p.value("count").toInt()>0&&++count>=p.value("count").toInt())app.quit();
    });
    receiver.start(p.value("host"),port,p.value("robot-id"));
    if(p.value("timeout").toInt()>0)QTimer::singleShot(p.value("timeout").toInt()*1000,&app,[&]{app.exit(2);});
    return app.exec();
}
