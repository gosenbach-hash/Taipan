#include "taipan_core.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <map>
#include <ostream>
#include <sstream>

namespace tp {

const char* const kItemName[4] = {"Opium", "Silk", "Arms", "General Cargo"};
const char* const kPortName[8] = {"At sea", "Hong Kong", "Shanghai", "Nagasaki",
                                  "Saigon", "Manila", "Singapore", "Batavia"};
const char* const kMonthName[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kStatusName[6] = {"Critical", "Poor", "Fair", "Good", "Prime", "Perfect"};
const char* const kRatingName[5] = {"Ma Tsu", "Master Taipan", "Taipan", "Compradore", "Galley Hand"};

static const int kBasePrice[4][8] = {{1000, 11, 16, 15, 14, 12, 10, 13},
                                     {100, 11, 14, 15, 16, 10, 13, 12},
                                     {10, 12, 16, 10, 11, 13, 14, 15},
                                     {1, 10, 11, 12, 13, 14, 15, 16}};

static const long long kMoneyCap = 1000000000000000LL;

static const char* const kComprador = "Comprador's Report";
static const char* const kCaptain = "Captain's Report";

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// ---------------------------------------------------------------- Rng

Rng::Rng(uint64_t seed) : s_(seed ? seed : 1) {
    for (int i = 0; i < 4; i++) next();  // scramble small seeds
}

uint64_t Rng::next() {
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return s_ * 2685821657736338717ULL;
}

int Rng::below(int n) {
    if (n <= 0) return 0;
    return (int)((next() >> 33) % (uint64_t)n);
}

double Rng::unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }

// ---------------------------------------------------------------- fancy

std::string fancy(long long n) {
    if (n < 0) return "-" + fancy(-n);
    if (n < 1000000) return fmt("%lld", n);
    if (n >= 100000000) return fmt("%lld Million", n / 1000000);
    long long hundredths = n / 10000;  // millions with two decimals
    long long whole = hundredths / 100, frac = hundredths % 100;
    if (frac == 0) return fmt("%lld Million", whole);
    if (frac % 10 == 0) return fmt("%lld.%lld Million", whole, frac / 10);
    return fmt("%lld.%02lld Million", whole, frac);
}

// ---------------------------------------------------------------- State / Battle

State::State()
    : cash(0), bank(0), debt(0), hold(0), capacity(60), guns(0), damage(0), month(1), year(1860),
      port(kHongKong), li(0), wuWarn(0), wuBailout(0), bp(10), ec(20), ed(0.5) {
    for (int i = 0; i < 4; i++) ship[i] = wh[i] = 0, price[i] = 0;
}

Battle::Battle()
    : kind(BATTLE_GENERIC), numShips(0), startShips(0), numOnScreen(0), orders(0), ok(0), ik(1), booty(0) {
    for (int i = 0; i < 10; i++) onScreen[i] = 0;
}

// ---------------------------------------------------------------- Game basics

Game::Game(uint64_t seed)
    : rng_(seed), phase_(Phase::Over), haveCur_(false), arrStep_(0), vstage_(V_START),
      result_(BattleStatus::Continue), resultSet_(false), pendBr_(0), retired_(false) {}

void Game::reseed(uint64_t seed) { rng_ = Rng(seed); }

int Game::timeMonths() const { return (s_.year - 1860) * 12 + s_.month; }

int Game::seaworthiness() const {
    if (s_.capacity <= 0) return 0;
    int pct = (int)(100.0 - ((double)s_.damage / s_.capacity) * 100.0);
    return pct < 0 ? 0 : pct;  // a ship can be hit past 0% and only sinks at the next battle round
}

int Game::warehouseUsed() const { return s_.wh[0] + s_.wh[1] + s_.wh[2] + s_.wh[3]; }

long long Game::maxBuy(int item) const {
    if (item < 0 || item > 3 || s_.price[item] <= 0) return 0;
    return s_.cash / s_.price[item];
}

bool Game::canRetire() const {
    return phase_ == Phase::Port && s_.port == kHongKong && s_.cash + s_.bank >= 1000000;
}

bool Game::broke() const {
    if (s_.cash != 0 || s_.bank != 0 || s_.guns != 0) return false;
    for (int i = 0; i < 4; i++)
        if (s_.ship[i] != 0 || s_.wh[i] != 0) return false;
    return true;
}

void Game::setPrices() {
    for (int i = 0; i < 4; i++)
        s_.price[i] = (long long)(kBasePrice[i][s_.port] / 2) * (rng_.below(3) + 1) * kBasePrice[i][0];
}

void Game::newGame(const std::string& firm, bool cashStart) {
    State n;
    n.firm = firm;
    if (cashStart) {
        n.cash = 400;
        n.debt = 5000;
        n.hold = 60;
        n.guns = 0;
        n.li = 0;
        n.bp = 10;
    } else {
        n.cash = 0;
        n.debt = 0;
        n.hold = 10;
        n.guns = 5;
        n.li = 1;
        n.bp = 7;
    }
    s_ = n;
    queue_.clear();
    haveCur_ = false;
    battle_ = Battle();
    pendBr_ = 0;
    retired_ = false;
    resultSet_ = false;
    setPrices();
    phase_ = Phase::Arrival;
    arrStep_ = 0;
}

Event Game::info(const char* title, const std::string& text) const {
    Event e;
    e.kind = EvKind::Message;
    e.id = EvId::Info;
    e.title = title;
    e.text = text;
    return e;
}

Event Game::question(EvId id, const char* title, const std::string& text, long long a, long long b) const {
    Event e;
    e.kind = EvKind::Question;
    e.id = id;
    e.title = title;
    e.text = text;
    e.a = a;
    e.b = b;
    return e;
}

void Game::push(const Event& e) { queue_.push_back(e); }

void Game::insertFront(const std::vector<Event>& v) {
    for (size_t i = v.size(); i > 0; i--) queue_.push_front(v[i - 1]);
}

void Game::gameOver(bool retired) {
    retired_ = retired;
    Event e;
    e.kind = EvKind::GameOver;
    e.id = EvId::Over;
    e.title = kComprador;
    e.text = retired ? "You retire a millionaire." : "Game over.";
    push(e);
    phase_ = Phase::Over;
}

// ---------------------------------------------------------------- event pump

bool Game::next(Event& e) {
    if (!haveCur_) {
        while (queue_.empty()) {
            if (phase_ == Phase::Arrival)
                stepArrival();
            else if (phase_ == Phase::Voyage)
                stepVoyage();
            else
                break;
        }
        if (queue_.empty()) return false;
        cur_ = queue_.front();
        queue_.pop_front();
        haveCur_ = true;
    }
    e = cur_;
    return true;
}

void Game::ack() {
    if (!haveCur_) return;
    if (cur_.kind == EvKind::Message || cur_.kind == EvKind::Action || cur_.kind == EvKind::GameOver)
        haveCur_ = false;
}

static const int kArrivalSteps = 11;

void Game::stepArrival() {
    if (arrStep_ >= kArrivalSteps) {
        phase_ = Phase::Port;
        return;
    }
    arrivalStep(arrStep_++);
}

void Game::arrivalStep(int step) {
    const bool hk = (s_.port == kHongKong);
    switch (step) {
    case 0:
        if (hk && s_.li == 0 && s_.cash > 0) beginLiYuenExtortion();
        break;
    case 1:
        if (hk && s_.damage > 0) offerRepairs();
        break;
    case 2:
        if (hk && s_.debt >= 10000 && s_.wuWarn == 0) {
            int braves = rng_.below(100) + 50;
            push(info(kComprador, fmt("Elder Brother Wu has sent %d braves\nto escort you to the Wu mansion, Taipan.", braves)));
            push(info(kComprador, "Elder Brother Wu reminds you of the\nConfucian ideal of personal worthiness,\nand how this applies to paying one's\ndebts."));
            push(info(kComprador, "He is reminded of a fabled barbarian\nwho came to a bad end, after not caring\nfor his obligations.\n\nHe hopes no such fate awaits you, his\nfriend, Taipan."));
            s_.wuWarn = 1;
        }
        break;
    case 3:
        if (hk)
            push(question(EvId::WuBusiness, kComprador, "Do you have business with Elder Brother\nWu, the moneylender?"));
        break;
    case 4:
        if (rng_.below(4) == 0) {
            if (rng_.below(2) == 0)
                offerNewShip();
            else if (s_.guns < 1000)
                offerNewGun();
        }
        break;
    case 5:
        if (!hk && rng_.below(18) == 0 && s_.ship[0] > 0) {
            long long fine = (long long)((s_.cash / 1.8) * rng_.unit()) + 1;
            if (s_.cash == 0) fine = 0;
            if (fine > s_.cash) fine = s_.cash;
            s_.hold += s_.ship[0];
            s_.ship[0] = 0;
            s_.cash -= fine;
            if (fine <= 0)
                push(info(kComprador, "Bad Joss!!\nThe local authorities have seized your\nOpium cargo, Taipan!"));
            else
                push(info(kComprador, fmt("Bad Joss!!\nThe local authorities have seized your\nOpium cargo and have also fined you\n%s, Taipan!", fancy(fine).c_str())));
        }
        break;
    case 6:
        if (rng_.below(50) == 0 && warehouseUsed() > 0) {
            for (int i = 0; i < 4; i++) s_.wh[i] = (int)((s_.wh[i] / 1.8) * rng_.unit());
            push(info(kComprador, "Messenger reports large theft\nfrom warehouse, Taipan."));
        }
        break;
    case 7:
        if (rng_.below(20) == 0) {
            if (s_.li > 0) s_.li++;
            if (s_.li == 4) s_.li = 0;
        }
        break;
    case 8:
        if (!hk && s_.li == 0 && rng_.below(4) != 0)
            push(info(kComprador, "Li Yuen has sent a Lieutenant,\nTaipan.  He says his admiral wishes\nto see you in Hong Kong, posthaste!"));
        break;
    case 9:
        if (rng_.below(9) == 0) {
            int i = rng_.below(4), j = rng_.below(2);
            if (j == 0) {
                s_.price[i] = s_.price[i] / 5;
                push(info(kComprador, fmt("Taipan!!  The price of %s\nhas dropped to %lld!!", kItemName[i], s_.price[i])));
            } else {
                s_.price[i] = s_.price[i] * (rng_.below(5) + 5);
                push(info(kComprador, fmt("Taipan!!  The price of %s\nhas risen to %lld!!", kItemName[i], s_.price[i])));
            }
        }
        break;
    case 10:
        if (s_.cash > 25000 && rng_.below(20) == 0) {
            long long robbed = (long long)((s_.cash / 1.4) * rng_.unit());
            s_.cash -= robbed;
            push(info(kComprador, fmt("Bad Joss!!\nYou've been beaten up and\nrobbed of %s in cash, Taipan!!", fancy(robbed).c_str())));
        }
        break;
    }
}

void Game::beginLiYuenExtortion() {
    int time = timeMonths();
    double i = 1.8, j = 0;
    if (time > 12) {
        j = rng_.below(1000 * time) + 1000 * time;
        i = 1;
    }
    long long amount = (long long)((s_.cash / i) * rng_.unit() + j);
    push(question(EvId::LiYuenPay, kComprador,
                  fmt("Li Yuen asks %s in donation\nto the temple of Tin Hau, the Sea\nGoddess.  Will you pay?", fancy(amount).c_str()),
                  amount));
}

void Game::offerRepairs() {
    push(question(EvId::Repairs, kComprador,
                  "Taipan, Mc Henry from the Hong Kong\nShipyards has arrived!!  He says, \"I see\nye've a wee bit of damage to yer ship.\nWill ye be wanting repairs?"));
}

void Game::offerNewShip() {
    int time = timeMonths();
    long long amount = (long long)rng_.below(1000 * (time + 5) / 6) * (s_.capacity / 50) + 1000;
    if (s_.cash < amount) return;
    push(question(EvId::NewShip, kComprador,
                  fmt("Do you wish to trade in your %s\nship for one with 50 more capacity by\npaying an additional %s, Taipan?",
                      s_.damage > 0 ? "damaged" : "fine", fancy(amount).c_str()),
                  amount));
}

void Game::offerNewGun() {
    int time = timeMonths();
    long long amount = rng_.below(1000 * (time + 5) / 6) + 500;
    if (s_.cash < amount || s_.hold < 10) return;
    push(question(EvId::NewGun, kComprador,
                  fmt("Do you wish to buy a ship's gun\nfor %s, Taipan?", fancy(amount).c_str()), amount));
}

void Game::openWu() {
    if (broke()) {
        int i = rng_.below(1500) + 500;
        s_.wuBailout++;
        int j = rng_.below(2000) * s_.wuBailout + 1500;
        insertFront(std::vector<Event>(
            1, question(EvId::WuBailout, kComprador,
                        fmt("Elder Brother is aware of your plight,\nTaipan.  He is willing to loan you an\nadditional %d if you will pay back\n%d. Are you willing, Taipan?", i, j),
                        i, j)));
    } else {
        Event e;
        e.kind = EvKind::Action;
        e.id = EvId::WuScreen;
        e.title = kComprador;
        insertFront(std::vector<Event>(1, e));
    }
}

void Game::applyRepair(long long amount) {
    if (pendBr_ > 0) s_.damage -= (int)((double)amount / pendBr_ + 0.5);
    if (s_.damage < 0) s_.damage = 0;
}

// ---------------------------------------------------------------- answers

void Game::answerYes(bool yes) {
    if (!haveCur_ || cur_.kind != EvKind::Question) return;
    Event e = cur_;
    haveCur_ = false;
    std::vector<Event> follow;
    switch (e.id) {
    case EvId::LiYuenPay:
        if (!yes) break;
        if (e.a <= s_.cash) {
            s_.cash -= e.a;
            s_.li = 1;
        } else {
            follow.push_back(info(kComprador, "Taipan, you do not have enough cash!!"));
            follow.push_back(question(EvId::LiYuenWuCover, kComprador,
                                      "Do you want Elder Brother Wu to make up\nthe difference for you?", e.a));
        }
        break;
    case EvId::LiYuenWuCover:
        if (yes) {
            s_.debt += e.a - s_.cash;
            s_.cash = 0;
            s_.li = 1;
            follow.push_back(info(kComprador, "Elder Brother has given Li Yuen the\ndifference between what he wanted and\nyour cash on hand and added the same\namount to your debt."));
        } else {
            s_.cash = 0;
            follow.push_back(info(kComprador, "Very well. Elder Brother Wu will not pay\nLi Yuen the difference.  I would be very\nwary of pirates if I were you, Taipan."));
        }
        break;
    case EvId::WuBusiness:
        if (yes)
            openWu();
        else
            wuLeave();
        break;
    case EvId::WuBailout:
        if (yes) {
            s_.cash += e.a;
            s_.debt += e.b;
            follow.push_back(info(kComprador, "Very well, Taipan.  Good joss!!"));
        } else {
            push(info(kComprador, "Very well, Taipan, the game is over!"));
            gameOver(false);
        }
        break;
    case EvId::NewShip:
        if (yes) {
            s_.cash -= e.a;
            s_.hold += 50;
            s_.capacity += 50;
            s_.damage = 0;
        }
        if (rng_.below(2) == 0 && s_.guns < 1000) offerNewGun();
        break;
    case EvId::NewGun:
        if (yes) {
            s_.cash -= e.a;
            s_.hold -= 10;
            s_.guns += 1;
        }
        break;
    case EvId::Repairs:
        if (yes) {
            int percent = (int)(((double)s_.damage / s_.capacity) * 100);
            int time = timeMonths();
            pendBr_ = (long long)((((60 * (time + 3) / 4) * rng_.unit()) + 25 * (time + 3) / 4) * s_.capacity / 50.0);
            if (pendBr_ < 1) pendBr_ = 1;
            long long price = pendBr_ * s_.damage + 1;
            Event n;
            n.kind = EvKind::Number;
            n.id = EvId::RepairAmount;
            n.title = kComprador;
            n.text = fmt("Och, 'tis a pity to be %d%% damaged.\nWe can fix yer whole ship for %lld,\nor make partial repairs if you wish.\nHow much will ye spend?", percent, price);
            n.a = price;
            n.b = pendBr_;
            follow.push_back(n);
        }
        break;
    case EvId::RepairWuCover:
        if (yes) {
            s_.debt += e.a - s_.cash;
            s_.cash = 0;
            applyRepair(e.a);
            follow.push_back(info(kComprador, "Elder Brother has given McHenry the\ndifference between what he wanted and\nyour cash on hand and added the same\namount to your debt."));
        } else {
            s_.cash = 0;
            follow.push_back(info(kComprador, "Very well. Elder Brother Wu will not pay\nMcHenry the difference.  I would be very\nwary of pirates if I were you, Taipan."));
        }
        break;
    default:
        break;
    }
    if (!follow.empty()) insertFront(follow);
}

void Game::answerNumber(long long n) {
    if (!haveCur_ || cur_.kind != EvKind::Number) return;
    Event e = cur_;
    haveCur_ = false;
    if (e.id != EvId::RepairAmount) return;
    if (n < 0 || n > e.a) n = e.a;  // "all", and no overpaying
    if (n > s_.cash) {
        std::vector<Event> follow;
        follow.push_back(info(kComprador, "Taipan, you do not have enough cash!!"));
        follow.push_back(question(EvId::RepairWuCover, kComprador,
                                  "Do you want Elder Brother Wu to make up\nthe difference for you?", n));
        insertFront(follow);
        return;
    }
    s_.cash -= n;
    applyRepair(n);
}

// ---------------------------------------------------------------- port actions

#define NEED_PORT() do { if (phase_ != Phase::Port) return Result::BadPhase; } while (0)
#define NEED_HK() do { NEED_PORT(); if (s_.port != kHongKong) return Result::BadPhase; } while (0)
// Wu may be visited from the arrival sequence as well as the port menu.
#define NEED_WU() do { if ((phase_ != Phase::Port && phase_ != Phase::Arrival) || s_.port != kHongKong) return Result::BadPhase; } while (0)
#define CHECK_ITEM() do { if (item < 0 || item > 3) return Result::BadAmount; } while (0)

Result Game::buy(int item, long long qty) {
    NEED_PORT();
    CHECK_ITEM();
    long long afford = maxBuy(item);
    if (qty < 0) qty = afford;
    if (qty > afford) return Result::NoFunds;
    s_.cash -= qty * s_.price[item];
    s_.ship[item] += (int)qty;
    s_.hold -= (int)qty;
    return Result::Ok;
}

Result Game::sell(int item, long long qty) {
    NEED_PORT();
    CHECK_ITEM();
    if (qty < 0) qty = s_.ship[item];
    if (qty > s_.ship[item]) return Result::NoStock;
    s_.ship[item] -= (int)qty;
    s_.cash += qty * s_.price[item];
    s_.hold += (int)qty;
    return Result::Ok;
}

Result Game::deposit(long long amt) {
    NEED_HK();
    if (amt < 0) amt = s_.cash;
    if (amt > s_.cash) return Result::NoFunds;
    s_.cash -= amt;
    s_.bank += amt;
    return Result::Ok;
}

Result Game::withdraw(long long amt) {
    NEED_HK();
    if (amt < 0) amt = s_.bank;
    if (amt > s_.bank) return Result::NoFunds;
    s_.bank -= amt;
    s_.cash += amt;
    return Result::Ok;
}

Result Game::toWarehouse(int item, long long qty) {
    NEED_HK();
    CHECK_ITEM();
    if (qty < 0) qty = std::min<long long>(s_.ship[item], kWarehouseCapacity - warehouseUsed());
    if (qty > s_.ship[item]) return Result::NoStock;
    if (warehouseUsed() + qty > kWarehouseCapacity) return Result::NoRoom;
    s_.ship[item] -= (int)qty;
    s_.wh[item] += (int)qty;
    s_.hold += (int)qty;
    return Result::Ok;
}

Result Game::toShip(int item, long long qty) {
    NEED_HK();
    CHECK_ITEM();
    if (qty < 0) qty = s_.wh[item];
    if (qty > s_.wh[item]) return Result::NoStock;
    s_.wh[item] -= (int)qty;
    s_.ship[item] += (int)qty;
    s_.hold -= (int)qty;
    return Result::Ok;
}

bool Game::wuEnter() {
    if ((phase_ != Phase::Port && phase_ != Phase::Arrival) || s_.port != kHongKong) return false;
    if (broke()) {
        openWu();
        return false;
    }
    return true;
}

Result Game::wuRepay(long long amt) {
    NEED_WU();
    if (s_.cash <= 0 || s_.debt == 0) return Result::BadAmount;
    if (amt < 0) amt = std::min(s_.cash, s_.debt);
    if (amt > s_.cash) return Result::NoFunds;
    if (amt > s_.debt) amt = s_.debt;
    s_.cash -= amt;
    s_.debt -= amt;
    return Result::Ok;
}

Result Game::wuBorrow(long long amt) {
    NEED_WU();
    if (amt < 0) amt = s_.cash * 2;
    if (amt > s_.cash * 2) return Result::Refused;
    s_.cash += amt;
    s_.debt += amt;
    return Result::Ok;
}

void Game::wuLeave() {
    if (s_.debt > 20000 && s_.cash > 0 && rng_.below(5) == 0) {
        int num = rng_.below(3) + 1;
        s_.cash = 0;
        insertFront(std::vector<Event>(
            1, info(kComprador, fmt("Bad joss!!\n%d of your bodyguards have been killed\nby cutthroats and you have been robbed\nof all of your cash, Taipan!!", num))));
    }
}

Result Game::sail(int dest) {
    NEED_PORT();
    if (dest < 1 || dest > 7 || dest == s_.port) return Result::BadAmount;
    if (s_.hold < 0) return Result::Overloaded;
    s_.port = dest;
    phase_ = Phase::Voyage;
    vstage_ = V_START;
    resultSet_ = false;
    return Result::Ok;
}

void Game::retire() {
    if (!canRetire()) return;
    push(info(kComprador, "Y o u ' r e    a\n\nM I L L I O N A I R E !"));
    gameOver(true);
}

// ---------------------------------------------------------------- voyage

void Game::stepVoyage() {
    switch (vstage_) {
    case V_START:
        if (rng_.below(s_.bp) == 0) {
            int n = rng_.below(s_.capacity / 10 + s_.guns) + 1;
            if (n > 9999) n = 9999;
            push(info(kCaptain, fmt("%d hostile ships approaching, Taipan!", n)));
            beginBattle(BATTLE_GENERIC, n);
        } else {
            vstage_ = V_LI_CHECK;
        }
        break;

    case V_LI_CHECK: {
        bool interrupted = resultSet_ && result_ == BattleStatus::Interrupted;
        bool none = !resultSet_;
        if (interrupted) push(info(kCaptain, "Li Yuen's fleet drove them off!"));
        if ((none && rng_.below(4 + 8 * s_.li) == 0) || interrupted) {
            push(info(kCaptain, "Li Yuen's pirates, Taipan!!"));
            if (s_.li > 0) {
                // As in the original: no month advance, straight to the next arrival.
                push(info(kCaptain, "Good joss!! They let us be!!"));
                phase_ = Phase::Arrival;
                arrStep_ = 0;
                return;
            }
            int n = rng_.below(s_.capacity / 5 + s_.guns) + 5;
            push(info(kCaptain, fmt("%d ships of Li Yuen's pirate\nfleet, Taipan!!", n)));
            resultSet_ = false;
            beginBattle(BATTLE_LI_YUEN, n);
        } else {
            vstage_ = V_RESULT;
        }
        break;
    }

    case V_RESULT:
        vstage_ = V_STORM;
        if (resultSet_) {
            if (result_ == BattleStatus::Won) {
                s_.cash += battle_.booty;
                push(info(kCaptain, fmt("We captured some booty.\nIt's worth %s!", fancy(battle_.booty).c_str())));
            } else if (result_ == BattleStatus::Fled) {
                push(info(kCaptain, "We made it!"));
            } else if (result_ == BattleStatus::Lost) {
                push(info(kCaptain, "The buggers got us, Taipan!!!\nIt's all over, now!!!"));
                gameOver(false);
            }
        }
        break;

    case V_STORM:
        vstage_ = V_ARRIVE;
        if (rng_.below(10) == 0) {
            push(info(kCaptain, "Storm, Taipan!!"));
            if (rng_.below(30) == 0) {
                push(info(kCaptain, "I think we're going down!!"));
                if (((double)s_.damage / s_.capacity * 3) * rng_.unit() >= 1) {
                    push(info(kCaptain, "We're going down, Taipan!!"));
                    gameOver(false);
                    return;
                }
            }
            push(info(kCaptain, "We made it!!"));
            if (rng_.below(3) == 0) {
                int orig = s_.port;
                while (s_.port == orig) s_.port = rng_.below(7) + 1;
                push(info(kCaptain, fmt("We've been blown off course\nto %s", kPortName[s_.port])));
            }
        }
        break;

    case V_ARRIVE:
        s_.month++;
        if (s_.month == 13) {
            s_.month = 1;
            s_.year++;
            s_.ec += 10;
            s_.ed += 0.5;
        }
        s_.debt = (long long)(s_.debt + s_.debt * 0.1);
        s_.bank = (long long)(s_.bank + s_.bank * 0.005);
        if (s_.debt > kMoneyCap) s_.debt = kMoneyCap;  // keep compounding debt from overflowing
        if (s_.bank > kMoneyCap) s_.bank = kMoneyCap;
        setPrices();
        push(info(kCaptain, fmt("Arriving at %s...", kPortName[s_.port])));
        phase_ = Phase::Arrival;
        arrStep_ = 0;
        break;
    }
}

// ---------------------------------------------------------------- battle

void Game::beginBattle(int kind, int ships) {
    Battle b;
    b.kind = kind;
    b.numShips = b.startShips = ships;
    b.booty = (long long)(timeMonths() / 4) * 1000 * ships + rng_.below(1000) + 250;
    battle_ = b;
    fillScreen();
    phase_ = Phase::Battle;
}

void Game::debugBeginBattle(int kind, int ships) { beginBattle(kind, ships); }

void Game::fillScreen() {
    Battle& b = battle_;
    for (int i = 0; i < 10; i++) {
        if (b.numShips > b.numOnScreen && b.onScreen[i] == 0) {
            b.onScreen[i] = (int)(s_.ec * rng_.unit() + 20);
            b.numOnScreen++;
        }
    }
}

void Game::trimScreen() {
    Battle& b = battle_;
    if (b.numShips > 10) return;
    for (int i = 9; i >= 0; i--) {
        if (b.numOnScreen > b.numShips && b.onScreen[i] > 0) {
            b.onScreen[i] = 0;
            b.numOnScreen--;
        }
    }
}

void Game::finishBattle(BattleStatus st) {
    result_ = st;
    resultSet_ = true;
    phase_ = Phase::Voyage;
    vstage_ = (battle_.kind == BATTLE_GENERIC) ? V_LI_CHECK : V_RESULT;
}

RoundResult Game::battleRound(int orders, int throwItem, long long throwAmt) {
    RoundResult r;
    if (phase_ != Phase::Battle || battle_.numShips <= 0) {
        r.status = BattleStatus::Won;
        return r;
    }
    Battle& b = battle_;
    const int id = b.kind;
    b.orders = orders;
    if (seaworthiness() <= 0) {  // original checks this at the top of every round
        r.status = BattleStatus::Lost;
        finishBattle(r.status);
        return r;
    }
    fillScreen();

    if (orders == ORD_FIGHT && s_.guns > 0) {
        b.ok = 3;
        b.ik = 1;
        int sk = 0;
        r.lines.push_back("Aye, we'll fight 'em, Taipan.");
        r.lines.push_back("We're firing on 'em, Taipan!");
        for (int i = 1; i <= s_.guns; i++) {
            if (b.numOnScreen == 0) fillScreen();
            int live[10], k = 0;
            for (int j = 0; j < 10; j++)
                if (b.onScreen[j] > 0) live[k++] = j;
            if (k == 0) break;
            int t = live[rng_.below(k)];
            b.onScreen[t] -= rng_.below(30) + 10;
            if (b.onScreen[t] <= 0) {
                b.onScreen[t] = 0;
                b.numOnScreen--;
                b.numShips--;
                sk++;
            }
            if (b.numShips == 0) break;
        }
        r.lines.push_back(sk > 0 ? fmt("Sunk %d of the buggers, Taipan!", sk)
                                 : std::string("Hit 'em, but didn't sink 'em, Taipan!"));
        if (b.startShips > 0 && b.numShips > 2 && rng_.below(b.startShips) > b.numShips * 0.6 / id) {
            int divisor = b.numShips / 3 / id;
            if (divisor == 0) divisor = 1;
            int ran = rng_.below(divisor);
            if (ran == 0) ran = 1;
            b.numShips -= ran;
            r.lines.push_back(fmt("%d ran away, Taipan!", ran));
            trimScreen();
        }
    } else if (orders == ORD_FIGHT) {
        r.lines.push_back("We have no guns, Taipan!!");
    } else if (orders == ORD_THROW) {
        long long amount = 0, total = 0;
        if (throwItem >= 0 && throwItem < 4) {
            total = s_.ship[throwItem];
            amount = throwAmt;
            if (amount < 0 || amount > total) amount = total;
        } else {
            total = (long long)s_.ship[0] + s_.ship[1] + s_.ship[2] + s_.ship[3];
        }
        if (total > 0) {
            r.lines.push_back("Let's hope we lose 'em, Taipan!");
            if (throwItem >= 0 && throwItem < 4) {
                s_.ship[throwItem] -= (int)amount;
                s_.hold += (int)amount;
                b.ok += (int)(amount / 10);
            } else {
                for (int i = 0; i < 4; i++) s_.ship[i] = 0;
                s_.hold += (int)total;
                b.ok += (int)(total / 10);
            }
        } else {
            r.lines.push_back("There's nothing there, Taipan!");
        }
    }

    if ((orders == ORD_RUN || orders == ORD_THROW) && b.numShips > 0) {
        if (orders == ORD_RUN) r.lines.push_back("Aye, we'll run, Taipan.");
        b.ok += b.ik++;
        if (rng_.below(b.ok) > rng_.below(b.numShips)) {
            r.lines.push_back("We got away from 'em, Taipan!");
            b.numShips = 0;
        } else {
            r.lines.push_back("Couldn't lose 'em.");
            if (b.numShips > 2 && rng_.below(5) == 0) {
                int lost = rng_.below(b.numShips) / 2 + 1;
                b.numShips -= lost;
                r.lines.push_back(fmt("But we escaped from %d of 'em!", lost));
                trimScreen();
            }
        }
    }

    if (b.numShips > 0) {
        r.lines.push_back("They're firing on us, Taipan!");
        r.lines.push_back("We've been hit, Taipan!!");
        int i = b.numShips > 15 ? 15 : b.numShips;
        double pct = ((double)s_.damage / s_.capacity) * 100;
        if (s_.guns > 0 && (rng_.below(100) < pct || pct > 80)) {
            i = 1;
            s_.guns--;
            s_.hold += 10;
            r.lines.push_back("The buggers hit a gun, Taipan!!");
        }
        s_.damage = (int)(s_.damage + (s_.ed * i * id) * rng_.unit() + (i / 2));
        if (id == BATTLE_GENERIC && rng_.below(20) == 0) {
            r.status = BattleStatus::Interrupted;
            finishBattle(r.status);
            return r;
        }
        if (seaworthiness() <= 0) {
            r.status = BattleStatus::Lost;
            finishBattle(r.status);
            return r;
        }
        fillScreen();
        return r;
    }

    if (orders == ORD_FIGHT) {
        r.lines.push_back("We got 'em all, Taipan!");
        r.status = BattleStatus::Won;
    } else {
        r.status = BattleStatus::Fled;
    }
    finishBattle(r.status);
    return r;
}

// ---------------------------------------------------------------- final report

FinalReport Game::finalReport() const {
    FinalReport f;
    f.net = s_.cash + s_.bank - s_.debt;
    f.capacity = s_.capacity;
    f.guns = s_.guns;
    f.years = s_.year - 1860;
    f.months = s_.month;
    f.retired = retired_;
    int time = timeMonths();
    if (time < 1) time = 1;
    f.score = (long long)std::floor((double)f.net / 100.0 / time + 0.5);
    f.rating = f.score >= 50000 ? 0 : f.score >= 8000 ? 1 : f.score >= 1000 ? 2 : f.score >= 500 ? 3 : 4;
    return f;
}

// ---------------------------------------------------------------- save / load

static const char* const kMagic = "TAIPAN1";

bool Game::save(std::ostream& os) const {
    if (phase_ != Phase::Port || haveCur_ || !queue_.empty()) return false;
    std::string firm = s_.firm;
    for (size_t i = 0; i < firm.size(); i++)
        if (firm[i] == '\n' || firm[i] == '\r') firm[i] = ' ';
    os << kMagic << "\n";
    os << "firm=" << firm << "\n";
    os << "cash=" << s_.cash << "\nbank=" << s_.bank << "\ndebt=" << s_.debt << "\n";
    os << "hold=" << s_.hold << "\ncapacity=" << s_.capacity << "\nguns=" << s_.guns
       << "\ndamage=" << s_.damage << "\nmonth=" << s_.month << "\nyear=" << s_.year
       << "\nport=" << s_.port << "\nli=" << s_.li << "\nwuWarn=" << s_.wuWarn
       << "\nwuBailout=" << s_.wuBailout << "\nbp=" << s_.bp << "\n";
    for (int i = 0; i < 4; i++) os << "ship" << i << "=" << s_.ship[i] << "\n";
    for (int i = 0; i < 4; i++) os << "wh" << i << "=" << s_.wh[i] << "\n";
    for (int i = 0; i < 4; i++) os << "price" << i << "=" << s_.price[i] << "\n";
    os << "ec=" << (long long)s_.ec << "\ned2=" << (long long)(s_.ed * 2 + 0.5) << "\n";
    os << "rng=" << rng_.state() << "\n";
    os << "end\n";
    return (bool)os;
}

bool Game::load(std::istream& is) {
    std::string line;
    if (!std::getline(is, line) || line != kMagic) return false;
    std::map<std::string, std::string> kv;
    bool ended = false;
    while (std::getline(is, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line == "end") {
            ended = true;
            break;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) return false;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (!ended) return false;

    bool ok = true;
    struct Get {
        std::map<std::string, std::string>& m;
        bool& ok;
        long long operator()(const std::string& k, long long lo, long long hi) {
            std::map<std::string, std::string>::iterator it = m.find(k);
            if (it == m.end() || it->second.empty()) {
                ok = false;
                return lo;
            }
            char* end = 0;
            long long v = strtoll(it->second.c_str(), &end, 10);
            if (*end != 0 || v < lo || v > hi) {
                ok = false;
                return lo;
            }
            return v;
        }
    } get = {kv, ok};

    State n;
    n.firm = kv.count("firm") ? kv["firm"].substr(0, 22) : std::string();
    const long long big = 100 * kMoneyCap;
    n.cash = get("cash", 0, big);
    n.bank = get("bank", 0, big);
    n.debt = get("debt", 0, big);
    n.capacity = (int)get("capacity", 50, 1000000);
    n.guns = (int)get("guns", 0, 100000);
    n.hold = (int)get("hold", -100000000, 100000000);
    n.damage = (int)get("damage", 0, 100000000);
    n.month = (int)get("month", 1, 12);
    n.year = (int)get("year", 1860, 3000);
    n.port = (int)get("port", 1, 7);
    n.li = (int)get("li", 0, 3);
    n.wuWarn = (int)get("wuWarn", 0, 1);
    n.wuBailout = (int)get("wuBailout", 0, 100000);
    n.bp = (int)get("bp", 1, 100);
    for (int i = 0; i < 4; i++) {
        n.ship[i] = (int)get("ship" + std::string(1, '0' + i), 0, 100000000);
        n.wh[i] = (int)get("wh" + std::string(1, '0' + i), 0, kWarehouseCapacity);
        n.price[i] = get("price" + std::string(1, '0' + i), 0, big);
    }
    n.ec = (double)get("ec", 20, 100000);
    n.ed = get("ed2", 1, 1000000) / 2.0;
    long long rngState = (long long)strtoull(kv.count("rng") ? kv["rng"].c_str() : "0", 0, 10);
    if (!ok) return false;
    if (n.wh[0] + n.wh[1] + n.wh[2] + n.wh[3] > kWarehouseCapacity) return false;
    long long used = (long long)n.ship[0] + n.ship[1] + n.ship[2] + n.ship[3];
    if (n.hold + used + 10LL * n.guns != n.capacity) return false;

    s_ = n;
    rng_.setState((uint64_t)rngState);
    queue_.clear();
    haveCur_ = false;
    battle_ = Battle();
    pendBr_ = 0;
    retired_ = false;
    resultSet_ = false;
    phase_ = Phase::Port;
    return true;
}

}  // namespace tp
