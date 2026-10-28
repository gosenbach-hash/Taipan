// Taipan for PocketBook Era Color: thin InkView layer over core/taipan_core.
// All game rules live in the core; this file only draws screens and forwards taps.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "inkview.h"
#include "taipan_core.h"

// ------------------------------------------------------------------ palette (flat, bold)
static const int C_WHITE = 0x00FFFFFF, C_BLACK = 0x00000000, C_GREY = 0x00555555;
static const int C_NAVY = 0x00203A60, C_PARCH = 0x00F2E6C4, C_BTN = 0x00BCD4EE;
static const int C_GOOD = 0x00B8E0B8, C_BAD = 0x00F0B8B8, C_OFF = 0x00DDDDDD, C_GOLD = 0x00F0C040;
static const int C_RED = 0x00B02020, C_GREEN = 0x00208020, C_AMBER = 0x00D09000;

static ifont *fMain = nullptr, *fSmall = nullptr, *fBig = nullptr;
static int SW = 1264, SH = 1680;

// ------------------------------------------------------------------ app state
enum Screen { SC_START, SC_NAME, SC_PORT, SC_EVENT, SC_PAD, SC_DEST, SC_BATTLE, SC_OVER, SC_BANK, SC_TRANSFER, SC_WU, SC_THROW };
enum PadKind { PK_BUY, PK_SELL, PK_REPAIR, PK_DEPOSIT, PK_WITHDRAW, PK_TOWH, PK_TOSHIP, PK_REPAY, PK_BORROW, PK_THROW };

static tp::Game game;
static Screen scr = SC_START, lastScr = (Screen)-1;
static tp::Event ev;
static std::string toast;
static int partials = 0;

static PadKind padKind = PK_BUY;
static int padItem = 0;
static long long padMax = 0, padValue = 0;
static std::string padTitle, padInfo;
static Screen padReturn = SC_PORT;  // where the pad goes back to

static bool pendingCash = true;     // start option chosen on the start screen
static std::string nameText;        // firm name being typed
static const size_t kMaxName = 22;

static std::vector<std::string> battleLog;
static bool inBattle = false, battleDone = false;

static const char *kSavePath = STATEPATH "/taipan.sav";
static const char *kSaveTmp = STATEPATH "/taipan.sav.tmp";

// ------------------------------------------------------------------ buttons
enum {
    B_NONE = 0, B_START_CASH, B_START_GUNS, B_CONTINUE, B_NEWGAME, B_SAIL, B_RETIRE, B_BANK, B_TRANSFER, B_WU,
    B_YES, B_NO, B_CONT, B_CANCEL, B_OVER_NEW,
    B_FIGHT, B_RUN, B_THROW,
    B_DEPOSIT, B_WITHDRAW, B_DONE, B_REPAY, B_BORROW, B_LEAVE,
    B_NAME_DEL, B_NAME_CLR, B_NAME_DONE, B_THROW_ALL, B_THROW_BACK,
    B_BUY = 100,    // +item
    B_SELL = 110,   // +item
    B_DEST = 120,   // +port 1..7
    B_TOWH = 130,   // +item
    B_TOSHIP = 140, // +item
    B_THROW_ITEM = 150, // +item
    B_CH = 400,     // +ASCII code of the typed character
    B_KEY = 200,    // +digit
    B_CLR = 210, B_DEL, B_MAX, B_OK
};
struct Btn { int x, y, w, h, id; };
static std::vector<Btn> btns;

// ------------------------------------------------------------------ helpers
static std::string commas(long long n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", n < 0 ? -n : n);
    std::string s = buf, out;
    int cnt = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[i]);
        if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return n < 0 ? "-" + out : out;
}

static std::string fmt(const char *f, long long a) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a);
    return buf;
}

static void txt(int x, int y, int w, int h, const std::string &s, int align, ifont *f, int color) {
    SetFont(f, color);
    DrawTextRect(x, y, w, h, s.c_str(), align);
}

// Draws text with embedded newlines, one DrawTextRect per line.
static int lines(int x, int y, int w, const std::string &s, int lineH, ifont *f, int color, int align = ALIGN_LEFT) {
    std::string cur;
    int yy = y;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '\n') {
            if (!cur.empty()) txt(x, yy, w, lineH, cur, align | VALIGN_MIDDLE, f, color);
            yy += lineH;
            cur.clear();
        } else {
            cur += s[i];
        }
    }
    return yy;
}

static void box(int x, int y, int w, int h, int fill, int border = 4) {
    FillArea(x, y, w, h, C_BLACK);
    FillArea(x + border, y + border, w - 2 * border, h - 2 * border, fill);
}

static void button(int id, int x, int y, int w, int h, const std::string &label, int fill, bool enabled = true,
                   ifont *f = nullptr, int textColor = C_BLACK) {
    if (!f) f = fMain;
    box(x, y, w, h, enabled ? fill : C_OFF);
    const int color = enabled ? textColor : 0x00888888;
    if (label.find('\n') == std::string::npos) {
        txt(x, y, w, h, label, ALIGN_CENTER | VALIGN_MIDDLE, f, color);
    } else {
        int n = 1;
        for (size_t i = 0; i < label.size(); i++)
            if (label[i] == '\n') n++;
        const int lh = (f == fBig) ? 90 : (f == fSmall) ? 44 : 56;
        lines(x, y + (h - n * lh) / 2, w, label, lh, f, color, ALIGN_CENTER);
    }
    if (enabled) {
        Btn b = {x, y, w, h, id};
        btns.push_back(b);
    }
}

static std::string dateText() {
    const tp::State &s = game.st();
    char buf[32];
    std::snprintf(buf, sizeof buf, "15 %s %d", tp::kMonthName[s.month - 1], s.year);
    return buf;
}

static bool atPort() { return game.phase() == tp::Phase::Port || game.phase() == tp::Phase::Arrival; }

// ------------------------------------------------------------------ saving
static void saveGame() {
    std::stringstream ss;
    if (!game.save(ss)) return;  // only valid in port with nothing pending
    {
        std::ofstream f(kSaveTmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << ss.str();
        if (!f) return;
    }
    std::rename(kSaveTmp, kSavePath);  // write-then-rename so a crash never leaves a half save
}

static bool loadInto(tp::Game &g) {
    std::ifstream f(kSavePath, std::ios::binary);
    if (!f) return false;
    return g.load(f);
}

static void deleteSave() { std::remove(kSavePath); }

// ------------------------------------------------------------------ flow
static void pump() {
    toast.clear();
    if (game.next(ev)) {
        if (ev.kind == tp::EvKind::Number) {
            scr = SC_PAD;
            padKind = PK_REPAIR;
            padMax = ev.a;
            padValue = 0;
            padTitle = ev.text;
            padInfo = "";
        } else {
            scr = SC_EVENT;
        }
        return;
    }
    switch (game.phase()) {
    case tp::Phase::Port:
        scr = SC_PORT;
        inBattle = false;
        saveGame();
        break;
    case tp::Phase::Battle:
        scr = SC_BATTLE;
        if (!inBattle) {
            battleLog.clear();
            inBattle = true;
            battleDone = false;
        }
        break;
    case tp::Phase::Over:
        scr = SC_OVER;
        inBattle = false;
        deleteSave();
        break;
    default:
        break;
    }
}

static void startNew(bool cashStart, const std::string &firm) {
    game.reseed((uint64_t)std::time(nullptr) * 2654435761ULL + 12345);
    game.newGame(firm, cashStart);
    deleteSave();
    inBattle = false;
    pump();
}

static void openPad(PadKind k, int item, long long maxv, Screen back) {
    padKind = k;
    padItem = item;
    padMax = maxv;
    padValue = 0;
    padReturn = back;
    const tp::State &s = game.st();
    const std::string name = (item >= 0 && item < 4) ? tp::kItemName[item] : "";
    switch (k) {
    case PK_BUY:
        padTitle = "Buy " + name;
        padInfo = "Price " + commas(s.price[item]) + " each.  You can afford " + commas(maxv) + ".";
        break;
    case PK_SELL:
        padTitle = "Sell " + name;
        padInfo = "Price " + commas(s.price[item]) + " each.  You have " + commas(maxv) + ".";
        break;
    case PK_DEPOSIT:
        padTitle = "How much will you deposit?";
        padInfo = "You have " + commas(s.cash) + " in cash.";
        break;
    case PK_WITHDRAW:
        padTitle = "How much will you withdraw?";
        padInfo = "You have " + commas(s.bank) + " in the bank.";
        break;
    case PK_TOWH:
        padTitle = "Move " + name + " to the warehouse";
        padInfo = "You can move up to " + commas(maxv) + ".";
        break;
    case PK_TOSHIP:
        padTitle = "Move " + name + " aboard ship";
        padInfo = "The warehouse holds " + commas(maxv) + ".";
        break;
    case PK_REPAY:
        padTitle = "How much do you wish to repay him?";
        padInfo = "You owe " + commas(s.debt) + " and have " + commas(s.cash) + " in cash.";
        break;
    case PK_BORROW:
        padTitle = "How much do you wish to borrow?";
        padInfo = "He will lend up to twice your cash: " + commas(maxv) + ".";
        break;
    case PK_THROW:
        padTitle = "Throw " + name + " overboard";
        padInfo = "You have " + commas(maxv) + " on board.";
        break;
    case PK_REPAIR:
        break;
    }
    scr = SC_PAD;
}

// ------------------------------------------------------------------ drawing: common
static void drawHeader() {
    const tp::State &s = game.st();
    FillArea(0, 0, SW, 100, C_NAVY);
    txt(24, 0, SW / 2 - 24, 100, s.firm, ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_WHITE);
    std::string where = atPort() ? tp::kPortName[s.port] : "At sea";
    txt(SW / 2, 0, SW / 2 - 24, 52, where, ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_WHITE);
    txt(SW / 2, 50, SW / 2 - 24, 46, dateText(), ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, 0x00C8D8F0);
}

static void statusRow(int x, int y, int w, const char *label, const std::string &value, int color) {
    txt(x, y, w / 2, 72, label, ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_GREY);
    txt(x + w / 2, y, w / 2, 72, value, ALIGN_RIGHT | VALIGN_MIDDLE, fMain, color);
}

// Cash/bank/debt and ship status; returns the y below it.
static int drawStatus() {
    const tp::State &s = game.st();
    const int colW = SW / 2 - 40, lx = 24, rx = SW / 2 + 16, y0 = 110;
    statusRow(lx, y0, colW, "Cash", commas(s.cash), C_BLACK);
    statusRow(lx, y0 + 72, colW, "Bank", commas(s.bank), C_BLACK);
    statusRow(lx, y0 + 144, colW, "Debt", commas(s.debt), s.debt > 0 ? C_RED : C_BLACK);

    int sea = game.seaworthiness();
    int idx = sea / 20;
    if (idx > 5) idx = 5;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s %d%%", tp::kStatusName[idx], sea);
    statusRow(rx, y0, colW, "Ship", buf, sea < 40 ? C_RED : C_BLACK);
    if (s.hold >= 0)
        statusRow(rx, y0 + 72, colW, "Hold free", commas(s.hold), C_BLACK);
    else
        statusRow(rx, y0 + 72, colW, "Overload", commas(-s.hold), C_RED);
    statusRow(rx, y0 + 144, colW, "Guns", commas(s.guns), C_BLACK);
    FillArea(20, y0 + 222, SW - 40, 4, C_BLACK);
    return y0 + 232;
}

static void drawToast(int y) {
    if (toast.empty()) return;
    txt(24, y, SW - 48, 64, toast, ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_RED);
}

// ------------------------------------------------------------------ drawing: screens
// ---- title art: a junk under sail, built from flat rectangles and scanlines
static void fillCircle(int cx, int cy, int r, int color) {
    for (int dy = -r; dy <= r; dy++) {
        const int half = (int)std::sqrt((double)(r * r - dy * dy));
        FillArea(cx - half, cy + dy, 2 * half + 1, 1, color);
    }
}

// Sail panel with horizontal battens.
static void sailPanel(int x, int y, int w, int h, int fill) {
    FillArea(x, y, w, h, C_BLACK);
    FillArea(x + 5, y + 5, w - 10, h - 10, fill);
    for (int yy = y + h / 6; yy < y + h - 6; yy += h / 6) FillArea(x, yy, w, 6, C_BLACK);
}

static void drawJunk(int cx, int waterY) {
    const int hullTop = waterY - 70;
    // Hull: wider at the deck than at the keel.
    for (int i = 0; i < 70; i++) {
        const int w = 560 + 140 * (70 - i) / 70;  // 700 at the deck, 560 at the keel
        FillArea(cx - w / 2, hullTop + i, w, 1, 0x00603818);
    }
    FillArea(cx - 350, hullTop, 700, 8, C_BLACK);
    FillArea(cx - 330, hullTop + 26, 660, 10, C_RED);
    // Raised stern castle and bow.
    FillArea(cx + 230, hullTop - 60, 130, 62, 0x00603818);
    FillArea(cx + 230, hullTop - 60, 130, 8, C_BLACK);
    FillArea(cx - 360, hullTop - 24, 70, 26, 0x00603818);
    // Masts.
    FillArea(cx - 6, hullTop - 330, 12, 340, C_BLACK);
    FillArea(cx - 216, hullTop - 240, 10, 250, C_BLACK);
    FillArea(cx + 196, hullTop - 210, 10, 220, C_BLACK);
    // Sails.
    sailPanel(cx + 12, hullTop - 320, 170, 300, 0x00D9A441);
    sailPanel(cx - 196, hullTop - 235, 170, 215, C_RED);
    sailPanel(cx + 208, hullTop - 205, 120, 190, 0x00D9A441);
    // Pennant on the main mast.
    for (int i = 0; i < 26; i++) FillArea(cx + 6, hullTop - 360 + i, 70 - i * 2, 1, C_RED);
}

static void drawTitleArt(int y0, int h) {
    const int seaTop = y0 + h * 62 / 100;
    FillArea(0, y0, SW, seaTop - y0, 0x00D6EAF8);   // sky
    fillCircle(SW - 260, y0 + 120, 72, C_GOLD);      // sun
    FillArea(0, seaTop, SW, y0 + h - seaTop, 0x002F6FA8);  // sea
    for (int k = 0; k < 4; k++) {                    // wave dashes
        const int wy = seaTop + 40 + k * 46;
        for (int x = (k % 2) * 70 - 40; x < SW; x += 190) FillArea(x, wy, 96, 8, 0x007FB2DC);
    }
    drawJunk(SW / 2 - 40, seaTop + 24);
    FillArea(0, y0 + h, SW, 8, C_BLACK);
}

static void drawStart() {
    drawTitleArt(30, 500);
    txt(0, 560, SW, 130, "TAIPAN", ALIGN_CENTER | VALIGN_MIDDLE, fBig, C_NAVY);
    txt(0, 690, SW, 60, "A trading game of the China Seas, 1860", ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_GREY);
    txt(0, 780, SW, 70, "Do you want to start . . .", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_BLACK);
    button(B_START_CASH, 112, 870, SW - 224, 170, "With cash (and a debt)", C_BTN);
    button(B_START_GUNS, 112, 1060, SW - 224, 190, "With five guns and no cash\n(but no debt!)", C_BTN);
    tp::Game probe;
    if (loadInto(probe)) button(B_CONTINUE, 112, 1290, SW - 224, 150, "Continue saved game", C_GOOD);
    FillArea(200, 1470, SW - 400, 4, C_NAVY);
    lines(0, 1485, SW, "Created by Art Canfil    Programmed by Jay Link\nCopyright (c) 1978 - 2002 Art Canfil\nTouch port for PocketBook Era Color", 46,
          fSmall, C_GREY, ALIGN_CENTER);
}

static void drawThrow() {
    const tp::State &s = game.st();
    drawHeader();
    txt(0, 120, SW, 90, "Throw cargo overboard", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_NAVY);
    txt(0, 200, SW, 60, "What shall I throw, Taipan?", ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_GREY);
    int y = 290;
    txt(560, y, 200, 50, "On board", ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);
    y += 50;
    for (int i = 0; i < 4; i++) {
        const int ry = y + i * 150;
        FillArea(20, ry, SW - 40, 140, (i % 2) ? 0x00F4F4F4 : C_PARCH);
        txt(30, ry, 520, 140, tp::kItemName[i], ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
        txt(560, ry, 200, 140, commas(s.ship[i]), ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_BLACK);
        button(B_THROW_ITEM + i, 800, ry + 10, 424, 120, "Throw some", C_BTN, s.ship[i] > 0);
    }
    y += 4 * 150 + 20;
    button(B_THROW_ALL, 20, y, SW - 40, 150, "Throw everything", C_BAD, true, fMain);
    button(B_THROW_BACK, 20, SH - 230, SW - 40, 170, "Back to the battle", C_BTN, true, fBig);
}

static void drawPort() {
    const tp::State &s = game.st();
    drawHeader();
    int y = drawStatus();
    const bool hk = (s.port == tp::kHongKong);

    if (hk) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "Warehouse  Opium %d  Silk %d  Arms %d  General %d  (%d/%d)", s.wh[0], s.wh[1],
                      s.wh[2], s.wh[3], game.warehouseUsed(), tp::kWarehouseCapacity);
        txt(24, y, SW - 48, 56, buf, ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_GREY);
    }
    y += 60;
    txt(24, y, 340, 50, "Goods", ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_GREY);
    txt(360, y, 200, 50, "Price", ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);
    txt(560, y, 160, 50, "On ship", ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);
    y += 50;
    for (int i = 0; i < 4; i++) {
        const int ry = y + i * 130;
        FillArea(20, ry, SW - 40, 120, (i % 2) ? 0x00F4F4F4 : C_PARCH);
        txt(30, ry, 330, 120, tp::kItemName[i], ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
        txt(360, ry, 200, 120, commas(s.price[i]), ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_BLACK);
        txt(560, ry, 160, 120, commas(s.ship[i]), ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_BLACK);
        button(B_BUY + i, 750, ry + 10, 230, 100, "Buy", C_GOOD);
        button(B_SELL + i, 1000, ry + 10, 230, 100, "Sell", C_GOLD);
    }
    y += 4 * 130 + 10;
    drawToast(y);
    y += 70;
    button(B_BANK, 20, y, 400, 120, "Bank", hk ? C_BTN : C_OFF, hk);
    button(B_TRANSFER, 432, y, 400, 120, "Transfer", hk ? C_BTN : C_OFF, hk);
    button(B_WU, 844, y, 400, 120, "Wu", hk ? C_BTN : C_OFF, hk);
    y += 140;
    button(B_SAIL, 20, y, SW - 40, 170, "Set Sail", C_NAVY, true, fBig, C_WHITE);
    y += 190;
    if (game.canRetire()) {
        button(B_RETIRE, 20, y, SW - 40, 120, "Retire a millionaire", C_GOLD);
        y += 140;
    }
    button(B_NEWGAME, SW - 340, SH - 110, 320, 90, "New game", C_OFF, true, fSmall);
}

static void drawEvent() {
    drawHeader();
    int y = drawStatus() + 10;
    const int bh = 560;
    box(20, y, SW - 40, bh, C_PARCH, 6);
    txt(40, y + 10, SW - 80, 70, ev.title, ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_NAVY);
    FillArea(60, y + 84, SW - 120, 4, C_NAVY);
    lines(60, y + 100, SW - 120, ev.text, 58, fMain, C_BLACK);
    y += bh + 30;
    if (ev.kind == tp::EvKind::Question) {
        button(B_YES, 20, y, SW / 2 - 30, 170, "Yes", C_GOOD, true, fBig);
        button(B_NO, SW / 2 + 10, y, SW / 2 - 30, 170, "No", C_BAD, true, fBig);
    } else {
        button(B_CONT, 20, y, SW - 40, 170, "Continue", C_BTN, true, fBig);
    }
}

static void drawPad() {
    drawHeader();
    int y = 110;
    y = lines(30, y, SW - 60, padTitle, 56, fMain, C_NAVY);
    if (!padInfo.empty()) y = lines(30, y + 4, SW - 60, padInfo, 48, fSmall, C_GREY);
    y += 10;
    box(20, y, SW - 40, 150, C_WHITE, 6);
    txt(40, y, SW - 80, 150, commas(padValue), ALIGN_RIGHT | VALIGN_MIDDLE, fBig, C_BLACK);
    y += 160;
    drawToast(y);
    y += 70;
    const int kw = (SW - 40 - 40) / 3, kh = 140;
    const int labels[4][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {-1, 0, -2}};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            const int v = labels[r][c];
            const int kx = 20 + c * (kw + 20), ky = y + r * (kh + 16);
            if (v >= 0) {
                char b[4];
                std::snprintf(b, sizeof b, "%d", v);
                button(B_KEY + v, kx, ky, kw, kh, b, C_BTN, true, fBig);
            } else if (v == -1) {
                button(B_CLR, kx, ky, kw, kh, "Clear", C_OFF);
            } else {
                button(B_DEL, kx, ky, kw, kh, "Del", C_OFF);
            }
        }
    }
    y += 4 * (kh + 16) + 10;
    button(B_MAX, 20, y, kw, 150, padKind == PK_REPAIR ? "All" : "Max", C_GOLD);
    button(B_CANCEL, 20 + kw + 20, y, kw, 150, "Cancel", C_BAD);
    button(B_OK, 20 + 2 * (kw + 20), y, kw, 150, "OK", C_GOOD, true, fBig);
}

static void drawDest() {
    drawHeader();
    txt(0, 130, SW, 90, "Where to, Taipan?", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_NAVY);
    const int bw = (SW - 60) / 2, bh = 190;
    for (int p = 1; p <= 7; p++) {
        const int col = (p - 1) % 2, row = (p - 1) / 2;
        const int x = 20 + col * (bw + 20), y = 250 + row * (bh + 20);
        const bool here = (p == game.st().port);
        char label[48];
        std::snprintf(label, sizeof label, "%d  %s", p, tp::kPortName[p]);
        button(B_DEST + p, x, y, bw, bh, here ? std::string(label) + "\n(here)" : std::string(label), C_BTN, !here);
    }
    button(B_CANCEL, 20 + (bw + 20), 250 + 3 * (bh + 20), bw, bh, "Stay in port", C_BAD);
}

static void drawLorcha(int x, int y, int w, int h) {
    FillArea(x + w / 2 - 5, y + 4, 10, h * 6 / 10, C_BLACK);                 // mast
    FillArea(x + w / 2 + 12, y + 10, w / 2 - 34, h * 4 / 10, C_RED);        // main sail
    FillArea(x + 22, y + h / 4, w / 2 - 40, h * 3 / 10, C_AMBER);           // fore sail
    FillArea(x + 8, y + h * 6 / 10, w - 16, h * 3 / 10, 0x00402810);        // hull
    FillArea(x + 8, y + h * 6 / 10, w - 16, 6, C_BLACK);
}

static void drawBattle() {
    drawHeader();
    const tp::Battle &b = game.battle();
    char buf[96];
    if (!battleDone) {
        std::snprintf(buf, sizeof buf, "%d ship%s attacking, Taipan!", b.numShips, b.numShips == 1 ? "" : "s");
        txt(20, 110, SW - 40, 80, buf, ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_RED);
    } else {
        txt(20, 110, SW - 40, 80, "The battle is over", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_NAVY);
    }
    // Enemy fleet: two rows of five slots.
    const int sw5 = (SW - 40) / 5, sh = 150;
    for (int i = 0; i < 10; i++) {
        const int x = 20 + (i % 5) * sw5, y = 200 + (i / 5) * (sh + 30);
        if (!battleDone && b.onScreen[i] > 0) drawLorcha(x, y, sw5 - 10, sh);
    }
    if (!battleDone && b.numShips > b.numOnScreen)
        txt(SW - 360, 200 + 2 * (sh + 30) - 10, 340, 50, fmt("+ %lld more", b.numShips - b.numOnScreen), ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);

    int y = 200 + 2 * (sh + 30) + 50;
    FillArea(20, y, SW - 40, 4, C_BLACK);
    y += 14;
    const tp::State &s = game.st();
    int sea = game.seaworthiness();
    std::snprintf(buf, sizeof buf, "Seaworthiness: %s (%d%%)", tp::kStatusName[sea / 20 > 5 ? 5 : sea / 20], sea);
    txt(24, y, SW - 48, 60, buf, ALIGN_LEFT | VALIGN_MIDDLE, fMain, sea < 40 ? C_RED : C_BLACK);
    y += 64;
    box(24, y, SW - 48, 36, C_WHITE, 4);
    FillArea(28, y + 4, (SW - 56) * sea / 100, 28, sea < 40 ? C_RED : sea < 70 ? C_AMBER : C_GREEN);
    y += 50;
    std::snprintf(buf, sizeof buf, "We have %d %s", s.guns, s.guns == 1 ? "gun" : "guns");
    txt(24, y, SW - 48, 56, buf, ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
    y += 64;
    box(20, y, SW - 40, 380, C_PARCH, 4);
    int ly = y + 10;
    size_t from = battleLog.size() > 8 ? battleLog.size() - 8 : 0;
    for (size_t i = from; i < battleLog.size(); i++) {
        txt(40, ly, SW - 80, 44, battleLog[i], ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_BLACK);
        ly += 44;
    }
    y += 400;
    if (battleDone) {
        button(B_CONT, 20, y, SW - 40, 170, "Continue", C_BTN, true, fBig);
    } else {
        const int bw = (SW - 80) / 3;
        button(B_FIGHT, 20, y, bw, 170, "Fight", C_BAD, true, fBig);
        button(B_RUN, 40 + bw, y, bw, 170, "Run", C_GOLD, true, fBig);
        button(B_THROW, 60 + 2 * bw, y, bw, 170, "Throw\ncargo", C_BTN, true, fMain);
    }
}

static void drawOver() {
    tp::FinalReport f = game.finalReport();
    int y = 80;
    txt(0, y, SW, 100, f.retired ? "You retire a millionaire!" : "Your final status", ALIGN_CENTER | VALIGN_MIDDLE, fBig, C_NAVY);
    y += 130;
    FillArea(20, y, SW - 40, 4, C_BLACK);
    y += 20;
    std::string s1 = "Net cash:  " + tp::fancy(f.net);
    char buf[160];
    txt(40, y, SW - 80, 70, s1, ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
    y += 76;
    std::snprintf(buf, sizeof buf, "Ship size: %d units with %d guns", f.capacity, f.guns);
    txt(40, y, SW - 80, 70, buf, ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
    y += 76;
    std::snprintf(buf, sizeof buf, "You traded for %d year%s and %d month%s", f.years, f.years == 1 ? "" : "s", f.months,
                  f.months == 1 ? "" : "s");
    txt(40, y, SW - 80, 70, buf, ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
    y += 90;
    std::snprintf(buf, sizeof buf, "Your score is %lld.", f.score);
    box(40, y, SW - 80, 100, C_GOLD, 4);
    txt(40, y, SW - 80, 100, buf, ALIGN_CENTER | VALIGN_MIDDLE, fBig, C_BLACK);
    y += 120;
    if (f.net < 0)
        txt(40, y, SW - 80, 60, "The crew has requested that you stay on shore!", ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_RED);
    else if (f.score < 100)
        txt(40, y, SW - 80, 60, "Have you considered a land based job?", ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_RED);
    y += 80;
    txt(40, y, SW - 80, 60, "Your Rating:", ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_NAVY);
    y += 70;
    static const char *range[5] = {"50,000 and over", "8,000 to 49,999", "1,000 to 7,999", "500 to 999", "less than 500"};
    for (int i = 0; i < 5; i++) {
        const bool me = (i == f.rating);
        FillArea(40, y, SW - 80, 80, me ? C_GOLD : C_PARCH);
        txt(60, y, 560, 80, tp::kRatingName[i], ALIGN_LEFT | VALIGN_MIDDLE, fMain, C_BLACK);
        txt(600, y, SW - 660, 80, range[i], ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_BLACK);
        y += 88;
    }
    button(B_OVER_NEW, 20, SH - 230, SW - 40, 170, "Play again", C_GOOD, true, fBig);
}

static void drawName() {
    txt(0, 80, SW, 90, "Taipan,", ALIGN_CENTER | VALIGN_MIDDLE, fBig, C_NAVY);
    txt(0, 170, SW, 70, "what will you name your firm?", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_BLACK);
    box(20, 270, SW - 40, 140, C_PARCH, 6);
    txt(40, 270, SW - 80, 140, nameText.empty() ? std::string("Taipan & Co.") : nameText, ALIGN_CENTER | VALIGN_MIDDLE, fBig,
        nameText.empty() ? 0x00A0A090 : C_BLACK);

    static const char *rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    const int gap = 8, kh = 130;
    const int kw = (SW - 40 - 9 * gap) / 10;
    int y = 450;
    for (int r = 0; r < 4; r++) {
        const int n = (int)std::string(rows[r]).size();
        const int rowW = n * kw + (n - 1) * gap;
        int x = (SW - rowW) / 2;
        for (int c = 0; c < n; c++) {
            const char ch = rows[r][c];
            char label[2] = {ch, 0};
            button(B_CH + (unsigned char)ch, x, y, kw, kh, label, C_BTN);
            x += kw + gap;
        }
        y += kh + gap;
    }
    y += 10;
    const int bw = (SW - 40 - 3 * gap) / 4;
    button(B_CH + ' ', 20, y, bw, 140, "Space", C_BTN);
    button(B_CH + '&', 20 + (bw + gap), y, bw, 140, "&", C_BTN);
    button(B_CH + '.', 20 + 2 * (bw + gap), y, bw, 140, ".", C_BTN);
    button(B_NAME_DEL, 20 + 3 * (bw + gap), y, bw, 140, "Del", C_OFF);
    y += 160;
    button(B_NAME_CLR, 20, y, bw, 170, "Clear", C_BAD);
    button(B_NAME_DONE, 20 + bw + gap, y, SW - 40 - bw - gap, 170, "Set sail!", C_GOOD, true, fBig);
}

static void drawBank() {
    const tp::State &s = game.st();
    drawHeader();
    int y = drawStatus() + 20;
    txt(0, y, SW, 90, "Hong Kong Bank", ALIGN_CENTER | VALIGN_MIDDLE, fBig, C_NAVY);
    y += 100;
    txt(0, y, SW, 60, "Deposits earn half a percent a month.", ALIGN_CENTER | VALIGN_MIDDLE, fSmall, C_GREY);
    y += 100;
    button(B_DEPOSIT, 20, y, SW - 40, 200, "Deposit", C_GOOD, s.cash > 0, fBig);
    y += 230;
    button(B_WITHDRAW, 20, y, SW - 40, 200, "Withdraw", C_GOLD, s.bank > 0, fBig);
    y += 220;
    drawToast(y);
    button(B_DONE, 20, SH - 230, SW - 40, 170, "Done", C_BTN, true, fBig);
}

static void drawTransfer() {
    const tp::State &s = game.st();
    drawHeader();
    int y = drawStatus() + 10;
    char buf[96];
    std::snprintf(buf, sizeof buf, "Warehouse: %d of %d used", game.warehouseUsed(), tp::kWarehouseCapacity);
    txt(24, y, SW - 48, 56, buf, ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_GREY);
    y += 60;
    txt(300, y, 190, 50, "On ship", ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);
    txt(500, y, 200, 50, "Warehouse", ALIGN_RIGHT | VALIGN_MIDDLE, fSmall, C_GREY);
    y += 50;
    for (int i = 0; i < 4; i++) {
        const int ry = y + i * 150;
        FillArea(20, ry, SW - 40, 140, (i % 2) ? 0x00F4F4F4 : C_PARCH);
        txt(30, ry, 270, 140, tp::kItemName[i], ALIGN_LEFT | VALIGN_MIDDLE, fSmall, C_BLACK);
        txt(300, ry, 190, 140, commas(s.ship[i]), ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_BLACK);
        txt(500, ry, 200, 140, commas(s.wh[i]), ALIGN_RIGHT | VALIGN_MIDDLE, fMain, C_BLACK);
        button(B_TOWH + i, 730, ry + 10, 250, 120, "To\nwarehouse", C_GOOD, s.ship[i] > 0, fSmall);
        button(B_TOSHIP + i, 1000, ry + 10, 244, 120, "To\nship", C_GOLD, s.wh[i] > 0, fSmall);
    }
    y += 4 * 150 + 10;
    drawToast(y);
    button(B_DONE, 20, SH - 230, SW - 40, 170, "Done", C_BTN, true, fBig);
}

static void drawWu() {
    const tp::State &s = game.st();
    drawHeader();
    int y = drawStatus() + 10;
    box(20, y, SW - 40, 330, C_PARCH, 6);
    txt(40, y + 10, SW - 80, 70, "Elder Brother Wu, the moneylender", ALIGN_CENTER | VALIGN_MIDDLE, fMain, C_NAVY);
    FillArea(60, y + 84, SW - 120, 4, C_NAVY);
    std::string t = "You owe him " + commas(s.debt) + ".\nInterest is ten percent a month.\nHe will lend up to twice your cash.";
    lines(60, y + 100, SW - 120, t, 58, fMain, C_BLACK);
    y += 360;
    button(B_REPAY, 20, y, SW - 40, 180, "Repay", C_GOOD, s.cash > 0 && s.debt > 0, fBig);
    y += 200;
    button(B_BORROW, 20, y, SW - 40, 180, "Borrow", C_GOLD, s.cash > 0, fBig);
    y += 190;
    drawToast(y);
    button(B_LEAVE, 20, SH - 230, SW - 40, 170, "Leave", C_BTN, true, fBig);
}

static void render() {
    btns.clear();
    SW = ScreenWidth();
    SH = ScreenHeight();
    ClearScreen();
    switch (scr) {
    case SC_START: drawStart(); break;
    case SC_NAME: drawName(); break;
    case SC_BANK: drawBank(); break;
    case SC_TRANSFER: drawTransfer(); break;
    case SC_WU: drawWu(); break;
    case SC_THROW: drawThrow(); break;
    case SC_PORT: drawPort(); break;
    case SC_EVENT: drawEvent(); break;
    case SC_PAD: drawPad(); break;
    case SC_DEST: drawDest(); break;
    case SC_BATTLE: drawBattle(); break;
    case SC_OVER: drawOver(); break;
    }
    if (scr != lastScr || ++partials >= 10) {
        FullUpdate();  // new screen (or periodically): clear ghosting
        partials = 0;
    } else {
        PartialUpdate(0, 0, SW, SH);
    }
    lastScr = scr;
}

// ------------------------------------------------------------------ taps
// One battle round with the given orders; updates the log and flags the end of the battle.
static void doRound(int orders, int throwItem, long long throwAmt) {
    tp::RoundResult rr = game.battleRound(orders, throwItem, throwAmt);
    battleLog = rr.lines;
    if (rr.status != tp::BattleStatus::Continue) {
        battleDone = true;
        const char *end = rr.status == tp::BattleStatus::Won ? "We won!"
                          : rr.status == tp::BattleStatus::Fled ? "We got away."
                          : rr.status == tp::BattleStatus::Lost ? "Our ship is lost!"
                                                                : "Li Yuen's pirates are upon us!";
        battleLog.push_back(end);
    }
}

static void padOk() {
    if (padValue > padMax) {
        toast = "Too many, Taipan! Maximum is " + commas(padMax);
        return;
    }
    if (padKind == PK_REPAIR) {
        game.answerNumber(padValue);
        pump();
        return;
    }
    if (padValue == 0) {
        scr = padReturn;
        return;
    }
    if (padKind == PK_THROW) {
        doRound(tp::ORD_THROW, padItem, padValue);
        scr = SC_BATTLE;
        return;
    }
    const std::string name = (padItem >= 0 && padItem < 4) ? tp::kItemName[padItem] : "";
    const std::string n = commas(padValue);
    tp::Result r = tp::Result::BadPhase;
    std::string done;
    switch (padKind) {
    case PK_BUY: r = game.buy(padItem, padValue); done = "Bought " + n + " " + name; break;
    case PK_SELL: r = game.sell(padItem, padValue); done = "Sold " + n + " " + name; break;
    case PK_DEPOSIT: r = game.deposit(padValue); done = "Deposited " + n; break;
    case PK_WITHDRAW: r = game.withdraw(padValue); done = "Withdrew " + n; break;
    case PK_TOWH: r = game.toWarehouse(padItem, padValue); done = "Moved " + n + " " + name + " to the warehouse"; break;
    case PK_TOSHIP: r = game.toShip(padItem, padValue); done = "Moved " + n + " " + name + " aboard"; break;
    case PK_REPAY: r = game.wuRepay(padValue); done = "Repaid " + n; break;
    case PK_BORROW: r = game.wuBorrow(padValue); done = "Borrowed " + n; break;
    case PK_REPAIR:
    case PK_THROW: break;
    }
    if (r != tp::Result::Ok) {
        toast = "That can't be done, Taipan!";
        return;
    }
    toast = done;
    if (game.st().hold < 0) toast += " (overloaded!)";
    scr = padReturn;
    saveGame();  // no-op unless in port with nothing pending
}

static void typeChar(char ch) {
    if (nameText.size() >= kMaxName) return;
    if (ch == ' ' && (nameText.empty() || nameText[nameText.size() - 1] == ' ')) return;
    // Auto-capitalize the start of each word, lowercase the rest.
    const bool word = nameText.empty() || nameText[nameText.size() - 1] == ' ';
    if (ch >= 'A' && ch <= 'Z' && !word) ch = (char)(ch - 'A' + 'a');
    nameText += ch;
}

static void onButton(int id) {
    toast.clear();
    if (id >= B_KEY && id <= B_KEY + 9) {
        padValue = padValue * 10 + (id - B_KEY);
        if (padValue > 999999999LL) padValue = 999999999LL;
        return;
    }
    if (id >= B_CH && id < B_CH + 128) {
        typeChar((char)(id - B_CH));
        return;
    }
    if (id >= B_BUY && id < B_BUY + 4) {
        const int i = id - B_BUY;
        if (game.maxBuy(i) == 0)
            toast = std::string("You can't afford any ") + tp::kItemName[i] + ", Taipan.";
        else
            openPad(PK_BUY, i, game.maxBuy(i), SC_PORT);
        return;
    }
    if (id >= B_SELL && id < B_SELL + 4) {
        const int i = id - B_SELL;
        if (game.st().ship[i] == 0)
            toast = std::string("You have no ") + tp::kItemName[i] + ", Taipan.";
        else
            openPad(PK_SELL, i, game.st().ship[i], SC_PORT);
        return;
    }
    if (id >= B_TOWH && id < B_TOWH + 4) {
        const int i = id - B_TOWH;
        const long long room = tp::kWarehouseCapacity - game.warehouseUsed();
        const long long m = game.st().ship[i] < room ? game.st().ship[i] : room;
        if (m <= 0)
            toast = "Your warehouse is full, Taipan!";
        else
            openPad(PK_TOWH, i, m, SC_TRANSFER);
        return;
    }
    if (id >= B_THROW_ITEM && id < B_THROW_ITEM + 4) {
        const int i = id - B_THROW_ITEM;
        openPad(PK_THROW, i, game.st().ship[i], SC_THROW);
        return;
    }
    if (id >= B_TOSHIP && id < B_TOSHIP + 4) {
        const int i = id - B_TOSHIP;
        openPad(PK_TOSHIP, i, game.st().wh[i], SC_TRANSFER);
        return;
    }
    if (id > B_DEST && id <= B_DEST + 7) {
        tp::Result r = game.sail(id - B_DEST);
        if (r == tp::Result::Overloaded) {
            toast = "Your ship is overloaded, Taipan!!";
            scr = SC_PORT;
        } else if (r == tp::Result::Ok) {
            pump();
        }
        return;
    }
    switch (id) {
    case B_START_CASH:
    case B_START_GUNS:
        pendingCash = (id == B_START_CASH);
        nameText.clear();
        scr = SC_NAME;
        break;
    case B_NAME_DEL:
        if (!nameText.empty()) nameText.erase(nameText.size() - 1);
        break;
    case B_NAME_CLR: nameText.clear(); break;
    case B_NAME_DONE: {
        while (!nameText.empty() && nameText[nameText.size() - 1] == ' ') nameText.erase(nameText.size() - 1);
        startNew(pendingCash, nameText.empty() ? std::string("Taipan & Co.") : nameText);
        break;
    }
    case B_CONTINUE:
        if (loadInto(game)) pump();
        break;
    case B_NEWGAME: scr = SC_START; break;
    case B_OVER_NEW: scr = SC_START; break;
    case B_SAIL: scr = SC_DEST; break;
    case B_CANCEL:
        if (scr == SC_PAD && padKind == PK_REPAIR) {
            game.answerNumber(0);
            pump();
        } else if (scr == SC_PAD) {
            scr = padReturn;
        } else {
            scr = SC_PORT;
        }
        break;
    case B_BANK: scr = SC_BANK; break;
    case B_TRANSFER: scr = SC_TRANSFER; break;
    case B_WU:
        if (game.wuEnter())
            scr = SC_WU;
        else
            pump();  // Wu offers a bailout instead
        break;
    case B_DONE:
        scr = SC_PORT;
        saveGame();
        break;
    case B_DEPOSIT:
        if (game.st().cash > 0) openPad(PK_DEPOSIT, -1, game.st().cash, SC_BANK);
        break;
    case B_WITHDRAW:
        if (game.st().bank > 0) openPad(PK_WITHDRAW, -1, game.st().bank, SC_BANK);
        break;
    case B_REPAY: {
        const long long m = game.st().cash < game.st().debt ? game.st().cash : game.st().debt;
        if (m > 0) openPad(PK_REPAY, -1, m, SC_WU);
        break;
    }
    case B_BORROW:
        if (game.st().cash > 0) openPad(PK_BORROW, -1, game.st().cash * 2, SC_WU);
        break;
    case B_LEAVE:
        game.wuLeave();  // may queue the cutthroat robbery
        pump();
        break;
    case B_RETIRE:
        game.retire();
        pump();
        break;
    case B_YES: game.answerYes(true); pump(); break;
    case B_NO: game.answerYes(false); pump(); break;
    case B_CONT:
        if (scr == SC_BATTLE) {
            inBattle = false;
            pump();
        } else {
            const bool wu = (ev.kind == tp::EvKind::Action && ev.id == tp::EvId::WuScreen);
            game.ack();
            if (wu)
                scr = SC_WU;
            else
                pump();
        }
        break;
    case B_CLR: padValue = 0; break;
    case B_DEL: padValue /= 10; break;
    case B_MAX: padValue = padMax; break;
    case B_OK: padOk(); break;
    case B_FIGHT: doRound(tp::ORD_FIGHT, 4, -1); break;
    case B_RUN: doRound(tp::ORD_RUN, 4, -1); break;
    case B_THROW: {
        const tp::State &s = game.st();
        if (s.ship[0] + s.ship[1] + s.ship[2] + s.ship[3] == 0)
            battleLog.assign(1, "You have no cargo to throw, Taipan!");
        else
            scr = SC_THROW;
        break;
    }
    case B_THROW_ALL:
        doRound(tp::ORD_THROW, 4, -1);
        scr = SC_BATTLE;
        break;
    case B_THROW_BACK: scr = SC_BATTLE; break;
    }
}

static int handler(int type, int par1, int par2) {
    switch (type) {
    case EVT_INIT:
        SetOrientation(0);             // lock portrait
        SetPanelType(PANEL_DISABLED);  // no system status bar overlapping the layout
        fMain = OpenFont("LiberationSans", 44, 1);
        fSmall = OpenFont("LiberationSans", 36, 1);
        fBig = OpenFont("LiberationSans", 72, 1);
        if (loadInto(game)) {
            pump();
            toast = "Game resumed";
        } else {
            scr = SC_START;
        }
        break;
    case EVT_ORIENTATION:
    case EVT_SHOW:
        lastScr = (Screen)-1;  // force a full refresh
        render();
        break;
    case EVT_POINTERUP:
        for (size_t i = 0; i < btns.size(); i++) {
            const Btn &b = btns[i];
            if (par1 >= b.x && par1 < b.x + b.w && par2 >= b.y && par2 < b.y + b.h) {
                onButton(b.id);
                render();
                break;
            }
        }
        break;
    case EVT_KEYPRESS:
        if (par1 == IV_KEY_PREV || par1 == IV_KEY_HOME || par1 == IV_KEY_BACK) CloseApp();
        break;
    case EVT_EXIT:
        saveGame();
        if (fMain) CloseFont(fMain);
        if (fSmall) CloseFont(fSmall);
        if (fBig) CloseFont(fBig);
        break;
    }
    return 0;
}

int main(int, char **) {
    InkViewMain(handler);
    return 0;
}
