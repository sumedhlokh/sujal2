// ============================================================
//  AgriBot Core (CLI version) - called by app.py for every click
//
//  Design: this program is run ONCE per action. It loads the
//  mission state from a small text file, performs one action,
//  saves the state back, and prints the new state as JSON.
//  Streamlit (app.py) is only the display; every rule about
//  moving, inspecting, spraying, undo and redo lives here.
//
//  OOP  : Abstract class, Inheritance, Polymorphism, Encapsulation
//  DSA  : Stack (undo/redo), Queue (mission plan), Vector (log)
//
//  Build:
//     g++ -std=c++17 agribot_core.cpp -o agribot_core.exe
//  (On Linux/Mac, drop .exe: -o agribot_core)
//
//  Usage (this is what app.py calls for you):
//     agribot_core.exe <state_file> state
//     agribot_core.exe <state_file> move dir=up
//     agribot_core.exe <state_file> inspect
//     agribot_core.exe <state_file> spray
//     agribot_core.exe <state_file> undo
//     agribot_core.exe <state_file> redo
//     agribot_core.exe <state_file> queue_add type=move dir=up
//     agribot_core.exe <state_file> queue_next
//     agribot_core.exe <state_file> queue_all
//     agribot_core.exe <state_file> reset
// ============================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stack>
#include <queue>
#include <map>
#include <memory>
using namespace std;

// ============================================================
// 1. ENCAPSULATION: Field hides the grid, robot position, battery
// ============================================================
struct Snapshot {
    char grid[5][5];
    int x = 0, y = 0, battery = 100;

    string encode() const {
        string s;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) s += grid[r][c];
        s += " " + to_string(x) + " " + to_string(y) + " " + to_string(battery);
        return s;
    }
    // Parses "25gridchars x y battery" and returns how many chars were consumed.
    static Snapshot decode(const string& s) {
        Snapshot sn;
        istringstream iss(s);
        string gridPart;
        iss >> gridPart >> sn.x >> sn.y >> sn.battery;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++)
            sn.grid[r][c] = (r * 5 + c < (int)gridPart.size()) ? gridPart[r * 5 + c] : 'H';
        return sn;
    }
};

class Field {
private:
    char grid[5][5];
    int rx, ry, battery;

public:
    Field() { reset(); }

    void reset() {
        static const char init[5][5] = {
            {'H','H','H','H','H'},
            {'H','H','W','H','H'},
            {'H','H','H','D','H'},
            {'H','H','H','H','H'},
            {'H','W','H','H','H'}
        };
        for (int i = 0; i < 5; i++) for (int j = 0; j < 5; j++) grid[i][j] = init[i][j];
        rx = 0; ry = 0; battery = 100;
    }

    char cellAt(int r, int c) const { return grid[r][c]; }
    void setCell(int r, int c, char v) { grid[r][c] = v; }
    int getX() const { return rx; }
    int getY() const { return ry; }
    void setPos(int nx, int ny) { rx = nx; ry = ny; }
    int getBattery() const { return battery; }
    void setBattery(int b) { battery = b < 0 ? 0 : (b > 100 ? 100 : b); }
    void adjustBattery(int d) { setBattery(battery + d); }

    Snapshot snap() const {
        Snapshot s;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) s.grid[r][c] = grid[r][c];
        s.x = rx; s.y = ry; s.battery = battery;
        return s;
    }
    void restore(const Snapshot& s) {
        for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) grid[r][c] = s.grid[r][c];
        rx = s.x; ry = s.y; battery = s.battery;
    }
};

// ============================================================
// 2. ABSTRACT CLASS
// ============================================================
class Command {
protected:
    string error;
public:
    virtual bool execute(Field& f) = 0;
    virtual string describe() const = 0;
    string lastError() const { return error; }
    virtual ~Command() {}
};

// ============================================================
// 3. CHILD CLASSES
// ============================================================
class MoveCommand : public Command {
    int dx, dy; string label; int newX = 0, newY = 0;
public:
    MoveCommand(int dx_, int dy_, string label_) : dx(dx_), dy(dy_), label(move(label_)) {}
    bool execute(Field& f) override {
        int ox = f.getX(), oy = f.getY(), ob = f.getBattery();
        int nx = ox + dx, ny = oy + dy;
        if (nx < 0 || nx >= 5 || ny < 0 || ny >= 5) { error = "Cannot move " + label + ": edge of field."; return false; }
        if (ob < 2) { error = "Battery too low to move (needs 2%)."; return false; }
        f.setPos(nx, ny); f.adjustBattery(-2);
        newX = nx; newY = ny;
        return true;
    }
    string describe() const override {
        return "MOVE " + label + " to (" + to_string(newX) + "," + to_string(newY) + ")";
    }
};

class InspectCommand : public Command {
    int x = 0, y = 0; string result;
public:
    bool execute(Field& f) override {
        if (f.getBattery() < 1) { error = "Battery too low to inspect."; return false; }
        x = f.getX(); y = f.getY();
        char c = f.cellAt(x, y);
        result = (c == 'H') ? "Healthy" : (c == 'W') ? "Weed detected" : "Possible disease";
        f.adjustBattery(-1);
        return true;
    }
    string describe() const override {
        return "INSPECT (" + to_string(x) + "," + to_string(y) + ") -> " + result;
    }
};

class SprayCommand : public Command {
    int x = 0, y = 0; char oldCell = 'H';
public:
    bool execute(Field& f) override {
        x = f.getX(); y = f.getY(); oldCell = f.cellAt(x, y);
        if (oldCell == 'H') { error = "Nothing to spray, crop is already healthy."; return false; }
        if (f.getBattery() < 5) { error = "Battery too low to spray (needs 5%)."; return false; }
        f.setCell(x, y, 'H'); f.adjustBattery(-5);
        return true;
    }
    string describe() const override {
        return "SPRAY (" + to_string(x) + "," + to_string(y) + "), treated " + string(1, oldCell) + " -> H";
    }
};

static string jsonEscape(const string& s) {
    string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += (char)c;
    }
    return o;
}

// ============================================================
// 4. State containers: Stack (undo/redo), Queue (missions), Vector (log)
// ============================================================
struct StackEntry { Snapshot toRestore; string label; };
struct PendingMission { string type, dir, label; };

static shared_ptr<Command> makeCommand(const string& type, const string& dir, string& err) {
    if (type == "move") {
        if (dir == "up")    return make_shared<MoveCommand>(-1, 0, "up");
        if (dir == "down")  return make_shared<MoveCommand>(1, 0, "down");
        if (dir == "left")  return make_shared<MoveCommand>(0, -1, "left");
        if (dir == "right") return make_shared<MoveCommand>(0, 1, "right");
        err = "Unknown direction."; return nullptr;
    }
    if (type == "inspect") return make_shared<InspectCommand>();
    if (type == "spray")   return make_shared<SprayCommand>();
    err = "Unknown mission type."; return nullptr;
}
static string missionLabel(const string& type, const string& dir) {
    if (type == "move") return "Move " + dir;
    if (type == "inspect") return "Inspect";
    if (type == "spray") return "Spray";
    return type;
}

// ---------- Save / load the whole mission state to a plain text file ----------
static void writeStackEntry(ofstream& out, const StackEntry& e) {
    out << e.toRestore.encode() << " " << e.label << "\n";
}
static StackEntry readStackEntry(const string& line) {
    // format: <25gridchars> <x> <y> <battery> <label...>
    istringstream iss(line);
    string gridPart; int x, y, battery;
    iss >> gridPart >> x >> y >> battery;
    string label; getline(iss, label);
    if (!label.empty() && label[0] == ' ') label.erase(0, 1);
    StackEntry e;
    for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++)
        e.toRestore.grid[r][c] = (r * 5 + c < (int)gridPart.size()) ? gridPart[r * 5 + c] : 'H';
    e.toRestore.x = x; e.toRestore.y = y; e.toRestore.battery = battery;
    e.label = label;
    return e;
}

struct MissionState {
    Field field;
    stack<StackEntry> undoStack;
    stack<StackEntry> redoStack;
    queue<PendingMission> missionQueue;
    vector<string> history;
};

static void saveState(const string& path, const MissionState& st) {
    ofstream out(path);
    out << st.field.snap().encode() << "\n";

    // undo stack: dump bottom-to-top
    {
        vector<StackEntry> tmp;
        stack<StackEntry> copy = st.undoStack;
        while (!copy.empty()) { tmp.push_back(copy.top()); copy.pop(); }
        out << tmp.size() << "\n";
        for (int i = (int)tmp.size() - 1; i >= 0; i--) writeStackEntry(out, tmp[i]);
    }
    // redo stack: same
    {
        vector<StackEntry> tmp;
        stack<StackEntry> copy = st.redoStack;
        while (!copy.empty()) { tmp.push_back(copy.top()); copy.pop(); }
        out << tmp.size() << "\n";
        for (int i = (int)tmp.size() - 1; i >= 0; i--) writeStackEntry(out, tmp[i]);
    }
    // mission queue: front-to-back
    {
        queue<PendingMission> copy = st.missionQueue;
        out << copy.size() << "\n";
        while (!copy.empty()) {
            string dirField = copy.front().dir.empty() ? "-" : copy.front().dir;
            out << copy.front().type << " " << dirField << " " << copy.front().label << "\n";
            copy.pop();
        }
    }
    // history
    out << st.history.size() << "\n";
    for (auto& h : st.history) out << h << "\n";
}

static void loadState(const string& path, MissionState& st) {
    ifstream in(path);
    if (!in) return;   // no file yet -> defaults already set

    string line;
    if (!getline(in, line)) return;
    st.field.restore(Snapshot::decode(line));

    int n;
    in >> n; in.ignore();
    vector<StackEntry> undoV;
    for (int i = 0; i < n; i++) { getline(in, line); undoV.push_back(readStackEntry(line)); }
    for (auto& e : undoV) st.undoStack.push(e);   // push bottom-to-top order (file is bottom-to-top)

    in >> n; in.ignore();
    vector<StackEntry> redoV;
    for (int i = 0; i < n; i++) { getline(in, line); redoV.push_back(readStackEntry(line)); }
    for (auto& e : redoV) st.redoStack.push(e);

    in >> n; in.ignore();
    for (int i = 0; i < n; i++) {
        getline(in, line);
        istringstream iss(line);
        PendingMission m; iss >> m.type >> m.dir; getline(iss, m.label);
        if (m.dir == "-") m.dir = "";
        if (!m.label.empty() && m.label[0] == ' ') m.label.erase(0, 1);
        st.missionQueue.push(m);
    }

    in >> n; in.ignore();
    for (int i = 0; i < n; i++) { getline(in, line); st.history.push_back(line); }
}

// ============================================================
// 5. Actions (perform / undo / redo / queue)
// ============================================================
static string perform(MissionState& st, shared_ptr<Command> cmd) {
    Snapshot before = st.field.snap();
    if (!cmd->execute(st.field)) return cmd->lastError();
    st.undoStack.push({ before, cmd->describe() });
    while (!st.redoStack.empty()) st.redoStack.pop();
    st.history.push_back(cmd->describe());
    return "";
}
static string doUndo(MissionState& st) {
    if (st.undoStack.empty()) return "Nothing to undo.";
    StackEntry e = st.undoStack.top(); st.undoStack.pop();
    Snapshot afterState = st.field.snap();
    st.redoStack.push({ afterState, e.label });
    st.field.restore(e.toRestore);
    st.history.push_back("UNDO -> " + e.label);
    return "";
}
static string doRedo(MissionState& st) {
    if (st.redoStack.empty()) return "Nothing to redo.";
    StackEntry e = st.redoStack.top(); st.redoStack.pop();
    Snapshot beforeState = st.field.snap();
    st.undoStack.push({ beforeState, e.label });
    st.field.restore(e.toRestore);
    st.history.push_back("REDO -> " + e.label);
    return "";
}

// ============================================================
// 6. Build the JSON printed to stdout (this is what app.py reads)
// ============================================================
static string buildJson(const MissionState& st, const string& msg) {
    string j = "{";
    j += "\"grid\":[";
    for (int i = 0; i < 5; i++) {
        j += "\"";
        for (int k = 0; k < 5; k++) j += st.field.cellAt(i, k);
        j += "\""; if (i < 4) j += ",";
    }
    j += "],";
    j += "\"x\":" + to_string(st.field.getX()) + ",";
    j += "\"y\":" + to_string(st.field.getY()) + ",";
    j += "\"battery\":" + to_string(st.field.getBattery()) + ",";

    j += "\"undo\":[";
    { stack<StackEntry> c = st.undoStack; bool first = true;
      while (!c.empty()) { if (!first) j += ","; j += "\"" + jsonEscape(c.top().label) + "\""; c.pop(); first = false; } }
    j += "],\"redo\":[";
    { stack<StackEntry> c = st.redoStack; bool first = true;
      while (!c.empty()) { if (!first) j += ","; j += "\"" + jsonEscape(c.top().label) + "\""; c.pop(); first = false; } }
    j += "],\"queue\":[";
    { queue<PendingMission> c = st.missionQueue; bool first = true;
      while (!c.empty()) { if (!first) j += ","; j += "\"" + jsonEscape(c.front().label) + "\""; c.pop(); first = false; } }
    j += "],\"history\":[";
    for (int i = (int)st.history.size() - 1; i >= 0; i--) {
        j += "\"" + jsonEscape(st.history[i]) + "\"";
        if (i > 0) j += ",";
    }
    j += "],\"msg\":\"" + jsonEscape(msg) + "\"}";
    return j;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cout << "{\"msg\":\"Usage: agribot_core <state_file> <action> [key=value ...]\"}";
        return 0;
    }
    string statePath = argv[1];
    string action = argv[2];
    map<string, string> params;
    for (int i = 3; i < argc; i++) {
        string a = argv[i];
        size_t eq = a.find('=');
        if (eq != string::npos) params[a.substr(0, eq)] = a.substr(eq + 1);
    }
    auto getp = [&](const string& k) { auto it = params.find(k); return it == params.end() ? string("") : it->second; };

    MissionState st;
    loadState(statePath, st);
    string msg;

    if (action == "state") { /* no change */ }
    else if (action == "move") {
        string err;
        auto cmd = makeCommand("move", getp("dir"), err);
        msg = cmd ? perform(st, cmd) : err;
    }
    else if (action == "inspect") msg = perform(st, make_shared<InspectCommand>());
    else if (action == "spray")   msg = perform(st, make_shared<SprayCommand>());
    else if (action == "undo")    msg = doUndo(st);
    else if (action == "redo")    msg = doRedo(st);
    else if (action == "queue_add") {
        string type = getp("type"), dir = getp("dir"), err;
        auto cmd = makeCommand(type, dir, err);
        if (!cmd) msg = err;
        else { st.missionQueue.push({ type, dir, missionLabel(type, dir) }); st.history.push_back("QUEUED -> " + missionLabel(type, dir)); }
    }
    else if (action == "queue_next") {
        if (st.missionQueue.empty()) msg = "No missions queued.";
        else {
            PendingMission m = st.missionQueue.front(); st.missionQueue.pop();
            string err; auto cmd = makeCommand(m.type, m.dir, err);
            msg = cmd ? perform(st, cmd) : err;
        }
    }
    else if (action == "queue_all") {
        if (st.missionQueue.empty()) msg = "No missions queued.";
        else {
            int count = 0;
            while (!st.missionQueue.empty()) {
                PendingMission m = st.missionQueue.front(); st.missionQueue.pop();
                string err; auto cmd = makeCommand(m.type, m.dir, err);
                if (cmd) perform(st, cmd);
                count++;
            }
            msg = "Executed " + to_string(count) + " queued mission(s).";
        }
    }
    else if (action == "reset") {
        st.field.reset();
        st.undoStack = stack<StackEntry>();
        st.redoStack = stack<StackEntry>();
        st.missionQueue = queue<PendingMission>();
        st.history.push_back("RESET mission control");
    }
    else msg = "Unknown action.";

    saveState(statePath, st);
    cout << buildJson(st, msg);
    return 0;
}
