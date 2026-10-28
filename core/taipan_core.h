// Portable game core for the Taipan port. No UI or InkView dependencies.
// Rules follow orig/taipan.c; see CLAUDE.md for the deliberate differences.
#pragma once

#include <deque>
#include <iosfwd>
#include <stdint.h>
#include <string>
#include <vector>

namespace tp {

enum Item { OPIUM = 0, SILK = 1, ARMS = 2, GENERAL = 3, NUM_ITEMS = 4 };

extern const char* const kItemName[4];
extern const char* const kPortName[8];  // [0] = "At sea", [1..7] = ports
extern const char* const kMonthName[12];
extern const char* const kStatusName[6];
extern const char* const kRatingName[5];

const int kWarehouseCapacity = 10000;
const int kHongKong = 1;

// Small deterministic generator so games and tests are repeatable and savable.
class Rng {
public:
    explicit Rng(uint64_t seed = 1);
    uint64_t next();
    int below(int n);     // uniform in [0, n); returns 0 when n <= 0
    double unit();        // uniform in [0, 1)
    uint64_t state() const { return s_; }
    void setState(uint64_t s) { s_ = s ? s : 1; }
private:
    uint64_t s_;
};

struct State {
    std::string firm;
    long long cash, bank, debt;
    int hold;      // free space on the ship (negative = overloaded)
    int capacity;
    int guns;
    int damage;
    int month, year;
    int port;      // 1..7; while at sea, the destination
    int li;        // Li Yuen protection: 0 = unpaid, 1..3 = paid
    int wuWarn, wuBailout;
    int bp;        // 1-in-bp chance of pirates on a voyage
    int ship[4];
    int wh[4];
    long long price[4];
    double ec, ed; // enemy health base, enemy damage; grow each year
    State();
};

enum class Phase { Port, Arrival, Voyage, Battle, Over };
enum class EvKind { Message, Question, Number, Action, GameOver };
enum class EvId {
    Info, LiYuenPay, LiYuenWuCover, WuBusiness, WuBailout, NewShip, NewGun,
    Repairs, RepairAmount, RepairWuCover, WuScreen, Over
};

struct Event {
    EvKind kind;
    EvId id;
    std::string title;
    std::string text;
    long long a, b;
    Event() : kind(EvKind::Message), id(EvId::Info), a(0), b(0) {}
};

enum class Result { Ok, BadPhase, BadAmount, NoFunds, NoStock, NoRoom, Overloaded, Refused };

enum Orders { ORD_NONE = 0, ORD_FIGHT = 1, ORD_RUN = 2, ORD_THROW = 3 };
enum BattleKind { BATTLE_GENERIC = 1, BATTLE_LI_YUEN = 2 };
enum class BattleStatus { Continue, Won, Fled, Lost, Interrupted };

struct Battle {
    int kind;
    int numShips;      // total enemy ships
    int startShips;
    int onScreen[10];  // enemy health per visible slot; 0 = empty
    int numOnScreen;
    int orders;
    int ok, ik;        // escape odds bookkeeping (as in the original)
    long long booty;
    Battle();
};

struct RoundResult {
    std::vector<std::string> lines;
    BattleStatus status;
    RoundResult() : status(BattleStatus::Continue) {}
};

struct FinalReport {
    long long net;
    int capacity, guns, years, months;
    long long score;
    int rating;   // index into kRatingName: 0 = Ma Tsu ... 4 = Galley Hand
    bool retired;
    FinalReport() : net(0), capacity(0), guns(0), years(0), months(0), score(0), rating(4), retired(false) {}
};

std::string fancy(long long n);  // "1.5 Million" style for big amounts

class Game {
public:
    explicit Game(uint64_t seed = 1);

    void reseed(uint64_t seed);
    // Starts a fresh game and queues the first arrival (Hong Kong).
    void newGame(const std::string& firm, bool cashStart);

    Phase phase() const { return phase_; }
    const State& st() const { return s_; }
    const Battle& battle() const { return battle_; }
    int timeMonths() const;          // months since Jan 1860, 1-based
    int seaworthiness() const;       // 0..100
    int warehouseUsed() const;
    long long maxBuy(int item) const;
    bool canRetire() const;
    bool broke() const;              // Wu bailout condition

    // Event pump. Returns the current event, generating arrival/voyage events
    // as needed. false = nothing pending (inspect phase()).
    bool next(Event& e);
    void ack();                      // Message / Action
    void answerYes(bool yes);        // Question
    void answerNumber(long long n);  // Number (-1 = all)

    // Port actions (Phase::Port, no event pending). qty < 0 means "all/max".
    Result buy(int item, long long qty);
    Result sell(int item, long long qty);
    Result deposit(long long amt);
    Result withdraw(long long amt);
    Result toWarehouse(int item, long long qty);
    Result toShip(int item, long long qty);
    bool wuEnter();                  // false: a bailout question was queued instead
    Result wuRepay(long long amt);
    Result wuBorrow(long long amt);
    void wuLeave();                  // may queue the cutthroat robbery
    Result sail(int dest);           // 1..7, not the current port
    void retire();                   // only if canRetire()

    // Battle (Phase::Battle). throwItem 0..3 or 4 = everything; throwAmt < 0 = all of it.
    RoundResult battleRound(int orders, int throwItem = 4, long long throwAmt = -1);

    FinalReport finalReport() const;

    // Save only works in Phase::Port with no event pending.
    bool save(std::ostream& os) const;
    bool load(std::istream& is);

    // Test hooks.
    State& mutableState() { return s_; }
    void debugBeginBattle(int kind, int ships);

private:
    enum Stage { V_START, V_LI_CHECK, V_RESULT, V_STORM, V_ARRIVE };

    void setPrices();
    void push(const Event& e);
    void insertFront(const std::vector<Event>& v);
    Event info(const char* title, const std::string& text) const;
    Event question(EvId id, const char* title, const std::string& text, long long a = 0, long long b = 0) const;
    void gameOver(bool retired);
    void stepArrival();
    void arrivalStep(int step);
    void stepVoyage();
    void offerNewShip();
    void offerNewGun();
    void offerRepairs();
    void beginLiYuenExtortion();
    void openWu();
    void applyRepair(long long amount);
    void beginBattle(int kind, int ships);
    void finishBattle(BattleStatus st);
    void fillScreen();
    void trimScreen();

    Rng rng_;
    State s_;
    Phase phase_;
    std::deque<Event> queue_;
    Event cur_;
    bool haveCur_;
    int arrStep_;
    Stage vstage_;
    BattleStatus result_;       // of the latest battle this voyage
    bool resultSet_;            // false = no battle happened yet
    Battle battle_;
    long long pendBr_;
    bool retired_;
};

}  // namespace tp
