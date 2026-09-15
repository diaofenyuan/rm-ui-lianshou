#include "assembler.h"
#include "match_state.h"
#include "map_transform.h"
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
    void consoleSemantics() {
        QCOMPARE(status::robotType(1), QString("1号英雄"));
        QCOMPARE(status::robotType(4), QString("4号步兵"));
        QVERIFY(status::robotType(99).contains("99"));
        QCOMPARE(status::connectionState(1), QString("已连接"));
        QCOMPARE(status::fieldState(1), QString("未上场"));
        QCOMPARE(status::aliveState(2), QString("战亡"));
        QCOMPARE(status::baseStatus(2), QString("解除无敌，护甲展开"));
        QCOMPARE(status::outpostStatus(5), QString("被击毁，重建中"));
        QCOMPARE(status::penaltyType(4), QString("超功率"));
        QCOMPARE(status::buffType(2), QString("防御增益/易伤"));
        rm::Event kill; kill.set_event_id(1); kill.set_param("1,101");
        QCOMPARE(status::eventText(kill), QString("击杀事件：红方 · 1 号英雄 被 蓝方 · 1 号英雄 击毁"));
        rm::Event outpost; outpost.set_event_id(2); outpost.set_param("111");
        QCOMPARE(status::eventText(outpost), QString("蓝方前哨站被摧毁"));
        rm::Event dart; dart.set_event_id(9); dart.set_param("2,4");
        QVERIFY(status::eventText(dart).contains("蓝方"));
        QVERIFY(status::eventText(dart).contains("基地随机移动目标"));
        rm::Event assemble; assemble.set_event_id(15); assemble.set_param("2");
        QCOMPARE(status::eventText(assemble), QString("装配结果：装配超时"));
        rm::Event unknown; unknown.set_event_id(99); unknown.set_param("x");
        QVERIFY(status::eventText(unknown).contains("99"));
    }
    void consoleDomainJson() {
        rm::GlobalUnitStatus u;
        u.set_base_health(5000); u.set_base_status(1); u.set_outpost_status(3);
        u.set_total_damage_ally(1200);
        for (int i = 0; i < 10; ++i) u.add_robot_health(quint32(100 + i));
        const auto o = status::json(u);
        QCOMPARE(o["message_type"].toString(), QString("GlobalUnitStatus"));
        QCOMPARE(o["base_status_name"].toString(), QString("解除无敌，护甲未展开"));
        QCOMPARE(o["outpost_status_name"].toString(), QString("被击毁，不可重建"));
        QCOMPARE(o["robot_health"].toArray().size(), 10);
        QCOMPARE(o["total_damage_ally"].toInt(), 1200);
        rm::GlobalUnitStatus empty;
        QVERIFY(status::json(empty)["base_health"].isNull());
        QVERIFY(status::json(empty)["enemy_base_status_name"].isNull());
        rm::RobotStaticStatus st;
        st.set_robot_id(104); st.set_robot_type(4); st.set_level(2); st.set_max_health(400);
        st.set_heat_cooldown_rate(10.0f);
        const auto so = status::json(st);
        QCOMPARE(so["message_type"].toString(), QString("RobotStaticStatus"));
        QCOMPARE(so["robot_type_name"].toString(), QString("4号步兵"));
        QCOMPARE(so["level"].toInt(), 2);
        QVERIFY(so["connection_state_name"].isNull());
        rm::RobotDynamicStatus dy;
        dy.set_current_health(300); dy.set_current_heat(55.5f); dy.set_remaining_ammo(600);
        dy.set_is_out_of_combat(true);
        const auto dyo = status::json(dy);
        QCOMPARE(dyo["current_health"].toInt(), 300);
        QCOMPARE(dyo["current_heat"].toDouble(), 55.5);
        QCOMPARE(dyo["is_out_of_combat"].toBool(), true);
        rm::RobotRespawnStatus rs;
        rs.set_is_pending_respawn(true); rs.set_total_respawn_progress(33); rs.set_current_respawn_progress(12);
        rs.set_gold_cost_for_respawn(440);
        QCOMPARE(status::json(rs)["progress_text"].toString(), QString("12/33"));
        rm::Event kill; kill.set_event_id(1); kill.set_param("1,101");
        QCOMPARE(status::json(kill)["message_type"].toString(), QString("Event"));
        QVERIFY(status::json(kill)["event_text"].toString().contains("击毁"));
    }
    void mapTransform() {
        using namespace maptf;
        QCOMPARE(normalize(0.0, 0.0), QPointF(0, 0));
        QCOMPARE(normalize(kFieldLengthM, kFieldWidthM), QPointF(1, 1));
        QCOMPARE(normalize(14.0, 7.5), QPointF(0.5, 0.5));
        // 雷达按厘米下发，换算到同一归一化口径。
        QCOMPARE(normalizeRadar(1400, 750), QPointF(0.5, 0.5));
        QCOMPARE(normalizeRadar(2800, 1500), QPointF(1, 1));
        QCOMPARE(normalizeRadar(0, 0), QPointF(0, 0));

        QVERIFY(plausible(0.0, 0.0));
        QVERIFY(plausible(kFieldLengthM, kFieldWidthM));
        QVERIFY(plausible(-0.5, 7.0));
        QVERIFY(!plausible(-2.0, 7.0));
        QVERIFY(!plausible(30.0, 7.0));
        QVERIFY(!plausible(14.0, 17.0));
        QCOMPARE(clamped(QPointF(1.2, -0.3)), QPointF(1, 0));

        // 28:15 取景：先按宽度铺满，超出高度时改为按高度取景并水平居中。
        const QRectF wide = fieldRect(QRectF(0, 0, 2000, 300));
        QVERIFY(qAbs(wide.height() - 300) < 0.01);
        QVERIFY(qAbs(wide.width() - 300 * 28.0 / 15.0) < 0.01);
        QVERIFY(qAbs(wide.x() - (2000 - wide.width()) / 2) < 0.01);
        const QRectF tall = fieldRect(QRectF(0, 0, 1000, 500));
        QVERIFY(qAbs(tall.width() - 500 * 28.0 / 15.0) < 0.01);
        QVERIFY(qAbs(tall.height() - 500) < 0.01);
        QVERIFY(qAbs(tall.y()) < 0.01);
        QVERIFY(fieldRect(QRectF()).isEmpty());

        const QRectF field(0, 0, 280, 150);
        QCOMPARE(toPixels(QPointF(0.5, 0.5), field), QPointF(140, 75));
        QCOMPARE(toPixels(QPointF(0, 0), field), QPointF(0, 0));

        // 蓝方视角为 180° 旋转，红方视角保持世界坐标。
        QCOMPARE(forView(QPointF(0.25, 0.75), false), QPointF(0.25, 0.75));
        QCOMPARE(forView(QPointF(0.25, 0.75), true), QPointF(0.75, 0.25));
        // 正北（+Y）在画布上向下，正东（+X）向右；蓝方视角整体再旋转 180°。
        QCOMPARE(yawToCanvasDegrees(0, false), 90.0);
        QCOMPARE(yawToCanvasDegrees(90, false), 0.0);
        QCOMPARE(yawToCanvasDegrees(180, false), 270.0);
        QCOMPARE(yawToCanvasDegrees(270, false), 180.0);
        QCOMPARE(yawToCanvasDegrees(0, true), 270.0);
        QCOMPARE(yawToCanvasDegrees(90, true), 180.0);
        QCOMPARE(yawToCanvasDegrees(360, false), 90.0);
    }
    void matchStateAggregates() {
        MatchState m;
        QVERIFY(m.isStale(MatchState::Domain::UnitStatus));
        rm::GlobalUnitStatus u;
        for (int i = 0; i < 10; ++i) u.add_robot_health(quint32(100 + i));
        m.applyUnitStatus(u);
        // 协议 2.2.4 固定顺序：索引 0–4 己方 1/2/3/4/7 号，5–9 对方。
        QCOMPARE(m.allyHealth(0).value_or(0), quint32(105));
        QCOMPARE(m.allyHealth(4).value_or(0), quint32(109));
        QCOMPARE(m.enemyHealth(0).value_or(0), quint32(100));
        QCOMPARE(m.enemyHealth(4).value_or(0), quint32(104));
        QVERIFY(m.allyHealth(5) == std::nullopt);
        QVERIFY(m.enemyHealth(-1) == std::nullopt);
        QVERIFY(!m.isStale(MatchState::Domain::UnitStatus));
        rm::RadarInfoToClient r;
        for (int i = 0; i < 12; ++i) r.add_robot_info()->set_target_pos_x(quint32(i));
        m.applyRadar(r);
        // 协议 2.2.19 固定顺序：索引 0–5 对方，6–11 己方。
        QCOMPARE(m.enemyRadar(0)->target_pos_x(), quint32(0));
        QCOMPARE(m.allyRadar(0)->target_pos_x(), quint32(6));
        QCOMPARE(m.allyRadar(5)->target_pos_x(), quint32(11));
        QVERIFY(m.allyRadar(6) == std::nullopt);
        rm::Event e; e.set_event_id(11);
        m.applyEvent(e);
        QCOMPARE(m.events().size(), 1);
        QCOMPARE(m.events().constLast().event.event_id(), 11);
        rm::Buff first; first.set_robot_id(104); first.set_buff_type(1); first.set_buff_left_time(10);
        m.applyBuff(first);
        rm::Buff updated; updated.set_robot_id(104); updated.set_buff_type(1); updated.set_buff_left_time(3);
        m.applyBuff(updated);
        QCOMPARE(m.buffs().size(), 1);
        QCOMPARE(m.buffs().constFirst().buff.buff_left_time(), quint32(3));
        rm::Buff other; other.set_robot_id(104); other.set_buff_type(5); other.set_buff_left_time(1);
        m.applyBuff(other);
        QCOMPARE(m.buffs().size(), 2);
        rm::Buff expired; expired.set_robot_id(104); expired.set_buff_type(1); expired.set_buff_left_time(0);
        m.applyBuff(expired);
        QCOMPARE(m.buffs().size(), 1);
        QCOMPARE(m.buffs().constFirst().buff.buff_type(), quint32(5));
        m.reset();
        QVERIFY(m.events().isEmpty()); QVERIFY(m.buffs().isEmpty());
        QVERIFY(!m.allyHealth(0).has_value());
        QVERIFY(m.isStale(MatchState::Domain::UnitStatus));
        MatchState capped;
        for (int i = 0; i < 210; ++i) { rm::Event flood; flood.set_event_id(i % 16); capped.applyEvent(flood); }
        QCOMPARE(capped.events().size(), 200);
    }
    void matchStateStaleness() {
        MatchState m;
        rm::GameStatus g; g.set_current_stage(4);
        m.applyGame(g);
        rm::RobotStaticStatus s; s.set_robot_id(104);
        m.applyStatic(s);
        QVERIFY(!m.isStale(MatchState::Domain::Game));
        QVERIFY(!m.isStale(MatchState::Domain::RobotStatic));
        QCOMPARE(m.ageMs(MatchState::Domain::Logistics), qint64(-1));
        QTest::qWait(1600);
        // 5Hz 域 1.5s 过期；1Hz 域阈值 3s，此时仍应实时。
        QVERIFY(m.isStale(MatchState::Domain::Game));
        QVERIFY(!m.isStale(MatchState::Domain::RobotStatic));
        QTest::qWait(1600);
        QVERIFY(m.isStale(MatchState::Domain::RobotStatic));
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
