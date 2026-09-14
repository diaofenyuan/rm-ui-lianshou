#include "assembler.h"
#include "status.h"
#include "operator_profile.h"
#include <QtTest>
#include <QtEndian>

static QByteArray packet(quint16 frame,quint16 index,quint32 size,const QByteArray &data) {
    QByteArray p(8,'\0');auto *b=reinterpret_cast<uchar *>(p.data());
    qToBigEndian(frame,b);qToBigEndian(index,b+2);qToBigEndian(size,b+4);return p+data;
}
class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void operatorProfiles() {
        QCOMPARE(profile::robotId(false, 3), 3);
        QCOMPARE(profile::robotId(true, 4), 104);
        QCOMPARE(profile::name(101), QString("蓝方 · 1 号英雄"));
        QCOMPARE(profile::name(5), QString("红方 · 5 号步兵"));
        QVERIFY(!profile::valid(0)); QVERIFY(!profile::valid(100)); QVERIFY(!profile::valid(110));
        QCOMPARE(profile::robotId(false, 10), 0);
        for (const auto &role : profile::roles) {
            QVERIFY(profile::valid(profile::robotId(false, role.number)));
            QVERIFY(profile::valid(profile::robotId(true, role.number)));
        }
    }
    void meaningfulMatchChanges() {
        rm::GameStatus before, after;
        before.set_current_round(1); before.set_current_stage(4); before.set_red_score(0);
        before.set_is_paused(false); before.set_game_result(255);
        after = before; after.set_stage_countdown_sec(60);
        QVERIFY(status::changes(before, after).isEmpty());
        after.set_is_paused(true); after.set_red_score(1);
        QCOMPARE(status::changes(before, after), QStringList({"比赛暂停", "红方得分：0 → 1"}));
        after.Clear(); QVERIFY(status::changes(before, after).isEmpty());
        after = before; after.set_current_round(2); after.set_red_score(1);
        QVERIFY(!status::changes(before, after).join(" ").contains("红方得分"));
        after = before; after.set_game_result(2);
        QVERIFY(status::changes(before, after).isEmpty());
        after.set_current_stage(5);
        QVERIFY(status::changes(before, after).contains("比赛结算：蓝方胜利"));
        QVERIFY(status::changes(after, after).isEmpty());
        after.set_end_reason(9);
        QVERIFY(status::changes(before, after).contains("结束原因：主裁判手动判定胜负"));
    }
    void gameStatusJson() {
        rm::GameStatus s;
        s.set_current_round(2); s.set_total_rounds(3); s.set_red_score(1); s.set_blue_score(0);
        s.set_current_stage(5); s.set_stage_countdown_sec(0); s.set_stage_elapsed_sec(12);
        s.set_is_paused(false); s.set_game_result(2); s.set_end_reason(9);
        const auto o = status::json(s);
        QCOMPARE(o["message_type"].toString(), QString("GameStatus"));
        QCOMPARE(o["current_stage_name"].toString(), QString("比赛结算中"));
        QCOMPARE(o["stage_countdown_text"].toString(), QString("00:00"));
        QCOMPARE(o["game_result_name"].toString(), QString("蓝方胜利"));
        QCOMPARE(o["end_reason_name"].toString(), QString("主裁判手动判定胜负"));
        QVERIFY(o["warnings"].toArray().isEmpty());
        s.set_current_stage(4);
        QVERIFY(status::json(s)["game_result_name"].isNull());
        QVERIFY(status::json(s)["end_reason_name"].isNull());
        QVERIFY(!status::warnings(s).isEmpty());
    }
    void gameStatusWarningsPreserveRawValues() {
        rm::GameStatus s; s.set_current_stage(99); s.set_game_result(7); s.set_end_reason(0);
        const auto o = status::json(s);
        QCOMPARE(o["current_stage"].toInt(), 99);
        QCOMPARE(o["game_result"].toInt(), 7);
        QVERIFY(o["warnings"].toArray().size() >= 3);
    }
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
    void oneBasedSlices() {
        FrameAssembler a; const QByteArray first(1392, 'x'), tail(10, 'y');
        // 1 起始分片故意乱序，验证尾片长度能锁定编号基数。
        QVERIFY(!a.push(packet(30, 2, 1402, tail), 0));
        auto f = a.push(packet(30, 1, 1402, first), 1);
        QVERIFY(f.has_value()); QCOMPARE(*f, first + tail);
        QCOMPARE(a.lastSliceBase(), 1); QCOMPARE(a.oneBasedFrames, 1ULL);
        // 恰好整除净载荷大小时，前两个包都可能暂时有歧义，第三包仍应正确收敛。
        const QByteArray full(1392, 'z'); FrameAssembler b;
        QVERIFY(!b.push(packet(31, 1, 2784, full), 0));
        auto g = b.push(packet(31, 2, 2784, full), 1);
        QVERIFY(g.has_value()); QCOMPARE(*g, full + full);
        QCOMPARE(b.lastSliceBase(), 1);
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
