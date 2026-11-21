#include "menu.h"
#include "../i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

static constexpr int kTop = 44, kRowH = 54, kListX = 20;

static constexpr int kTextTop = 44, kTextLineH = 30, kIndent = 26;

std::vector<std::pair<std::string, bool>> Menu::wrap(const std::string& text, int width, int indentPx) {
    std::vector<std::pair<std::string, bool>> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        std::string para = text.substr(start, nl - start);
        start = nl + 1;
        bool item = para.rfind("– ", 0) == 0;
        std::string line;
        bool cont = false;
        size_t p = 0;
        while (p <= para.size()) {
            size_t sp = para.find(' ', p);
            if (sp == std::string::npos) sp = para.size();
            std::string word = para.substr(p, sp - p);
            p = sp + 1;
            std::string cand = line.empty() ? word : line + " " + word;
            int avail = width - ((cont && item) ? indentPx : 0);
            if (!line.empty() && Canvas::textWidth(cand.c_str(), font_sb) > avail) {
                out.push_back({line, cont && item});
                cont = true;
                line = word;
            } else {
                line = cand;
            }
            if (sp == para.size()) break;
        }
        out.push_back({line, cont && item});
        if (nl == text.size()) break;
    }
    return out;
}

std::vector<int> Menu::paginate(const std::string& text, int width, int indentPx, int& total) {
    // wrap paragraph by paragraph; move a paragraph to the next page if it would be split
    // (unless it is longer than a page anyway)
    std::vector<int> starts{0};
    total = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        int n = (int)wrap(text.substr(start, nl - start), width, indentPx).size();
        int used = total - starts.back();
        if (used > 0 && used + n > kTextLines && n <= kTextLines) starts.push_back(total);
        while (total + n - starts.back() > kTextLines) starts.push_back(starts.back() + kTextLines);
        total += n;
        if (nl == text.size()) break;
        start = nl + 1;
    }
    return starts;
}

void Menu::openText(const MenuItem& it) {
    lines_.clear();
    const int width = W - 2 * kListX - 10;
    for (auto& l : wrap(it.text, width, kIndent)) lines_.push_back({l.first, l.second});
    int total = 0;
    pageStart_ = paginate(it.text, width, kIndent, total);
    page_ = 0;
    textTitle_ = it.label;
    viewing_ = true;
}

int Menu::pages() const { return (int)pageStart_.size(); }

void Menu::drawText(Canvas& c) const {
    c.fillRect(0, 0, W, H, WHITE);
    c.fillRect(0, 0, W, 40, BLACK);
    c.text(12, 30, TR("POMOC › ", "HELP › ") + textTitle_, font_sb, WHITE);
    char b[24];
    std::snprintf(b, sizeof b, "%d / %d", page_ + 1, pages());
    c.text(W - 12, 30, b, font_sb, WHITE, RIGHT);
    int first = pageStart_[page_];
    int last = page_ + 1 < (int)pageStart_.size() ? pageStart_[page_ + 1] : (int)lines_.size();
    for (int i = 0; i < kTextLines && first + i < last; i++) {
        const auto& l = lines_[first + i];
        c.text(kListX + (l.indent ? kIndent : 0), kTextTop + 30 + i * kTextLineH, l.s, font_sb, BLACK);
    }
    c.fillRect(0, H - 34, W, 34, WHITE);
    c.hline(0, H - 34, W, BLACK);
    c.text(12, H - 9, TR("KLAWISZ: następna strona   BOOT: poprzednia   przytrzymaj: powrót", "KEY: next page   BOOT: previous   hold: back"), font_s, BLACK);
}

void Menu::open(std::shared_ptr<MenuItem> root) {
    root_ = std::move(root);
    stack_.clear();
    stack_.push_back({root_.get(), 0, 0});
    open_ = true;
    editing_ = false;
}

std::string Menu::describe() const {
    if (!open_) return "";
    std::string s = "MENU";
    for (size_t i = 1; i < stack_.size(); i++) s += ">" + stack_[i].item->label;
    const Level& l = stack_.back();
    const auto& ch = l.item->children;
    if (l.sel >= 0 && l.sel < (int)ch.size()) s += ">[" + ch[l.sel].label + "]";
    if (editing_) s += TR(" edycja", " edit");
    if (viewing_) s += TR(" pomoc str.", " help p.") + std::to_string(page_ + 1);
    return s;
}

void Menu::close() {
    open_ = false;
    editing_ = false;
    viewing_ = false;
    stack_.clear();
}

std::string Menu::fmtNum(const MenuItem& m, float v) const {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", m.decimals, (double)v);
    return std::string(b) + (m.unit.empty() ? "" : " " + m.unit);
}

void Menu::adjustScroll() {
    Level& l = stack_.back();
    if (l.sel < l.scroll) l.scroll = l.sel;
    if (l.sel >= l.scroll + kRows) l.scroll = l.sel - kRows + 1;
}

void Menu::scrollPage(int dir) {
    Level& l = stack_.back();
    int n = (int)cur()->children.size();
    l.scroll = std::max(0, std::min(n - kRows, l.scroll + dir * (kRows - 1)));
    l.sel = std::max(l.scroll, std::min(l.scroll + kRows - 1, l.sel));  // keep the cursor visible for the buttons
}

void Menu::activate(int idx) {
    MenuItem* m = cur();
    if (idx < 0 || idx >= (int)m->children.size()) return;
    MenuItem& it = m->children[idx];
    switch (it.kind) {
        case MenuItem::Submenu:
            stack_.push_back({&it, 0, 0});
            break;
        case MenuItem::Action:
            if (it.action) it.action();
            dirty_ = true;
            if (it.closeAfter) close();
            break;
        case MenuItem::Toggle:
            if (it.action) it.action();
            dirty_ = true;
            break;
        case MenuItem::Number:
            editing_ = true;
            editVal_ = it.get ? it.get() : 0;
            break;
        case MenuItem::Text:
            openText(it);
            break;
    }
}

bool Menu::handleKey(Key k) {
    if (!open_ || k == Key::None) return false;
    if (viewing_) {
        switch (k) {
            case Key::NextShort: if (page_ + 1 < pages()) page_++; break;
            case Key::PrevShort: if (page_ > 0) page_--; break;
            case Key::NextLong:
            case Key::PrevLong: viewing_ = false; break;
            default: break;
        }
        return true;
    }
    Level& l = stack_.back();
    int n = (int)cur()->children.size();
    if (editing_) {
        MenuItem& it = cur()->children[l.sel];
        switch (k) {
            case Key::NextShort: editVal_ = std::min(it.max, editVal_ + it.step); break;
            case Key::PrevShort: editVal_ = std::max(it.min, editVal_ - it.step); break;
            case Key::NextLong:
                if (it.set) it.set(editVal_);
                dirty_ = true;
                editing_ = false;
                break;
            case Key::PrevLong: editing_ = false; break;
            default: break;
        }
        return true;
    }
    switch (k) {
        case Key::NextShort: l.sel = n ? (l.sel + 1) % n : 0; adjustScroll(); break;
        case Key::PrevShort: l.sel = n ? (l.sel + n - 1) % n : 0; adjustScroll(); break;
        case Key::NextLong: activate(l.sel); break;
        case Key::PrevLong:
            if (stack_.size() > 1) stack_.pop_back();
            else close();
            break;
        default: break;
    }
    return true;
}

bool Menu::handleTap(int x, int y) {
    if (!open_) return false;
    if (viewing_) {
        if (y < kTop) viewing_ = false;
        else if (x >= W / 2) handleKey(Key::NextShort);
        else handleKey(Key::PrevShort);
        return true;
    }
    if (editing_) {
        Level& l = stack_.back();
        MenuItem& it = cur()->children[l.sel];
        if (y > 400) {
            if (x < 240) editVal_ = std::max(it.min, editVal_ - it.step * 10);
            else if (x < 480) editVal_ = std::max(it.min, editVal_ - it.step);
            else if (x < 720) editVal_ = std::min(it.max, editVal_ + it.step);
            else editVal_ = std::min(it.max, editVal_ + it.step * 10);
        } else if (y > 300) {
            if (x < W / 2) handleKey(Key::PrevLong);
            else handleKey(Key::NextLong);
        }
        return true;
    }
    if (y < kTop) {  // status/title bar: back
        handleKey(Key::PrevLong);
        return true;
    }
    Level& l = stack_.back();
    if (scrollable() && x >= W - kArrowW) {  // touch scroll column: upper half = page up, lower = page down
        if (y < kTop + 6 + kRows * kRowH) scrollPage(y < kTop + 6 + kRows * kRowH / 2 ? -1 : 1);
        return true;
    }
    int row = (y - kTop - 6) / kRowH;
    if (row < 0 || row >= kRows) return true;
    int idx = l.scroll + row;
    if (idx < (int)cur()->children.size()) {
        l.sel = idx;
        activate(idx);
    }
    return true;
}

void Menu::draw(Canvas& c) const {
    if (!open_) return;
    if (viewing_) {
        drawText(c);
        return;
    }
    c.fillRect(0, 0, W, H, WHITE);
    // title bar with breadcrumb
    std::string title = "MENU";
    for (size_t i = 1; i < stack_.size(); i++) title += " › " + stack_[i].item->label;
    c.fillRect(0, 0, W, 40, BLACK);
    c.text(12, 30, title, font_sb, WHITE);
    c.text(W - 12, 30, TR("◄ wstecz", "◄ back"), font_sb, WHITE, RIGHT);

    const Level& l = stack_.back();
    const auto& items = cur()->children;
    if (editing_) {
        const MenuItem& it = items[l.sel];
        c.text(W / 2, 120, it.label, font_m, BLACK, CENTER);
        c.text(W / 2, 250, fmtNum(it, editVal_), font_xl, BLACK, CENTER);
        c.roundRect(40, 310, 420, 70, 35, BLACK, 3);
        c.text(250, 358, TR("ANULUJ (BOOT długo)", "CANCEL (BOOT long)"), font_sb, BLACK, CENTER);
        c.fillRoundRect(500, 310, 420, 70, 35, BLACK);
        c.text(710, 358, TR("ZAPISZ (KLAWISZ długo)", "SAVE (KEY long)"), font_sb, WHITE, CENTER);
        const char* bl[4] = {"– –", "–", "+", "+ +"};
        for (int i = 0; i < 4; i++) {  // rounded buttons with a 4 px shadow
            c.fillRoundRect(i * 240 + 14, 414, 220, 110, 16, BLACK);
            c.fillRoundRect(i * 240 + 10, 410, 220, 110, 16, WHITE);
            c.roundRect(i * 240 + 10, 410, 220, 110, 16, BLACK, 3);
            c.text(i * 240 + 120, 482, bl[i], font_l, BLACK, CENTER);
        }
        return;
    }
    const bool sc = scrollable();
    const int right = sc ? W - kArrowW - 8 : W;  // right edge of the list
    for (int r = 0; r < kRows; r++) {
        int idx = l.scroll + r;
        if (idx >= (int)items.size()) break;
        const MenuItem& it = items[idx];
        int y = kTop + 6 + r * kRowH;
        std::string val;
        if (it.kind == MenuItem::Number && it.get) val = fmtNum(it, it.get());
        else if (it.value) val = it.value();
        int vw = val.empty() ? 0 : c.text(right - 20, y + 38, val, font_m, BLACK, RIGHT) + 24;
        std::string lab = it.label;
        std::string suffix = it.kind == MenuItem::Submenu ? "  ›" : "";
        int maxW = right - 2 * kListX - vw - Canvas::textWidth(suffix.c_str(), font_m);
        if (Canvas::textWidth(lab.c_str(), font_m) > maxW) {
            while (!lab.empty() && Canvas::textWidth((lab + ".").c_str(), font_m) > maxW) {
                size_t i = lab.size() - 1;
                while (i > 0 && ((unsigned char)lab[i] & 0xC0) == 0x80) i--;
                lab.erase(i);
            }
            lab += ".";
        }
        c.text(kListX, y + 38, lab + suffix, font_m, BLACK);
        c.hline(kListX, y + kRowH - 2, right - 2 * kListX, BLACK);
        if (idx == l.sel) c.invertRoundRect(4, y, right - 8, kRowH - 3, 12);
    }
    if (sc) {
        // touch scroll column: page up / page down buttons with the position bar between them
        const int x0 = W - kArrowW, y0 = kTop + 6, hh = kRows * kRowH;
        const int bh = hh / 2 - 16, cx = x0 + kArrowW / 2;
        int n = (int)items.size();
        bool canUp = l.scroll > 0, canDown = l.scroll + kRows < n;
        for (int k = 0; k < 2; k++) {
            int by = k == 0 ? y0 : y0 + hh - bh;
            bool en = k == 0 ? canUp : canDown;
            c.roundRect(x0 + 2, by, kArrowW - 6, bh, 14, BLACK, en ? 3 : 1);
            int ty = by + bh / 2, s = 22;
            Pt tri[3] = {{cx - s, k == 0 ? ty + s / 2 : ty - s / 2}, {cx + s, k == 0 ? ty + s / 2 : ty - s / 2},
                         {cx, k == 0 ? ty - s / 2 - 4 : ty + s / 2 + 4}};
            if (en) c.fillPolygon(tri, 3, BLACK);
            else c.polyline(tri, 3, BLACK, 1, true);
        }
        // position bar between the buttons: the visible part of the list
        int gy = y0 + bh + 8, gw = kArrowW - 10;
        c.hline(x0 + 4, gy + 8, gw, BLACK);
        int p0 = gw * l.scroll / n, p1 = gw * std::min(n, l.scroll + kRows) / n;
        c.fillRect(x0 + 4 + p0, gy + 3, std::max(4, p1 - p0), 11, BLACK);
    }
    c.fillRect(0, H - 34, W, 34, WHITE);
    c.hline(0, H - 34, W, BLACK);
    c.text(12, H - 9, TR("KLAWISZ: dalej / przytrzymaj = wybierz     BOOT: wstecz / przytrzymaj = wyjdź", "KEY: next / hold = select     BOOT: back / hold = exit"), font_s, BLACK);
}

}  // namespace ui
