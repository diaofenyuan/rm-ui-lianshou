#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setApplicationName("rm_client");
    QCommandLineParser p;p.addHelpOption();
    p.addOptions({{"connect","启动后连接当前配置"},{"ffmpeg","FFmpeg 可执行文件","path","ffmpeg"},
        {"smoke-seconds","运行指定秒数后检查接收状态并退出","seconds","0"},{"screenshot","保存程序窗口截图","path"},{"metrics","保存接收统计 JSON","path"},{"ui-checks","验证叠加、折叠面板、表单及全屏"},{"ui-evidence","界面检查截图的路径前缀，需同时使用 --ui-checks","prefix"}});
    p.process(app);
    QString ffmpeg=p.value("ffmpeg");
    if(!p.isSet("ffmpeg") && QFile::exists(QCoreApplication::applicationDirPath()+"/ffmpeg.exe"))ffmpeg=QCoreApplication::applicationDirPath()+"/ffmpeg.exe";
    MainWindow window(ffmpeg);window.show();
    if(p.isSet("connect"))QTimer::singleShot(0,&window,&MainWindow::startConnection);
    bool uiOkay=true;
    if(p.isSet("ui-checks"))QTimer::singleShot(3000,&window,[&]{uiOkay=window.runUiChecks(p.value("ui-evidence"));});
    const int seconds=p.value("smoke-seconds").toInt();
    if(seconds>0)QTimer::singleShot(seconds*1000,&window,[&]{
        auto data=window.metrics();bool okay=uiOkay&&data["messages"].toInt()>=5&&data["decoded_frames"].toInt()>=5&&!data["data_stale"].toBool()&&!data["video_stale"].toBool();
        if(p.isSet("ui-checks"))data["ui_checks_passed"]=uiOkay;
        if(p.isSet("screenshot"))okay=window.saveEvidence(p.value("screenshot"))&&okay;
        if(p.isSet("metrics")){QFile f(p.value("metrics"));if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(data).toJson());else okay=false;}
        app.exit(okay?0:2);
    });
    return app.exec();
}
