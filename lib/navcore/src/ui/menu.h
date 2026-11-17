// Generic menu engine usable with 2 buttons (+ optional touch).
//   KEY short  : next item           | in edit: +step
//   KEY long   : enter / select      | in edit: confirm
//   BOOT short : previous item       | in edit: -step
//   BOOT long  : back / close        | in edit: cancel
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"

namespace ui {

enum class Key { None, NextShort, NextLong, PrevShort, PrevLong };

struct MenuItem {
    enum Kind { Action, Submenu, Number, Toggle, Text } kind = Action;
    std::string label;
    std::function<std::string()> value;  // optional right-hand value text
    std::function<void()> action;        // Action / Toggle
    // Number
    std::function<float()> get;
    std::function<void(float)> set;
    float min = 0, max = 100, step = 1;
    int decimals = 0;
    std::string unit;
    std::vector<MenuItem> children;      // Submenu
    bool closeAfter = false;             // Action closes the whole menu
    std::string text;                    // Text: paragraphs separated by '\n'

    static MenuItem act(const std::string& l, std::function<void()> f, bool close = false) {
        MenuItem m;
        m.kind = Action;
        m.label = l;
        m.action = std::move(f);
        m.closeAfter = close;
        return m;
    }
    static MenuItem toggle(const std::string& l, std::function<std::string()> v, std::function<void()> f) {
        MenuItem m;
        m.kind = Toggle;
        m.label = l;
        m.value = std::move(v);
        m.action = std::move(f);
        return m;
    }
    static MenuItem num(const std::string& l, std::function<float()> g, std::function<void(float)> s, float mn,
                        float mx, float st, int dec, const std::string& unit) {
        MenuItem m;
        m.kind = Number;
        m.label = l;
        m.get = std::move(g);
        m.set = std::move(s);
        m.min = mn;
        m.max = mx;
        m.step = st;
        m.decimals = dec;
        m.unit = unit;
        return m;
    }
    static MenuItem page(const std::string& l, const std::string& t) {
        MenuItem m;
        m.kind = Text;
        m.label = l;
        m.text = t;
        return m;
    }
    static MenuItem sub(const std::string& l, std::vector<MenuItem> c) {
        MenuItem m;
        m.kind = Submenu;
        m.label = l;
        m.children = std::move(c);
        return m;
    }
};

class Menu {
public:
    void open(std::shared_ptr<MenuItem> root);
    void close();
    bool isOpen() const { return open_; }
    // Returns true if something changed that needs saving
    bool handleKey(Key k);
    bool handleTap(int x, int y);
    void draw(Canvas& c) const;
    // Short description of where the user is (for crash breadcrumbs), e.g. "MENU>Paliwo>[Zużycie] edycja"
    std::string describe() const;
    bool consumeDirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    struct Level {
        MenuItem* item;
        int sel = 0;
        int scroll = 0;
    };
    MenuItem* cur() const { return stack_.back().item; }
    std::string fmtNum(const MenuItem& m, float v) const;
    void activate(int idx);
    void adjustScroll();
    void openText(const MenuItem& it);
    void drawText(Canvas& c) const;
    struct TextLine {
        std::string s;
        bool indent;
    };
    bool viewing_ = false;
    std::vector<TextLine> lines_;
    int page_ = 0;
    std::vector<int> pageStart_;  // first line index of every page (pages break between paragraphs)
    std::string textTitle_;
    int pages() const;
    std::shared_ptr<MenuItem> root_;
    std::vector<Level> stack_;
    bool open_ = false;
    bool editing_ = false;
    float editVal_ = 0;
    bool dirty_ = false;
    static constexpr int kRows = 8;
public:
    static constexpr int kTextLines = 14;  // lines per help page
    // Word-wrap a help text (public for the layout checker)
    static std::vector<std::pair<std::string, bool>> wrap(const std::string& text, int width, int indentPx);
    // Page break positions for a text (public for the layout checker)
    static std::vector<int> paginate(const std::string& text, int width, int indentPx, int& totalLines);
};

}  // namespace ui
