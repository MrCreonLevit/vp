// Viewpoints (MIT License) - See LICENSE file
#include "CommandParser.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace {

std::string Lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void Trim(std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        b--;
    s = s.substr(a, b - a);
}

void StripTimestamp(std::string& s) {
    // [HH:MM:SS.mmm]
    if (s.size() < 14 || s[0] != '[' || s[3] != ':' || s[6] != ':' ||
        s[9] != '.' || s[13] != ']')
        return;
    for (int i : {1, 2, 4, 5, 7, 8, 10, 11, 12}) {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return;
    }
    s.erase(0, 14);
    Trim(s);
}

struct Scan {
    std::string s;
    size_t i = 0;

    void Skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            i++;
    }
    bool Eof() { Skip(); return i >= s.size(); }
    char At(size_t k) const {
        if (k >= s.size()) return 0;
        return static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
    }
    bool Starts(std::string_view lit) {
        Skip();
        for (size_t k = 0; k < lit.size(); k++) {
            char want = static_cast<char>(std::tolower(static_cast<unsigned char>(lit[k])));
            if (At(i + k) != want)
                return false;
        }
        if (!lit.empty() && std::isalnum(static_cast<unsigned char>(lit.back()))) {
            size_t n = i + lit.size();
            if (n < s.size() && std::isalnum(static_cast<unsigned char>(s[n])))
                return false;
        }
        return true;
    }
    bool Eat(std::string_view lit) {
        if (!Starts(lit)) return false;
        i += lit.size();
        return true;
    }
    // "on" immediately before a colon is the location preposition left behind
    // after the plot phrase was removed ("Grid lines on: on").
    void EatPrepositionOn() {
        Skip();
        size_t save = i;
        if (!Eat("on")) return;
        Skip();
        if (i < s.size() && s[i] == ':')
            return;
        i = save;
    }
    bool Number(float& out) {
        Skip();
        if (i >= s.size()) return false;
        char* end = nullptr;
        const char* start = s.c_str() + i;
        float v = std::strtof(start, &end);
        if (end == start) return false;
        i = static_cast<size_t>(end - s.c_str());
        out = v;
        Eat("\u00B0");
        if (!Eof() && Starts("degrees")) Eat("degrees");
        else if (!Eof() && Starts("deg")) Eat("deg");
        return true;
    }
    bool Int(int& out) {
        float f;
        size_t save = i;
        if (!Number(f)) return false;
        if (f != static_cast<float>(static_cast<int>(f))) { i = save; return false; }
        out = static_cast<int>(f);
        return true;
    }
    std::string Rest() {
        Skip();
        std::string r = s.substr(i);
        Trim(r);
        i = s.size();
        return r;
    }
};

bool EqualsCi(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Remove one plot location. Returns false if a "plot(" phrase is malformed.
bool StripLocation(std::string& s, Command& cmd) {
    std::string low = Lower(s);
    auto plotPos = low.find("plot(");
    if (plotPos != std::string::npos) {
        size_t p = plotPos + 5;
        char* end = nullptr;
        long row = std::strtol(s.c_str() + p, &end, 10);
        if (end == s.c_str() + p) return false;
        while (*end && std::isspace(static_cast<unsigned char>(*end))) end++;
        if (*end != ',') return false;
        end++;
        while (*end && std::isspace(static_cast<unsigned char>(*end))) end++;
        char* end2 = nullptr;
        long col = std::strtol(end, &end2, 10);
        if (end2 == end) return false;
        while (*end2 && std::isspace(static_cast<unsigned char>(*end2))) end2++;
        if (*end2 != ')') return false;
        size_t begin = plotPos;
        size_t close = static_cast<size_t>(end2 - s.c_str());
        if (begin > 0 && s[begin - 1] == ' ') begin--;
        s.erase(begin, close + 1 - begin);
        Trim(s);
        if (Lower(s).find("plot(") != std::string::npos) return false;
        cmd.scope = CommandScope::Plot;
        cmd.row = static_cast<int>(row);
        cmd.col = static_cast<int>(col);
        return true;
    }
    auto paren = low.find("(all plots)");
    if (paren != std::string::npos) {
        size_t begin = paren;
        if (begin > 0 && s[begin - 1] == ' ') begin--;
        s.erase(begin, paren + std::string("(all plots)").size() - begin);
        Trim(s);
        cmd.scope = CommandScope::All;
        return true;
    }
    auto onAll = low.find(" on all plots");
    if (onAll != std::string::npos) {
        size_t after = onAll + std::string(" on all plots").size();
        if (after == low.size() || low[after] == ':' || std::isspace(static_cast<unsigned char>(low[after]))) {
            s.erase(onAll, std::string(" on all plots").size());
            Trim(s);
            cmd.scope = CommandScope::All;
            return true;
        }
    }
    const std::string tail = " all plots";
    if (low.size() >= tail.size() &&
        low.compare(low.size() - tail.size(), tail.size(), tail) == 0) {
        s.erase(s.size() - tail.size());
        Trim(s);
        cmd.scope = CommandScope::All;
    }
    return true;
}

bool OnOff(Scan& sc, bool& on) {
    if (sc.Eat("on")) { on = true; return sc.Eof(); }
    if (sc.Eat("off")) { on = false; return sc.Eof(); }
    return false;
}

bool FlagSentence(Scan& sc, std::string_view a, std::string_view b, bool& on) {
    if (!sc.Eat(a) || !sc.Eat(b)) return false;
    sc.EatPrepositionOn();
    if (!sc.Eat(":")) return false;
    return OnOff(sc, on);
}

bool TakeNamed(Scan& sc, std::string_view key, std::string& out) {
    if (!sc.Eat(key)) return false;
    sc.Skip();
    size_t start = sc.i;
    auto lowRest = Lower(sc.s.substr(sc.i));
    size_t cut = lowRest.find(", y=");
    if (key == "x=" && cut != std::string::npos) {
        out = sc.s.substr(start, cut);
        Trim(out);
        sc.i = start + cut;
        sc.Eat(",");
        return true;
    }
    // x= value may also end at ", norm=" for z, handled elsewhere.
    out = sc.Rest();
    return true;
}

bool ParseAxis(Scan& sc, Command& cmd) {
    if (!sc.Eat("axis")) return false;
    sc.EatPrepositionOn();
    if (!sc.Eat(":")) return false;
    sc.Skip();
    if (sc.Starts("x=")) {
        if (!TakeNamed(sc, "x=", cmd.xName) || cmd.xName.empty()) return false;
        cmd.hasX = true;
        sc.Skip();
        if (sc.Eat("y=")) {
            cmd.yName = sc.Rest();
            if (cmd.yName.empty()) return false;
            cmd.hasY = true;
        }
        return sc.Eof() && (cmd.hasX || cmd.hasY);
    }
    if (sc.Eat("y=")) {
        cmd.yName = sc.Rest();
        cmd.hasY = !cmd.yName.empty();
        return cmd.hasY && sc.Eof();
    }
    return false;
}

bool ParseLock(Scan& sc, Command& cmd) {
    if (!sc.Eat("axis") || !sc.Eat("lock")) return false;
    sc.EatPrepositionOn();
    if (!sc.Eat(":")) return false;
    while (!sc.Eof()) {
        if (sc.Eat("x=")) {
            if (sc.Eat("unlocked")) cmd.on = false;
            else if (sc.Eat("locked")) cmd.on = true;
            else return false;
            cmd.hasX = true;
        } else if (sc.Eat("y=")) {
            if (sc.Eat("unlocked")) cmd.reversed = false;
            else if (sc.Eat("locked")) cmd.reversed = true;
            else return false;
            cmd.hasY = true;
        } else {
            return false;
        }
    }
    return cmd.hasX || cmd.hasY;
}

bool ParseNorm(Scan& sc, Command& cmd) {
    if (!sc.Eat("normalization")) return false;
    sc.EatPrepositionOn();
    if (!sc.Eat(":")) return false;
    sc.Skip();
    if (sc.Starts("x=")) {
        if (!TakeNamed(sc, "x=", cmd.xName) || cmd.xName.empty()) return false;
        cmd.hasX = true;
        if (sc.Eat("y=")) {
            cmd.yName = sc.Rest();
            if (cmd.yName.empty()) return false;
            cmd.hasY = true;
        }
        return sc.Eof();
    }
    if (sc.Eat("y=")) {
        cmd.yName = sc.Rest();
        cmd.hasY = !cmd.yName.empty();
        return cmd.hasY;
    }
    return false;
}

bool ParseZ(Scan& sc, Command& cmd) {
    if (!sc.Eat("z-axis")) return false;
    sc.EatPrepositionOn();
    if (!sc.Eat(":")) return false;
    std::string rest = sc.Rest();
    std::string low = Lower(rest);
    auto normAt = low.find(", norm=");
    std::string colPart = rest;
    std::string normPart;
    if (normAt != std::string::npos) {
        colPart = rest.substr(0, normAt);
        normPart = rest.substr(normAt + std::string(", norm=").size());
        Trim(colPart);
        Trim(normPart);
        if (normPart.empty()) return false;
        cmd.yName = normPart;
        cmd.hasY = true;
    } else if (low.rfind("norm=", 0) == 0) {
        normPart = rest.substr(std::string("norm=").size());
        Trim(normPart);
        if (normPart.empty()) return false;
        cmd.yName = normPart;
        cmd.hasY = true;
        return true;
    }
    if (!colPart.empty()) {
        cmd.hasX = true;
        if (EqualsCi(colPart, "none")) cmd.zNone = true;
        else cmd.xName = colPart;
    }
    return cmd.hasX || cmd.hasY;
}

bool ParseRotate(Scan& sc, Command& cmd) {
    if (!sc.Eat("rotate")) return false;
    if (sc.Eat("x")) cmd.axis = 1;
    else if (sc.Eat("y")) cmd.axis = 0;
    else if (sc.Eat("z")) cmd.axis = 2;
    else return false;
    if (!sc.Number(cmd.f0)) return false;
    sc.Eat("on");
    return sc.Eof();
}

bool ParseSpin(Scan& sc, Command& cmd) {
    if (sc.Eat("x")) cmd.axis = 1;
    else if (sc.Eat("y")) cmd.axis = 0;
    else if (sc.Eat("z")) cmd.axis = 2;
    else return false;
    if (!sc.Eat("rotation")) return false;
    if (sc.Eat("stopped")) {
        cmd.spinning = false;
        cmd.rocking = false;
        return sc.Eof();
    }
    if (sc.Eat("spin")) {
        sc.Eat("on");
        cmd.spinning = true;
        return sc.Eof();
    }
    if (sc.Eat("rock")) {
        sc.Eat("on");
        cmd.rocking = true;
        return sc.Eof();
    }
    return false;
}

bool ReadBracket(Scan& sc, std::string& out) {
    if (!sc.Eat("[")) return false;
    size_t start = sc.i;
    size_t end = sc.s.find(']', start);
    if (end == std::string::npos) return false;
    out = sc.s.substr(start, end - start);
    Trim(out);
    sc.i = end + 1;
    return !out.empty();
}

// "Select on plot(1,1): X [a, b] Y [c, d], brush 1, 1420 / 10000 selected"
// The trailing count is the result of the drag; it is accepted and ignored.
bool ParseBrushRect(Scan& sc, Command& cmd) {
    int mode = -1;
    if (sc.Eat("select")) mode = 0;
    else if (sc.Eat("extend")) mode = 1;
    else if (sc.Eat("erase")) mode = 2;
    else return false;
    sc.Eat("on");
    if (!sc.Eat(":")) return false;
    if (!sc.Eat("x") || !ReadBracket(sc, cmd.xName)) return false;
    if (!sc.Eat("y") || !ReadBracket(sc, cmd.yName)) return false;
    int brush = 1;
    if (!sc.Eof()) {
        if (!sc.Eat(",") || !sc.Eat("brush") || !sc.Int(brush)) return false;
        if (!sc.Eof()) {
            int n1 = 0, n2 = 0;
            if (!sc.Eat(",") || !sc.Int(n1) || !sc.Eat("/") || !sc.Int(n2) ||
                !sc.Eat("selected") || !sc.Eof())
                return false;
        }
    }
    cmd.kind = CommandKind::BrushRect;
    cmd.axis = mode;
    cmd.brush = brush;
    return true;
}

bool ParseBrushTail(Scan& sc, Command& cmd) {
    if (!sc.Eat("brush")) return false;
    if (!sc.Int(cmd.intVal)) return false;
    if (sc.Eat("color")) {
        if (!sc.Eat(":") || !sc.Eat("(")) return false;
        if (!sc.Number(cmd.f0)) return false;
        sc.Eat(",");
        if (!sc.Number(cmd.f1)) return false;
        sc.Eat(",");
        if (!sc.Number(cmd.f2)) return false;
        sc.Eat(",");
        if (!sc.Number(cmd.f3)) return false;
        if (!sc.Eat(")")) return false;
        cmd.kind = CommandKind::BrushColor;
        return sc.Eof();
    }
    if (sc.Eat("symbol")) {
        if (!sc.Eat(":")) return false;
        cmd.xName = sc.Rest();
        cmd.kind = CommandKind::BrushSymbol;
        return !cmd.xName.empty();
    }
    if (sc.Eat("size") && sc.Eat("offset")) {
        if (!sc.Number(cmd.f0)) return false;
        cmd.kind = CommandKind::BrushSizeOffset;
        return sc.Eof();
    }
    if (sc.Eat("opacity") && sc.Eat("offset")) {
        if (!sc.Number(cmd.f0)) return false;
        cmd.kind = CommandKind::BrushOpacityOffset;
        return sc.Eof();
    }
    return false;
}

} // namespace

std::optional<Command> ParseCommand(const std::string& lineIn) {
    std::string s = lineIn;
    Trim(s);
    StripTimestamp(s);
    if (s.empty()) return std::nullopt;

    if (s[0] == '#') {
        Command c;
        c.kind = CommandKind::Ignore;
        return c;
    }

    std::string low = Lower(s);
    if (low.rfind("not a command", 0) == 0) {
        Command c;
        c.kind = CommandKind::Ignore;
        return c;
    }
    if (low == "all plots highlighted") {
        Command c;
        c.kind = CommandKind::AllPlotsHighlighted;
        return c;
    }
    // @path or @ path. Checked before location stripping so a path may contain "plot(".
    if (s[0] == '@') {
        std::string path = s.substr(1);
        Trim(path);
        if (path.empty()) return std::nullopt;
        Command c;
        c.kind = CommandKind::RunFile;
        c.xName = path;
        return c;
    }

    Command cmd;
    if (!StripLocation(s, cmd)) return std::nullopt;
    if (s.empty()) return std::nullopt;

    Scan sc{s, 0};

    auto flag = [&](std::string_view a, std::string_view b, CommandKind k) -> bool {
        Scan t = sc;
        bool on = false;
        if (!FlagSentence(t, a, b, on)) return false;
        cmd.kind = k;
        cmd.on = on;
        sc = t;
        return true;
    };

    {
        Scan t = sc;
        if (ParseBrushRect(t, cmd)) return cmd;
    }
    if (flag("grid", "lines", CommandKind::GridLines)) return cmd;
    if (flag("show", "unselected", CommandKind::ShowUnselected)) return cmd;
    if (flag("hover", "details", CommandKind::HoverDetails)) return cmd;
    if (flag("defer", "redraws", CommandKind::DeferRedraws)) return cmd;

    {
        Scan t = sc;
        if (t.Eat("histograms")) {
            t.EatPrepositionOn();
            if (t.Eat(":") && OnOff(t, cmd.on)) {
                cmd.kind = CommandKind::Histograms;
                return cmd;
            }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("randomized") && t.Eat("axes")) {
            t.Eat("on");
            if (t.Eof()) { cmd.kind = CommandKind::RandomizeAxes; return cmd; }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("reset") && t.Eat("rotation")) {
            t.Eat("on");
            if (t.Eof()) { cmd.kind = CommandKind::ResetRotation; return cmd; }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("reset") && t.Eat("brush") && t.Int(cmd.intVal) &&
            t.Eat("to") && t.Eat("default") && t.Eof()) {
            cmd.kind = CommandKind::BrushReset;
            return cmd;
        }
    }
    {
        Scan t = sc;
        if (ParseLock(t, cmd)) { cmd.kind = CommandKind::AxisLock; return cmd; }
    }
    {
        Scan t = sc;
        if (ParseZ(t, cmd)) { cmd.kind = CommandKind::ZAxis; return cmd; }
    }
    {
        Scan t = sc;
        if (ParseAxis(t, cmd)) { cmd.kind = CommandKind::Axis; return cmd; }
    }
    {
        Scan t = sc;
        if (ParseNorm(t, cmd)) { cmd.kind = CommandKind::Normalization; return cmd; }
    }
    {
        Scan t = sc;
        if (ParseRotate(t, cmd)) { cmd.kind = CommandKind::Rotate; return cmd; }
    }
    {
        Scan t = sc;
        if (ParseSpin(t, cmd)) { cmd.kind = CommandKind::SpinRock; return cmd; }
    }
    {
        Scan t = sc;
        float v;
        if (t.Eat("point") && t.Eat("size") && t.Number(v)) {
            t.Eat("on");
            if (t.Eof()) { cmd.kind = CommandKind::PointSize; cmd.f0 = v; return cmd; }
        }
    }
    {
        Scan t = sc;
        float v;
        if (t.Eat("opacity") && t.Number(v) && t.Eat("%")) {
            t.Eat("on");
            if (t.Eof()) { cmd.kind = CommandKind::Opacity; cmd.f0 = v; return cmd; }
        }
    }
    {
        Scan t = sc;
        int n;
        if (t.Eat("hist") && t.Eat("bins") && t.Int(n)) {
            t.Eat("on");
            if (t.Eof()) { cmd.kind = CommandKind::HistBins; cmd.intVal = n; return cmd; }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("colormap") && t.Eat(":")) {
            std::string rest = t.Rest();
            std::string lowRest = Lower(rest);
            const std::string key = ", color by ";
            auto at = lowRest.find(key);
            if (at != std::string::npos) {
                cmd.xName = rest.substr(0, at);
                std::string by = rest.substr(at + key.size());
                Trim(cmd.xName);
                Trim(by);
                std::string lowBy = Lower(by);
                const std::string rev = ", reversed";
                if (lowBy.size() >= rev.size() &&
                    lowBy.compare(lowBy.size() - rev.size(), rev.size(), rev) == 0) {
                    cmd.reversed = true;
                    by.erase(by.size() - rev.size());
                    Trim(by);
                }
                if (!cmd.xName.empty() && !by.empty()) {
                    cmd.yName = by;
                    cmd.kind = CommandKind::Colormap;
                    return cmd;
                }
            }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("additive") && t.Eat("blending") && t.Eat("(selected)") && t.Eat(":")) {
            if (OnOff(t, cmd.on)) { cmd.kind = CommandKind::AdditiveBlending; return cmd; }
        }
    }
    {
        Scan t = sc;
        float v;
        if (t.Eat("background") && t.Eat("brightness") && t.Number(v) && t.Eat("%") && t.Eof()) {
            cmd.kind = CommandKind::Background;
            cmd.f0 = v;
            return cmd;
        }
    }
    {
        Scan t = sc;
        if (t.Eat("active") && t.Eat("brush") && t.Eat(":") && t.Int(cmd.intVal)) {
            if (t.Eat("(unselected") && t.Eat("points)"))
                cmd.unselectedBrush = true;
            if (t.Eof()) { cmd.kind = CommandKind::ActiveBrush; return cmd; }
        }
    }
    {
        Scan t = sc;
        if (t.Eat("active") && t.Eat("plot") && t.Eat(":") && t.Eof()) {
            cmd.kind = CommandKind::ActivePlot;
            return cmd;
        }
    }
    {
        Scan t = sc;
        if (ParseBrushTail(t, cmd)) return cmd;
    }
    {
        Scan t = sc;
        int r, c;
        if (t.Eat("grid") && t.Eat("resized") && t.Eat("to") && t.Int(r) &&
            t.Eat("x") && t.Int(c) && t.Eof()) {
            cmd.kind = CommandKind::GridResize;
            cmd.row = r;
            cmd.col = c;
            return cmd;
        }
    }
    {
        Scan t = sc;
        if (t.Eat("cleared") && t.Eat("selection") && t.Eof()) {
            cmd.kind = CommandKind::ClearSelection;
            return cmd;
        }
    }
    {
        Scan t = sc;
        if (t.Eat("inverted") && t.Eat("selection") && t.Eof()) {
            cmd.kind = CommandKind::InvertSelection;
            return cmd;
        }
    }
    return std::nullopt;
}
