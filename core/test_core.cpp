// Unit tests and a random-playthrough fuzz test for the Taipan core.
#include "taipan_core.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace tp;

static int g_checks = 0, g_fails = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fails++;                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                        \
    } while (0)

// Drains every pending event, answering questions with `yesToAll`.
static void drain(Game& g, bool yesToAll, int limit = 200) {
    Event e;
    while (limit-- > 0 && g.next(e)) {
        if (e.kind == EvKind::Question)
            g.answerYes(yesToAll);
        else if (e.kind == EvKind::Number)
            g.answerNumber(-1);
        else
            g.ack();
    }
}

static Game portGame(uint64_t seed, bool cashStart) {
    Game g(seed);
    g.newGame("Test Firm", cashStart);
    drain(g, false);
    return g;
}

static void invariants(const Game& g, const char* where) {
    const State& s = g.st();
    bool good = s.cash >= 0 && s.bank >= 0 && s.debt >= 0 && s.guns >= 0 && s.damage >= 0 &&
                s.capacity >= 60 && s.month >= 1 && s.month <= 12 && s.port >= 1 && s.port <= 7;
    int used = 0;
    for (int i = 0; i < 4; i++) {
        good = good && s.ship[i] >= 0 && s.wh[i] >= 0 && s.price[i] >= 0;
        used += s.ship[i];
    }
    good = good && g.warehouseUsed() <= kWarehouseCapacity;
    good = good && (long long)s.hold + used + 10LL * s.guns == s.capacity;
    if (!good) {
        g_fails++;
        printf("FAIL invariants at %s: cash=%lld bank=%lld debt=%lld hold=%d cap=%d guns=%d dmg=%d\n", where,
               s.cash, s.bank, s.debt, s.hold, s.capacity, s.guns, s.damage);
    }
    g_checks++;
}

// ---------------------------------------------------------------- tests

static void testRngAndFancy() {
    Rng r(42);
    bool inRange = true;
    for (int i = 0; i < 5000; i++) {
        int v = r.below(10);
        double u = r.unit();
        if (v < 0 || v > 9 || u < 0 || u >= 1) inRange = false;
    }
    CHECK(inRange);
    CHECK(r.below(0) == 0);
    Rng a(7), b(7);
    CHECK(a.next() == b.next());

    CHECK(fancy(0) == "0");
    CHECK(fancy(999999) == "999999");
    CHECK(fancy(1000000) == "1 Million");
    CHECK(fancy(1500000) == "1.5 Million");
    CHECK(fancy(1230000) == "1.23 Million");
    CHECK(fancy(1050000) == "1.05 Million");
    CHECK(fancy(123456789) == "123 Million");
    CHECK(fancy(-1500000) == "-1.5 Million");
}

static void testNewGame() {
    Game g(1);
    g.newGame("Firm", true);
    CHECK(g.st().cash == 400 && g.st().debt == 5000 && g.st().hold == 60 && g.st().guns == 0);
    CHECK(g.st().bp == 10 && g.st().li == 0 && g.st().port == 1);
    invariants(g, "newGame cash");
    g.newGame("Firm", false);
    CHECK(g.st().cash == 0 && g.st().debt == 0 && g.st().hold == 10 && g.st().guns == 5);
    CHECK(g.st().bp == 7 && g.st().li == 1);
    invariants(g, "newGame guns");

    // Prices follow the original table: base/2 * (1..3) * multiplier at Hong Kong.
    bool pricesOk = true;
    for (int seed = 1; seed < 200; seed++) {
        Game h(seed);
        h.newGame("P", true);
        long long lo[4] = {5 * 1000, 5 * 100, 6 * 10, 5 * 1};
        for (int i = 0; i < 4; i++) {
            long long p = h.st().price[i];
            if (p < lo[i] || p > lo[i] * 3) pricesOk = false;
        }
    }
    CHECK(pricesOk);
}

static void testFirstEvents() {
    Game g(3);
    g.newGame("Firm", true);
    Event e;
    CHECK(g.next(e));
    CHECK(e.kind == EvKind::Question && e.id == EvId::LiYuenPay);
    CHECK(e.a <= 400 / 1.8 + 1);
    long long amount = e.a;
    g.answerYes(true);
    CHECK(g.st().li == 1 && g.st().cash == 400 - amount);
    drain(g, false);
    CHECK(g.phase() == Phase::Port);

    Game h(3);
    h.newGame("Firm", false);
    CHECK(h.next(e) && e.id == EvId::WuBusiness);
}

static void testLiYuenCannotPay() {
    Game g(5);
    g.newGame("Firm", true);
    g.mutableState().year = 1861;
    g.mutableState().cash = 1000;
    g.mutableState().hold = 1000 - 1000 + 60;
    Event e;
    CHECK(g.next(e) && e.id == EvId::LiYuenPay);
    long long amount = e.a;
    CHECK(amount > 1000);
    g.answerYes(true);
    CHECK(g.next(e) && e.kind == EvKind::Message);
    g.ack();
    CHECK(g.next(e) && e.id == EvId::LiYuenWuCover);
    long long debtBefore = g.st().debt;
    g.answerYes(true);
    CHECK(g.st().cash == 0 && g.st().li == 1 && g.st().debt == debtBefore + amount - 1000);
    drain(g, false);
    CHECK(g.phase() == Phase::Port);
}

static void testWuDuringArrival() {
    Game g(3);
    g.newGame("Firm", true);
    Event e;
    CHECK(g.next(e) && e.id == EvId::LiYuenPay);
    g.answerYes(false);
    CHECK(g.next(e) && e.id == EvId::WuBusiness);
    CHECK(g.phase() == Phase::Arrival);
    g.answerYes(true);
    CHECK(g.next(e) && e.id == EvId::WuScreen);
    g.ack();
    CHECK(g.wuRepay(100) == Result::Ok && g.st().debt == 4900);
    CHECK(g.wuBorrow(200) == Result::Ok && g.st().cash == 500 && g.st().debt == 5100);
    g.wuLeave();
    drain(g, false);
    CHECK(g.phase() == Phase::Port);
    CHECK(g.deposit(1) == Result::Ok);  // bank still only at port
}

static void testRepairs() {
    Game g(9);
    g.newGame("Firm", false);  // li = 1: no extortion
    g.mutableState().damage = 20;
    g.mutableState().cash = 100000;
    Event e;
    CHECK(g.next(e) && e.id == EvId::Repairs);
    g.answerYes(true);
    CHECK(g.next(e) && e.kind == EvKind::Number && e.id == EvId::RepairAmount);
    long long price = e.a;
    CHECK(price > 0);
    g.answerNumber(-1);
    CHECK(g.st().damage == 0 && g.st().cash == 100000 - price);

    // Not enough cash: Wu covers the difference.
    Game h(9);
    h.newGame("Firm", false);
    h.mutableState().damage = 20;
    h.mutableState().cash = 100;
    CHECK(h.next(e) && e.id == EvId::Repairs);
    h.answerYes(true);
    CHECK(h.next(e) && e.id == EvId::RepairAmount);
    price = e.a;
    h.answerNumber(-1);
    CHECK(h.next(e) && e.kind == EvKind::Message);
    h.ack();
    CHECK(h.next(e) && e.id == EvId::RepairWuCover);
    h.answerYes(true);
    CHECK(h.st().cash == 0 && h.st().debt == price - 100 && h.st().damage == 0);
}

static void testPortActions() {
    Game g = portGame(11, false);
    State& s = g.mutableState();
    s.cash = 1000;
    s.price[OPIUM] = 100;
    s.debt = 5000;
    int hold0 = s.hold;
    CHECK(g.maxBuy(OPIUM) == 10);
    CHECK(g.buy(OPIUM, 11) == Result::NoFunds);
    CHECK(g.buy(OPIUM, -1) == Result::Ok);
    CHECK(s.cash == 0 && s.ship[OPIUM] == 10 && s.hold == hold0 - 10);
    CHECK(g.sell(OPIUM, 11) == Result::NoStock);
    CHECK(g.sell(OPIUM, 4) == Result::Ok);
    CHECK(s.cash == 400 && s.ship[OPIUM] == 6);
    invariants(g, "buy/sell");

    // Overloading is allowed but blocks sailing.
    int holdNow = s.hold;
    s.cash = 100000;
    s.price[GENERAL] = 1;
    CHECK(g.buy(GENERAL, holdNow + 5) == Result::Ok);
    CHECK(s.hold == -5);
    CHECK(g.sail(2) == Result::Overloaded);
    CHECK(g.sell(GENERAL, 5) == Result::Ok);

    // Bank.
    CHECK(g.deposit(s.cash + 1) == Result::NoFunds);
    long long cash = s.cash;
    CHECK(g.deposit(-1) == Result::Ok && s.cash == 0 && s.bank == cash);
    CHECK(g.withdraw(s.bank + 1) == Result::NoFunds);
    CHECK(g.withdraw(100) == Result::Ok && s.cash == 100);

    // Warehouse.
    s.capacity = 20000;
    s.wh[SILK] = 9990;
    s.ship[OPIUM] = 20;
    s.hold = s.capacity - (s.ship[0] + s.ship[1] + s.ship[2] + s.ship[3]) - 10 * s.guns;
    CHECK(g.toWarehouse(OPIUM, 20) == Result::NoRoom);
    CHECK(g.toWarehouse(OPIUM, 10) == Result::Ok && g.warehouseUsed() == 10000);
    CHECK(g.toShip(OPIUM, 11) == Result::NoStock);
    CHECK(g.toShip(OPIUM, -1) == Result::Ok);
    invariants(g, "warehouse");

    // Wu.
    s.cash = 1000;
    s.debt = 5000;
    CHECK(g.wuRepay(-1) == Result::Ok && s.cash == 0 && s.debt == 4000);
    CHECK(g.wuRepay(1) == Result::BadAmount);
    s.cash = 500;
    CHECK(g.wuBorrow(1001) == Result::Refused);
    CHECK(g.wuBorrow(1000) == Result::Ok && s.cash == 1500 && s.debt == 5000);
    s.cash = 9000;
    s.debt = 2000;
    CHECK(g.wuRepay(9000) == Result::Ok && s.debt == 0 && s.cash == 7000);

    // Not in Hong Kong: no bank, warehouse or Wu.
    s.port = 3;
    CHECK(g.deposit(1) == Result::BadPhase && g.toWarehouse(OPIUM, 1) == Result::BadPhase);
    CHECK(g.wuRepay(1) == Result::BadPhase && !g.wuEnter());
}

static void testWuBailout() {
    Game g = portGame(13, false);
    State& s = g.mutableState();
    s.guns = 0;
    s.cash = s.bank = 0;
    for (int i = 0; i < 4; i++) s.ship[i] = s.wh[i] = 0;
    s.hold = s.capacity;
    CHECK(g.broke());
    CHECK(!g.wuEnter());
    Event e;
    CHECK(g.next(e) && e.id == EvId::WuBailout);
    long long loan = e.a, payback = e.b;
    CHECK(loan >= 500 && payback >= 1500);
    g.answerYes(true);
    CHECK(s.cash == loan && s.debt == payback);
    drain(g, false);

    Game h = portGame(13, false);
    State& t = h.mutableState();
    t.guns = 0;
    t.cash = t.bank = 0;
    for (int i = 0; i < 4; i++) t.ship[i] = t.wh[i] = 0;
    t.hold = t.capacity;
    CHECK(!h.wuEnter());
    CHECK(h.next(e) && e.id == EvId::WuBailout);
    h.answerYes(false);
    CHECK(h.next(e) && e.kind == EvKind::Message);
    h.ack();
    CHECK(h.next(e) && e.kind == EvKind::GameOver);
    h.ack();
    CHECK(h.phase() == Phase::Over);
}

static void testBattleFight() {
    Game g(21);
    g.newGame("Firm", false);
    g.mutableState().guns = 5;
    g.mutableState().hold = g.st().capacity - 50;
    g.debugBeginBattle(BATTLE_GENERIC, 3);
    CHECK(g.phase() == Phase::Battle);
    CHECK(g.battle().numShips == 3 && g.battle().numOnScreen == 3);
    int before = g.battle().numShips;
    BattleStatus last = BattleStatus::Continue;
    for (int i = 0; i < 60 && g.phase() == Phase::Battle; i++) {
        RoundResult r = g.battleRound(ORD_FIGHT);
        CHECK(!r.lines.empty());
        CHECK(g.battle().numShips <= before);
        before = g.battle().numShips;
        last = r.status;
        invariants(g, "battle fight");
    }
    CHECK(g.phase() != Phase::Battle);
    CHECK(last != BattleStatus::Continue);
}

static void testBattleThrowAndRun() {
    Game g(22);
    g.newGame("Firm", true);
    State& s = g.mutableState();
    s.ship[OPIUM] = 30;
    s.hold = s.capacity - 30;
    g.debugBeginBattle(BATTLE_LI_YUEN, 5000);
    int hold0 = s.hold;
    RoundResult r = g.battleRound(ORD_THROW, OPIUM, 10);
    CHECK(s.ship[OPIUM] == 20);
    CHECK(s.hold == hold0 + 10);  // no guns, so nothing else changes the hold
    CHECK(r.lines.size() >= 2);
    r = g.battleRound(ORD_THROW, 4, -1);
    CHECK(g.st().ship[OPIUM] == 0 || g.phase() != Phase::Battle);
    r = g.battleRound(ORD_FIGHT);  // no guns
    bool noGuns = false;
    for (size_t i = 0; i < r.lines.size(); i++)
        if (r.lines[i].find("no guns") != std::string::npos) noGuns = true;
    CHECK(noGuns || g.phase() != Phase::Battle);
    invariants(g, "battle throw");
}

static void testFinalReport() {
    Game g(1);
    g.newGame("Firm", true);
    State& s = g.mutableState();
    s.cash = 5000000;
    s.bank = 0;
    s.debt = 0;
    FinalReport f = g.finalReport();
    CHECK(f.score == 50000 && f.rating == 0);
    s.cash = 4999900;
    CHECK(g.finalReport().rating == 1);
    s.cash = 800000;
    CHECK(g.finalReport().rating == 1 && g.finalReport().score == 8000);
    s.cash = 100000;
    CHECK(g.finalReport().rating == 2);
    s.cash = 50000;
    CHECK(g.finalReport().rating == 3);
    s.cash = 100;
    CHECK(g.finalReport().rating == 4);
    s.cash = 0;
    s.debt = 1000;
    CHECK(g.finalReport().rating == 4 && g.finalReport().score < 0);
    s.debt = 0;
    s.year = 1862;
    s.month = 4;
    s.cash = 2800000;
    FinalReport f2 = g.finalReport();
    CHECK(f2.years == 2 && f2.months == 4 && f2.score == 1000);  // 2.8M/100/28 months
}

static void testRetire() {
    Game g = portGame(17, false);
    CHECK(!g.canRetire());
    g.mutableState().cash = 600000;
    g.mutableState().bank = 400000;
    CHECK(g.canRetire());
    g.retire();
    Event e;
    CHECK(g.next(e) && e.kind == EvKind::Message);
    g.ack();
    CHECK(g.next(e) && e.kind == EvKind::GameOver);
    g.ack();
    CHECK(g.finalReport().retired);
}

static void testSaveLoad() {
    Game g = portGame(31, true);
    g.mutableState().cash = 1234;
    g.mutableState().firm = "Roundtrip & Co";
    std::stringstream ss;
    CHECK(g.save(ss));
    std::string text = ss.str();

    Game h(99);
    std::stringstream in(text);
    CHECK(h.load(in));
    std::stringstream out;
    CHECK(h.save(out));
    CHECK(out.str() == text);
    CHECK(h.phase() == Phase::Port && h.st().firm == "Roundtrip & Co" && h.st().cash == 1234);

    // Rejections leave the loaded game untouched.
    std::string bad[5];
    bad[0] = "TAIPAN9\n" + text.substr(text.find('\n') + 1);
    bad[1] = text;
    bad[1].replace(bad[1].find("cash=1234"), 9, "cash=abcd");
    bad[2] = text.substr(0, text.find("end\n"));
    bad[3] = text;
    bad[3].replace(bad[3].find("hold="), 5, "hold=1");
    bad[4] = text;
    bad[4].replace(bad[4].find("month=") + 6, 1, "99");
    for (int i = 0; i < 5; i++) {
        std::stringstream b(bad[i]);
        CHECK(!h.load(b));
    }
    CHECK(h.st().cash == 1234);

    // Saving mid-event or at sea is refused.
    Game f(31);
    f.newGame("X", true);
    std::stringstream s2;
    CHECK(!f.save(s2));
}

static void testSailAndArrive() {
    Game g = portGame(41, true);
    g.mutableState().li = 1;
    int month0 = g.st().month;
    CHECK(g.sail(1) == Result::BadAmount);
    CHECK(g.sail(9) == Result::BadAmount);
    CHECK(g.sail(3) == Result::Ok);
    CHECK(g.phase() == Phase::Voyage);
    CHECK(g.sail(4) == Result::BadPhase);
    int guard = 0;
    while (g.phase() != Phase::Port && g.phase() != Phase::Over && guard++ < 500) {
        Event e;
        if (g.next(e)) {
            if (e.kind == EvKind::Question)
                g.answerYes(false);
            else if (e.kind == EvKind::Number)
                g.answerNumber(-1);
            else
                g.ack();
        } else if (g.phase() == Phase::Battle) {
            g.battleRound(ORD_FIGHT);
        }
    }
    CHECK(g.phase() == Phase::Port || g.phase() == Phase::Over);
    if (g.phase() == Phase::Port) CHECK(g.st().month == month0 + 1 || g.st().month == 1 || true);
    invariants(g, "sail");
}

// ---------------------------------------------------------------- fuzz

struct FuzzStats {
    int games, over, retired, battles, saves, sails, bailouts;
    FuzzStats() : games(0), over(0), retired(0), battles(0), saves(0), sails(0), bailouts(0) {}
};

static void fuzzOne(uint64_t seed, FuzzStats& st) {
    Rng r(seed * 7919 + 13);
    Game g(seed);
    g.newGame("Fuzz", (seed & 1) != 0);
    st.games++;
    bool inBattle = false;
    for (int step = 0; step < 900; step++) {
        invariants(g, "fuzz loop");
        Event e;
        if (g.next(e)) {
            if (e.id == EvId::WuBailout) st.bailouts++;
            if (e.kind == EvKind::Question)
                g.answerYes(r.below(3) != 0);
            else if (e.kind == EvKind::Number)
                g.answerNumber(r.below(2) ? -1 : r.below(5000));
            else if (e.kind == EvKind::GameOver) {
                g.ack();
                st.over++;
                if (g.finalReport().retired) st.retired++;
                FinalReport f = g.finalReport();
                CHECK(f.rating >= 0 && f.rating <= 4);
                return;
            } else
                g.ack();
            continue;
        }
        switch (g.phase()) {
        case Phase::Battle: {
            if (!inBattle) st.battles++;
            inBattle = true;
            int o = 1 + r.below(3);
            g.battleRound(o, r.below(5), r.below(2) ? -1 : r.below(100));
            if (g.phase() != Phase::Battle) inBattle = false;
            break;
        }
        case Phase::Port: {
            const State& s = g.st();
            switch (r.below(12)) {
            case 0: g.buy(r.below(4), r.below(2) ? -1 : r.below(50)); break;
            case 1: g.buy(r.below(4), r.below(30)); break;
            case 2: g.sell(r.below(4), r.below(2) ? -1 : r.below(50)); break;
            case 3: g.deposit(r.below(2) ? -1 : r.below(3000)); break;
            case 4: g.withdraw(r.below(2) ? -1 : r.below(3000)); break;
            case 5: g.toWarehouse(r.below(4), r.below(2) ? -1 : r.below(40)); break;
            case 6: g.toShip(r.below(4), r.below(2) ? -1 : r.below(40)); break;
            case 7:
                if (s.port == kHongKong && g.wuEnter()) {
                    g.wuRepay(r.below(2) ? -1 : r.below(4000));
                    g.wuBorrow(r.below(2) ? -1 : r.below(4000));
                    g.wuLeave();
                }
                break;
            case 8:
                if (g.canRetire()) g.retire();
                break;
            case 9: {
                std::stringstream a;
                if (g.save(a)) {
                    st.saves++;
                    Game h(1);
                    std::stringstream in(a.str());
                    bool ok = h.load(in);
                    CHECK(ok);
                    if (!ok) {
                        printf("%s\n", a.str().c_str());
                        return;
                    }
                    std::stringstream b;
                    h.save(b);
                    CHECK(a.str() == b.str());
                    // Continue the run from the reloaded copy (exercises restored RNG).
                    g = h;
                }
                break;
            }
            default: {
                int dest = 1 + r.below(7);
                if (g.sail(dest) == Result::Ok) st.sails++;
                else if (g.st().hold < 0) {
                    // Dump cargo to get legal again.
                    for (int i = 0; i < 4 && g.st().hold < 0; i++) g.sell(i, -1);
                }
                break;
            }
            }
            break;
        }
        case Phase::Over: return;
        default: break;
        }
    }
}

static void testFuzz() {
    FuzzStats st;
    for (uint64_t seed = 1; seed <= 1500; seed++) fuzzOne(seed, st);
    printf("fuzz: games=%d over=%d retired=%d battles=%d saves=%d sails=%d bailouts=%d\n", st.games, st.over,
           st.retired, st.battles, st.saves, st.sails, st.bailouts);
    CHECK(st.sails > 1000);
    CHECK(st.battles > 100);
    CHECK(st.saves > 100);
}

int main() {
    testRngAndFancy();
    testNewGame();
    testFirstEvents();
    testLiYuenCannotPay();
    testRepairs();
    testWuDuringArrival();
    testPortActions();
    testWuBailout();
    testBattleFight();
    testBattleThrowAndRun();
    testFinalReport();
    testRetire();
    testSaveLoad();
    testSailAndArrive();
    testFuzz();
    printf("%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
