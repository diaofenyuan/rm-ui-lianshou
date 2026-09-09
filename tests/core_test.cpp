#include "assembler.h"
#include "status.h"
#include <QtTest>
#include <QtEndian>

static QByteArray packet(quint16 frame,quint16 index,quint32 size,const QByteArray &data) {
    QByteArray p(8,'\0');auto *b=reinterpret_cast<uchar *>(p.data());
    qToBigEndian(frame,b);qToBigEndian(index,b+2);qToBigEndian(size,b+4);return p+data;
}
class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void protocolFixture() {
        // 固定 wire 样本独立于模拟发布器，防止收发两端一起写错字段编号。
        rm::GameStatus s;QVERIFY(s.ParseFromString(QByteArray::fromHex("0802100318012000280430ac02383c400048ff0150ff01").toStdString()));
        QCOMPARE(s.current_round(),2U);QCOMPARE(s.total_rounds(),3U);QCOMPARE(s.red_score(),1U);
        QVERIFY(s.has_blue_score());QCOMPARE(s.blue_score(),0U);QCOMPARE(s.current_stage(),4U);
        QCOMPARE(s.stage_countdown_sec(),300);QCOMPARE(s.stage_elapsed_sec(),60);QVERIFY(s.has_is_paused());QVERIFY(!s.is_paused());
        QCOMPARE(s.game_result(),255U);QCOMPARE(s.end_reason(),255U);
    }
    void missingAndUnknown() {
        rm::GameStatus s;QVERIFY(s.ParseFromString(""));QVERIFY(status::json(s)["red_score"].isNull());
        QVERIFY(s.ParseFromString(QByteArray::fromHex("28635009a00601").toStdString()));
        QVERIFY(status::stage(s.current_stage()).contains("99"));QCOMPARE(s.end_reason(),9U);
        QVERIFY(!s.ParseFromString(QByteArray::fromHex("3080").toStdString()));
    }
    void signedTime() {
        rm::GameStatus s;QVERIFY(s.ParseFromString(QByteArray::fromHex("30ffffffffffffffffff01").toStdString()));
        QCOMPARE(s.stage_countdown_sec(),-1);QCOMPARE(status::duration(-1),QString("-00:01"));
        QCOMPARE(status::duration(INT32_MIN),QString("-35791394:08"));
    }
    void reorderAndDuplicate() {
        FrameAssembler a;QByteArray p(1392,'a');QByteArray tail(10,'b');
        QVERIFY(!a.push(packet(20,1,1402,tail),0));QVERIFY(!a.push(packet(20,1,1402,tail),1));
        auto f=a.push(packet(20,0,1402,p),2);QVERIFY(f.has_value());QCOMPARE(*f,p+tail);
        QVERIFY(!a.push(packet(20,0,1402,p),3));QCOMPARE(a.pending(),0);
    }
    void lossTimeoutAndRecovery() {
        FrameAssembler a;QVERIFY(!a.push(packet(20,0,1402,QByteArray(1392,'a')),0));
        a.expire(151);QCOMPARE(a.pending(),0);QCOMPARE(a.dropped,1ULL);
        QVERIFY(a.push(packet(21,0,3,"abc"),160));
        QVERIFY(!a.push(packet(19,0,3,"old"),170));
        QVERIFY(a.push(packet(0,0,3,"new"),1300));
    }
    void frameWrap() {
        FrameAssembler a;QVERIFY(a.push(packet(65535,0,3,"abc"),0));QVERIFY(a.push(packet(0,0,3,"def"),10));
    }
    void invalidPacketsAndBounds() {
        FrameAssembler a;QVERIFY(!a.push("bad",0));QVERIFY(!a.push(packet(1,0,0,"a"),0));
        QVERIFY(!a.push(packet(1,0,8*1024*1024,"a"),0));QVERIFY(!a.push(packet(1,2,4,"abcd"),0));
        QVERIFY(!a.push(packet(1,0,1402,"short"),0));QCOMPARE(a.invalid,5ULL);
        for(int i=0;i<20;++i)a.push(packet(i,0,1402,QByteArray(1392,'a')),0);
        QVERIFY(a.pending()<=8);
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
