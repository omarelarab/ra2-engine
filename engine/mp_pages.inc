// ============================================================================
// Multiplayer shell pages (agent S): Network (LAN browser), Internet (online
// rooms on the player's own ra2lobbyd server), the multiplayer lobby, and the
// match hand-off. Drawn on the faithful shell (draw_shell + the skirmish
// page's widgets) at the 640x480 template canvas through ShellXform, so they
// scale and stay touch-friendly exactly like the Skirmish page (every target
// is at least a 19-px logical row = ~28 px on a 720p panel; lists scroll by
// tapping their strip).
//
// Included by main.cpp right after skirmish_page.inc. The protocol layers
// are the SDL-free engine/mp/ modules (tested in tests/test_mp.cpp):
//   LAN:    LanBrowser / LanAnnouncer (UDP 50010) + LobbyHost/LobbyClient
//           over MpNet (direct UDP; host game port [Net] LanPort = 50020)
//   Online: ServerLink (ra2lobbyd control plane) + the lobby over the
//           CnCNet V3 tunnel (MpNet::open_tunnel)
//   Start:  mp_start_session() -- THE hand-off to the lockstep layer.
//
// --play targets / probe keys: scripts/play/README.md "Multiplayer".
// docs/gap/net/05-transport-infra.md §3, §6; 01-original-protocol.md §6/§7.1.
// ============================================================================

// (engine/mp/ headers are included at the top of main.cpp: this file lands
// inside main.cpp's anonymous namespace.)

namespace mpui {

// --- RA2.INI [Net] -----------------------------------------------------------
struct Prefs {
    std::string nick = "Player";
    std::string server;               // "host[:port]" of the player's ra2lobbyd
    std::uint64_t pid = 0;            // random per install, never shown
    std::uint16_t lan_port = ra2mp::kLanGamePort;

    static Prefs load() {
        Prefs p;
        const ra2engine::IniDoc doc = read_settings_doc(settings_file_path());
        if (auto v = doc.get("Net", "Nickname")) p.nick = ra2mp::sanitize_name(*v);
        else if (auto h = doc.get("MultiPlayer", "Handle")) p.nick = ra2mp::sanitize_name(ra2engine::lobby_handle_decode(*h));
        if (p.nick.empty()) p.nick = "Player";
        if (auto v = doc.get("Net", "Server")) p.server = *v;
        if (auto v = doc.get("Net", "LanPort")) {
            const int n = std::atoi(v->c_str());
            if (n > 1024 && n < 65536) p.lan_port = static_cast<std::uint16_t>(n);
        }
        if (auto v = doc.get("Net", "PlayerId")) p.pid = std::strtoull(v->c_str(), nullptr, 16);
        if (p.pid == 0) {
            // Random 64-bit id: time + address entropy through splitmix64.
            std::uint64_t z = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
                              reinterpret_cast<std::uintptr_t>(&p) ^ (static_cast<std::uint64_t>(std::rand()) << 32);
            z += 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            p.pid = (z ^ (z >> 31)) | 1ull;
            p.save();
        }
        return p;
    }
    void save() const {
        const std::string path = settings_file_path();
        if (path.empty()) return;
        ra2engine::IniDoc doc = read_settings_doc(path);
        doc.set("Net", "Nickname", nick);
        doc.set("Net", "Server", server);
        doc.set("Net", "PlayerId", ra2mp::hex64(pid));
        doc.set("Net", "LanPort", std::to_string(lan_port));
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        const std::string t = doc.serialize();
        f.write(t.data(), static_cast<std::streamsize>(t.size()));
    }
};

// The version gate's "same game data" check (05 §2.5): CRC-32 chained over
// the effective bytes of EVERY INI the simulation reads -- rulesmd.ini,
// artmd.ini (infantry sequences, fire timing, foundations and buildup are in
// the sync hash) and aimd.ini (AI players and the AI takeover run it on every
// peer). W9-05-02: it covered rulesmd.ini only, so a modded artmd / aimd
// joined and desynced minutes in. Maps are matched by their [Digest] (the
// lobby) and by the frame-0 scenario CRC (TXT_SCENARIOS_DO_NOT_MATCH).
// Computed once.
inline std::uint32_t rules_crc() {
    static std::uint32_t crc = 0;
    static bool done = false;
    if (!done) {
        done = true;
        for (const char* name : {"rulesmd.ini", "artmd.ini", "aimd.ini"}) {
            std::vector<uint8_t> b;
            try {
                b = read_ra2md_file(env_install_dir(), name);
            } catch (const std::exception&) { b.clear(); }
            const std::uint32_t n = static_cast<std::uint32_t>(b.size());   // a missing file counts too
            crc = ra2engine::save::crc32(reinterpret_cast<const std::uint8_t*>(&n), sizeof n, crc);
            if (!b.empty()) crc = ra2engine::save::crc32(b.data(), b.size(), crc);
        }
    }
    return crc;
}

inline ra2mp::MpIdentity identity(const Prefs& p) {
    ra2mp::MpIdentity id;
    id.nick = p.nick;
    id.player_id = p.pid;
    id.rules_crc = rules_crc();
    id.version = RA2_VERSION;
    id.platform = ra2mp::platform_code();   // NET-PLAY: mixed platforms need the cross-platform option
    return id;
}

inline int platform_code() { return ra2mp::platform_code(); }

// W9-04-13: a game-list row's platform note: nothing for a host on this
// platform; "(PC)" / "(Android)" for another one, plus whether its host must
// turn on "Cross-platform (exp.)" first (xp = the host already did).
inline std::string platform_tag(std::uint8_t plat, bool xp) {
    if (plat == 0 || plat == ra2mp::platform_code()) return {};
    const char* n = ra2mp::platform_name(plat);
    std::string t = std::string("  (") + (*n ? n : "other platform") + ")";
    if (!xp) t += " needs Cross-platform";
    return t;
}

inline void init_once() {
    static bool done = false;
    if (done) return;
    done = true;
    ra2mp::net_init();
    // Android: WifiManager.MulticastLock while a LAN browser / announcer is open.
    ra2mp::lan_set_multicast_hook([](bool on) { ra2engine::os_android_multicast_lock(on); });
}

inline std::uint32_t random_u32() {
    static std::uint64_t s = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    std::uint32_t v = static_cast<std::uint32_t>(s >> 32);
    return v ? v : 1u;
}

// Log a multiplayer event to stderr and (under --play) the play log.
inline void log_line(const std::string& s) {
    std::cerr << "[mp] " << s << "\n";
    pres_log(0, "mp " + s);
}

// W9-03-04: the multiplayer game types of MPModesMD.ini (read by
// FUN_005D6FB0 for the sections Battle / ManBattle / Siege / Unholy /
// FreeForAll / Cooperative, FUN_005D7700; the list is kept sorted by the
// mode index, the MPModeIndex of the options string):
//   <index>=<GUI name>, <STT tooltip>, <rules override INI>, <map filter>, <random maps allowed>
// AI players: [RE-FALLBACK: the INI's own comment -- the Battle class "is the
// only one that allows AI players"; the class virtuals were not traced].
// AlliesAllowed / MustAlly come from the mode's override INI
// [MultiplayerDialogSettings].
struct GameMode {
    int index = 1;
    std::string klass = "Battle";
    std::string name_key = "GUI:Battle", stt_key = "STT:ModeBattle";
    std::string rules = "MPBattleMD.ini", filter = "standard";
    bool random_maps = true;
    bool allies_allowed = true, must_ally = false;
    bool ai_allowed() const { return klass == "Battle"; }
};

inline std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
}

inline bool ini_yes(const std::string& v, bool def) {
    if (v.empty()) return def;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
    return c == 'y' || c == 't' || c == '1';
}

inline const std::vector<GameMode>& game_modes() {
    static std::vector<GameMode> modes;
    static bool done = false;
    if (done) return modes;
    done = true;
    try {
        const std::vector<uint8_t> raw = read_ra2md_file(env_install_dir(), "mpmodesmd.ini");
        const Ini ini = Ini::parse(std::string(raw.begin(), raw.end()));
        for (const char* klass : {"Battle", "ManBattle", "Siege", "Unholy", "FreeForAll", "Cooperative"}) {
            if (!ini.has(klass)) continue;
            for (const auto& kv : ini.section(klass)) {
                const int idx = std::atoi(kv.first.c_str());
                std::vector<std::string> f;
                std::string cur;
                std::string v = kv.second;
                const std::size_t sc = v.find(';');
                if (sc != std::string::npos) v.resize(sc);
                for (char c : v) {
                    if (c == ',') { f.push_back(trim(cur)); cur.clear(); }
                    else cur += c;
                }
                f.push_back(trim(cur));
                if (idx <= 0 || f.size() < 4) continue;
                GameMode m;
                m.index = idx;
                m.klass = klass;
                m.name_key = f[0];
                m.stt_key = f[1];
                m.rules = f[2];
                m.filter = f[3];
                for (char& c : m.filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                m.random_maps = f.size() > 4 && ini_yes(f[4], false);
                try {   // the override's [MultiplayerDialogSettings]
                    const std::vector<uint8_t> r = read_ra2md_file(env_install_dir(), m.rules);
                    const Ini o = Ini::parse(std::string(r.begin(), r.end()));
                    m.allies_allowed = ini_yes(o.get("MultiplayerDialogSettings", "AlliesAllowed", "yes"), true);
                    m.must_ally = ini_yes(o.get("MultiplayerDialogSettings", "MustAlly", "no"), false);
                } catch (const std::exception&) {}
                modes.push_back(m);
            }
        }
    } catch (const std::exception&) {}
    if (modes.empty()) modes.push_back(GameMode{});   // a partial install: Battle only
    std::stable_sort(modes.begin(), modes.end(), [](const GameMode& a, const GameMode& b) { return a.index < b.index; });
    return modes;
}

// The YR string table (ra2md.csf in langmd.mix, like the map names of
// maplist.cpp): the game-type labels added by YR (GUI:TeamGame "Team
// Alliance", STT:ModeTeamGame) are not in the RA2 ra2.csf that gf_csf reads.
inline std::string yr_text(const std::string& label, const std::string& fallback) {
    static std::unique_ptr<ra2assets::CsfStrings> csf;
    static bool tried = false;
    if (!tried) {
        tried = true;
        try {
            ra2assets::MixFile lang = ra2assets::MixFile::open_path(env_install_dir() + "langmd.mix");
            if (const ra2assets::MixEntry* e = lang.find(std::string("ra2md.csf"))) {
                const auto raw = lang.read(*e);
                csf = std::make_unique<ra2assets::CsfStrings>(ra2assets::CsfStrings::load(raw.data(), raw.size()));
            }
        } catch (const std::exception&) {}
    }
    if (csf)
        if (const std::u16string* t = csf->find(label)) {
            std::string out;
            for (char16_t ch : *t) out += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ';
            if (!out.empty()) return out;
        }
    const std::string g = gf_csf(label, fallback.c_str());
    return g;
}

inline const GameMode& game_mode(int index) {
    for (const GameMode& m : game_modes())
        if (m.index == index) return m;
    return game_modes().front();
}

inline bool map_has_mode(const ra2engine::MapEntry& e, const std::string& tag) {
    for (const std::string& t : e.game_modes)
        if (t == tag) return true;
    return false;
}

// W9-03-01/05/06/07/08: the Host / Join Screen (0xBC / 0xBD) laid out on the
// clone's 640x480 canvas, in the template's column order (accept picture,
// player, flag, side, colour, start, team; headers above; check-boxes left,
// labelled sliders right, Build Off Ally beside Superweapons; chat below).
// One table so the phone layer can scale it.
struct LobbyLayout {
    int head_y = 17;                          // GUI:Players / Side / Color / Start / Team
    int row0 = 35, pitch = 23, row_h = 19;
    int accept_x = 12, accept_w = 16;         // 0x6D2.. acceptance pictures
    int name_x = 32, name_w = 128;
    int flag_x = 164, flag_w = 48;            // 0x6DA.. flags
    int side_x = 216, side_w = 106;
    int color_x = 326, color_w = 52;
    int start_x = 382, start_w = 42;
    int team_x = 428, team_w = 42;
    int opt_y = 222, opt_pitch = 23;
    int check_x = 12, check_w = 165;
    int slabel_x = 186, slabel_w = 92;
    int slider_x = 282, slider_w = 127, slider_h = 21;
    int boa_x = 186, boa_w = 270;
    int chat_x = 12, chat_y = 334, chat_w = 458, chat_h = 94;
    int edit_y = 432;
};
inline LobbyLayout& lobby_layout() {
    static LobbyLayout l;
    return l;
}

}  // namespace mpui

// ---------------------------------------------------------------------------
// The hand-off to the lockstep layer (NET-PLAY): the GameScene is built from
// session_to_skirmish(s, ctx) with the session seed, the lobby's resolved
// start positions and the local house, and runs as a lockstep match over the
// live transport (the lobby's socket / tunnel id) -- see net_play.inc.
// ---------------------------------------------------------------------------
Scene* mp_start_session(const Rules& rules, bool no_audio, const MenuArt& art, const ra2mp::MpSessionConfig& s,
                        std::shared_ptr<ra2mp::MpNet> transport);

namespace mpui {

// A text edit owned by a page.
struct Edit {
    std::string id;                   // probe target "edit:<id>"
    ra2engine::SbRect r;
    std::string text;
    std::size_t max = 32;
    bool enabled = true;
    int ime = ra2engine::kOsImeText;  // W9-04-14: soft-keyboard layout (kOsIme*)
};

}  // namespace mpui

// Common machinery of the multiplayer pages: shell chrome, right-column
// buttons, edits (Delete clears, Enter = on_enter), confirm popup, status
// line, --play probe. Subclasses draw/handle their body.
class MpPage : public Scene {
public:
    MpPage(const Rules& rules, bool no_audio, const MenuArt& art) : rules_(rules), no_audio_(no_audio), art_(art) {
        mpui::init_once();
    }
    ~MpPage() override { set_focus(-1); }

    void update(double dt) override {
        clock_ += dt;
        tick(ra2mp::now_ms());
        if (!art_.audio || !art_.audio->sys.enabled || !art_.audio->sys.mixer) return;
        if (!art_.audio->sys.mixer->playing("menu:grinder")) art_.audio->sys.mixer->play("menu:grinder");
        art_.audio->sys.out.pump(*art_.audio->sys.mixer);
    }
    Scene* next() override { Scene* n = next_; next_ = nullptr; return n; }
    bool locate(const std::string& what, int& x, int& y) const override {
        const auto t = targets();
        const auto it = t.find(what);
        if (it == t.end()) return false;
        const ra2engine::SbRect d = xform().hit(it->second);
        x = d.x + d.w / 2;
        y = d.y + d.h / 2;
        return true;
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = name();
        p.set_s("page", header_text());
        for (const auto& kv : targets()) {
            const ra2engine::SbRect d = xform().hit(kv.second);
            p.targets[kv.first] = ra2play::Rect{d.x, d.y, std::max(1, d.w), std::max(1, d.h)};
        }
        for (const ShellButton& b : buttons()) {
            p.set("button_enabled(" + b.label + ")", b.enabled ? 1 : 0);
            if (b.label.find(' ') != std::string::npos) {   // scripts: "Main Menu" -> button_enabled(Main_Menu)
                std::string k = b.label;
                for (char& c : k) if (c == ' ') c = '_';
                p.set("button_enabled(" + k + ")", b.enabled ? 1 : 0);
            }
        }
        for (const mpui::Edit& e : edits_) p.set_s("edit(" + e.id + ")", e.text);
        p.set_s("focus", focus_ >= 0 ? edits_[static_cast<std::size_t>(focus_)].id : "");
        p.set_s("status", status_);
        // W9-04-01..03/-14: the soft keyboard (text input state, the IME seed).
        p.set("text_input_active", SDL_IsTextInputActive() ? 1 : 0);
        p.set_s("ime_seed", g_ime_seed);
        p.set("ime_kind", g_ime_kind);
        p.set("touch_page", touch_seen_ ? 1 : 0);
        p.set_s("status_line", status_line());
        {
            int lx = -1000, ly = -1000;
            if (mx_ >= 0) to_logical(mx_, my_, lx, ly);
            p.set_s("tooltip", hover_tip(lx, ly));
        }
        body_probe(p);
        confirm_.play_probe(p);
    }

    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        apply_touch_metrics();   // W9-04-06 (a list rebuilt since the last event)
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const ShellXform xf = xform();
        const std::vector<ShellButton> btns = buttons();
        int lmx = -1000, lmy = -1000;
        if (mx_ >= 0) to_logical(mx_, my_, lmx, lmy);
        int hover = -1;
        for (int i = 0; i < static_cast<int>(btns.size()); ++i)
            if (btns[i].enabled && btns[i].rect.contains(lmx, lmy)) hover = i;
        draw_shell(plat, tc, art_, xf, btns, hover, (hover >= 0 && down_) ? hover : -1, header_text(), "v1.006",
                   -1, nullptr, 1);
        const ShellPainter p{ren, tc, art_, xf};
        draw_body(p, tc);
        for (std::size_t i = 0; i < edits_.size(); ++i) {
            const mpui::Edit& e = edits_[i];
            if (!e.enabled) {
                p.frame(p.dev(e.r), false);
                p.label(p.fit(e.text, e.r.w - 10), e.r.x + 4, e.r.y, e.r.w - 8, e.r.h, 0, false);
                continue;
            }
            p.edit(e.r, e.text, static_cast<int>(i) == focus_, std::fmod(clock_, 1.0) < 0.5);
        }
        // W9-04-14: a PASTE chip inside the focused edit's right end (touch:
        // there is no long-press menu on the engine's own edit).
        if (paste_chip_shown()) {
            const ra2engine::SbRect c = paste_chip();
            p.fill(p.dev(c), 60, 0, 0, 255);
            p.frame(p.dev(c), true);
            p.label("PASTE", c.x, c.y, c.w, c.h, 1, true);
        }
        draw_overlay(p);
        p.label(p.fit(status_line(lmx, lmy), 454), 15, 458, 454, 19, 0);
        confirm_.render(ren, tc);
    }

    void handle(const ra2engine::InputEvent& ev_in) override {
        using ra2engine::InputEvent;
        // W9-04-07: the Android lifecycle (the lobby's AWAY / BACK).
        if (ev_in.kind == InputEvent::AppBackground) { on_background(); return; }
        if (ev_in.kind == InputEvent::AppForeground) { on_foreground(); return; }
        InputEvent ev = ev_in;
        if (ev.kind == InputEvent::PointerMove || ev.kind == InputEvent::PointerDown || ev.kind == InputEvent::PointerUp ||
            ev.kind == InputEvent::Pan) {
            touch_seen_ = ev.touch;   // W9-04-06: touch metrics while the last pointer is a finger
            apply_touch_metrics();
        }
        // W9-04-15(g): a finger hold (the gesture layer's right click) is a
        // slow tap here -- on an edit it pastes (W9-04-14), elsewhere it
        // clicks like a tap instead of being silently ignored.
        if (ev.touch && ev.hold && ev.button == InputEvent::Right &&
            (ev.kind == InputEvent::PointerDown || ev.kind == InputEvent::PointerUp)) {
            if (ev.kind == InputEvent::PointerDown && !confirm_.open()) {
                int hx = 0, hy = 0;
                to_logical(ev.x, ev.y, hx, hy);
                for (std::size_t i = 0; i < edits_.size(); ++i)
                    if (edits_[i].enabled && edits_[i].r.contains(hx, hy)) {
                        set_focus(static_cast<int>(i));
                        paste();
                        return;
                    }
            }
            ev.button = InputEvent::Left;
            ev.hold = false;
        }
        if (ev.kind == InputEvent::PointerMove || ev.kind == InputEvent::PointerDown || ev.kind == InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == InputEvent::Left) {
            if (ev.kind == InputEvent::PointerDown) down_ = true;
            if (ev.kind == InputEvent::PointerUp) down_ = false;
        }
        if (confirm_.open()) {
            const int b = confirm_.handle(ev);
            if (b >= 0) confirm_done(confirm_action_, b);
            return;
        }
        int lx = 0, ly = 0;
        to_logical(ev.x, ev.y, lx, ly);
        if (ev.kind == InputEvent::Key) {
            if (focus_ >= 0) {
                mpui::Edit& e = edits_[static_cast<std::size_t>(focus_)];
                if (ev.key == ra2engine::kKeyBackspace) {
                    while (!e.text.empty() && (static_cast<unsigned char>(e.text.back()) & 0xC0) == 0x80) e.text.pop_back();
                    if (!e.text.empty()) e.text.pop_back();
                    edit_changed(focus_);
                    return;
                }
                if (ev.key == ra2engine::kKeyDelete) {
                    e.text.clear();
                    ime_sync(e.text, e.ime, static_cast<int>(e.max));
                    edit_changed(focus_);
                    return;
                }
                if (ev.key == ra2engine::kKeyEnter) { const int f = focus_; on_enter(f); return; }
                if (ev.ctrl && ev.vk == 'V') { paste(); return; }   // W9-04-14: Ctrl+V
            }
            if (body_key(ev)) return;
            if (ev.key == ra2engine::kKeyEscape) {
                set_focus(-1);
                press(back_action());
            }
            return;
        }
        if (ev.kind == InputEvent::Text) {
            if (focus_ < 0) return;
            mpui::Edit& e = edits_[static_cast<std::size_t>(focus_)];
            for (const char* c = ev.text; *c; ++c) {
                const unsigned char ch = static_cast<unsigned char>(*c);
                if (ch < 32 || ch == 127) continue;
                if (e.text.size() < e.max) e.text += static_cast<char>(ch);
            }
            edit_changed(focus_);
            return;
        }
        if (ev.kind == InputEvent::PointerMove || ev.kind == InputEvent::PointerUp) {
            body_drag(ev, lx, ly);
            return;
        }
        // W9-04-05: a finger drag (the gesture layer's Pan) scrolls a list /
        // moves a slider thumb where it started.
        if (ev.kind == InputEvent::Pan) {
            if (ev.x0 != pan_x0_ || ev.y0 != pan_y0_) {   // a new gesture
                pan_x0_ = ev.x0;
                pan_y0_ = ev.y0;
                pan_acc_ = 0.0;
            }
            int sx = 0, sy = 0;
            to_logical(ev.x0, ev.y0, sx, sy);
            body_pan(ev, sx, sy, lx, ly);
            return;
        }
        if (ev.kind != InputEvent::PointerDown || ev.button != InputEvent::Left) return;
        if (ev.touch) touch_snap(lx, ly);   // W9-04-06: a near miss lands on the nearest target
        if (paste_chip_shown() && paste_chip().contains(lx, ly)) {
            paste();
            return;
        }
        if (body_modal(lx, ly)) return;
        for (const ShellButton& b : buttons())
            if (b.enabled && b.rect.contains(lx, ly)) {
                set_focus(-1);
                play_ui("menu:click");
                press(b.action);
                return;
            }
        for (std::size_t i = 0; i < edits_.size(); ++i)
            if (edits_[i].enabled && edits_[i].r.contains(lx, ly)) { set_focus(static_cast<int>(i)); return; }
        set_focus(-1);
        body_click(lx, ly);
    }

protected:
    static constexpr int kWinW = 1280, kWinH = 720;
    static ShellXform xform() { return ShellXform::make(kWinW, kWinH); }
    static void to_logical(int dx, int dy, int& lx, int& ly) {
        const ShellXform xf = xform();
        lx = static_cast<int>(std::floor((dx - xf.ox) / xf.s));
        ly = static_cast<int>(std::floor((dy - xf.oy) / xf.s));
    }
    static ShellButton btn(int row, const std::string& s, bool en, int act) {
        return ShellButton{{484, 199 + 42 * row, 156, 42}, s, en, act, PAGE_MAIN};
    }
    virtual std::vector<ShellButton> buttons() const = 0;
    virtual std::string header_text() const = 0;
    virtual int back_action() const = 0;
    virtual void press(int act) = 0;
    virtual void draw_body(const ShellPainter& p, ra2engine::TextureCache& tc) = 0;
    virtual void draw_overlay(const ShellPainter& p) { (void)p; }
    virtual bool body_modal(int lx, int ly) { (void)lx; (void)ly; return false; }
    virtual void body_click(int lx, int ly) { (void)lx; (void)ly; }
    virtual void body_drag(const ra2engine::InputEvent& ev, int lx, int ly) { (void)ev; (void)lx; (void)ly; }
    virtual bool body_key(const ra2engine::InputEvent& ev) { (void)ev; return false; }
    // W9-04-05: a finger drag that started at logical (sx, sy), now at (lx, ly).
    virtual void body_pan(const ra2engine::InputEvent& ev, int sx, int sy, int lx, int ly) {
        (void)ev; (void)sx; (void)sy; (void)lx; (void)ly;
    }
    // W9-04-07: Android background / foreground (the lobby's AWAY / BACK).
    virtual void on_background() {}
    virtual void on_foreground() {}
    // W9-04-06: extra touch hit areas (the full rects behind small probe targets).
    virtual void touch_extra(std::vector<ra2engine::SbRect>& out) const { (void)out; }
    // W9-04-06: the page's lists (their scroll strip widens on touch).
    virtual void page_lists(std::vector<ShellList*>& out) { (void)out; }
    virtual void body_probe(ra2play::Probe& p) { (void)p; }
    virtual void body_targets(std::map<std::string, ra2engine::SbRect>& t) const { (void)t; }
    virtual void on_enter(int edit) { (void)edit; set_focus(-1); }
    virtual void edit_changed(int edit) { (void)edit; }
    virtual void tick(std::uint64_t now) { (void)now; }
    virtual void confirm_done(int action, int button) { (void)action; (void)button; }
    // W9-03-12: the status line 0x695 carries the hovered control's STT:*
    // tooltip (GUI options Tooltips on), else a short-lived notice (flash),
    // else the page's status text.
    virtual std::string hover_tip(int lx, int ly) const { (void)lx; (void)ly; return {}; }
    void flash(const std::string& s, double seconds = 4.0) {
        flash_ = s;
        flash_until_ = clock_ + seconds;
    }
    std::string status_line(int lx = -1000, int ly = -1000) const {
        if (!g_user_settings || g_user_settings->tooltips) {
            const std::string t = hover_tip(lx, ly);
            if (!t.empty()) return t;
        }
        if (!flash_.empty() && clock_ < flash_until_) return flash_;
        return status_;
    }

    std::map<std::string, ra2engine::SbRect> targets() const {
        std::map<std::string, ra2engine::SbRect> t;
        for (const ShellButton& b : buttons())
            if (b.enabled) t["button:" + b.label] = b.rect;
        if (confirm_.open()) return t;
        for (const mpui::Edit& e : edits_)
            if (e.enabled) t["edit:" + e.id] = e.r;
        body_targets(t);
        return t;
    }
    // W9-04-02/-03: focusing an edit (also the focused one again, or another
    // one) (re)opens the soft keyboard on it, seeded with its text; leaving
    // the edits hides it on a soft-keyboard platform (the desktop keeps its
    // old "restore what was there" behaviour).
    void set_focus(int i) {
        if (i >= 0) {
            if (focus_ < 0) text_was_active_ = SDL_IsTextInputActive();
            const bool restart = i != focus_ || !SDL_IsTextInputActive();
            focus_ = i;
            if (restart) {
                const mpui::Edit& e = edits_[static_cast<std::size_t>(i)];
                const SDL_Rect d = xform().dev(e.r.x, e.r.y, e.r.w, e.r.h);
                ime_open(d.x, d.y, d.w, d.h, e.text, e.ime, static_cast<int>(e.max));   // keep the edit above the keyboard
            }
            return;
        }
        if (focus_ >= 0 && (ime_screen_keyboard() || !text_was_active_)) SDL_StopTextInput();
        focus_ = -1;
    }
    // W9-04-14: the clipboard into the focused edit (Ctrl+V, the PASTE chip,
    // a finger hold on the edit). One line, control characters dropped.
    void paste() {
        if (focus_ < 0 || !SDL_HasClipboardText()) return;
        char* clip = SDL_GetClipboardText();
        if (!clip) return;
        mpui::Edit& e = edits_[static_cast<std::size_t>(focus_)];
        for (const char* c = clip; *c && e.text.size() < e.max; ++c) {
            const unsigned char ch = static_cast<unsigned char>(*c);
            if (ch == '\n' || ch == '\r') break;
            if (ch < 32 || ch == 127) continue;
            e.text += static_cast<char>(ch);
        }
        SDL_free(clip);
        ime_sync(e.text, e.ime, static_cast<int>(e.max));
        edit_changed(focus_);
        play_ui("menu:click");
    }
    ra2engine::SbRect paste_chip() const {
        if (focus_ < 0) return {};
        const ra2engine::SbRect& r = edits_[static_cast<std::size_t>(focus_)].r;
        return {r.x + r.w - 44, r.y + 1, 43, r.h - 2};
    }
    bool paste_chip_shown() const {
        if (!touch_seen_ || focus_ < 0 || !edits_[static_cast<std::size_t>(focus_)].enabled ||
            edits_[static_cast<std::size_t>(focus_)].ime == ra2engine::kOsImeChat)
            return false;
        // SDL_HasClipboardText is a JNI round trip on Android: ask twice a second.
        if (clock_ - clip_checked_ >= 0.5 || clip_checked_ < 0.0) {
            clip_checked_ = clock_;
            clip_has_ = SDL_HasClipboardText() == SDL_TRUE;
        }
        return clip_has_;
    }
    // W9-04-06: touch targets. The faithful 640x480 layout stays (the art is
    // not resized); a finger tap that hits no target lands on the nearest
    // one within 16 dp, so every target gets a >= 48 dp tall catch zone on a
    // phone, and the lists' scroll strips widen to 32 logical px.
    void touch_snap(int& lx, int& ly) const {
        std::vector<ra2engine::SbRect> c;
        for (const auto& kv : targets())
            if (kv.first.rfind("slider:", 0) != 0) c.push_back(kv.second);
        if (paste_chip_shown()) c.push_back(paste_chip());
        touch_extra(c);
        for (const ra2engine::SbRect& r : c)
            if (r.w > 0 && r.contains(lx, ly)) return;
        const double radius = std::max(6.0, std::min(24.0, 16.0 * window_px_per_dp() / xform().s));
        const ra2engine::SbRect* best = nullptr;
        double bd = radius;
        for (const ra2engine::SbRect& r : c) {
            if (r.w <= 0 || r.h <= 0) continue;
            const int dx = lx < r.x ? r.x - lx : lx >= r.x + r.w ? lx - (r.x + r.w - 1) : 0;
            const int dy = ly < r.y ? r.y - ly : ly >= r.y + r.h ? ly - (r.y + r.h - 1) : 0;
            const double d = std::sqrt(double(dx) * dx + double(dy) * dy);
            if (d <= bd) {
                bd = d;
                best = &r;
            }
        }
        if (!best) return;
        lx = std::max(best->x, std::min(best->x + best->w - 1, lx));
        ly = std::max(best->y, std::min(best->y + best->h - 1, ly));
    }
    void apply_touch_metrics() {
        std::vector<ShellList*> ls;
        page_lists(ls);
        for (ShellList* l : ls) l->strip_w = touch_seen_ ? 32 : ShellList::kStripW;
    }
    // W9-04-05: a drag on a list scrolls it by whole rows (the content follows the finger).
    void list_pan(ShellList& l, const ra2engine::InputEvent& ev) {
        pan_acc_ += ev.dy / xform().s;
        const int rows = static_cast<int>(pan_acc_ / std::max(1, l.item_h));
        if (rows) {
            l.scroll(-rows);
            pan_acc_ -= rows * l.item_h;
        }
    }
    void play_ui(const char* key) const {
        if (art_.audio && art_.audio->sys.enabled && art_.audio->sys.mixer) art_.audio->sys.mixer->play(key);
    }
    void ask(int action, const std::string& text, std::vector<std::string> buttons) {
        confirm_action_ = action;
        confirm_.ask(text, std::move(buttons));
    }
    // A wrapped multi-line text block.
    int text_block(const ShellPainter& p, const std::string& s, int x, int y, int w, int max_lines = 99) const {
        int n = 0;
        for (const std::string& ln : shell_wrap(p, s, w)) {
            if (n++ >= max_lines) break;
            p.label(ln, x, y, w, 16, 0);
            y += 16;
        }
        return y;
    }

    const Rules& rules_;
    bool no_audio_ = false;
    const MenuArt& art_;
    std::vector<mpui::Edit> edits_;
    int focus_ = -1;
    bool text_was_active_ = true;
    std::string status_;
    GfConfirm confirm_;
    int confirm_action_ = 0;
    Scene* next_ = nullptr;
    int mx_ = -1, my_ = -1;
    bool down_ = false;
    double clock_ = 0.0;
#if defined(__ANDROID__) || defined(RA2_IOS)
    bool touch_seen_ = true;                 // W9-04-06: a phone starts in touch metrics
#else
    bool touch_seen_ = false;
#endif
    int pan_x0_ = -1, pan_y0_ = -1;          // W9-04-05: the current drag gesture
    double pan_acc_ = 0.0;
    mutable double clip_checked_ = -1.0;     // W9-04-14: clipboard poll
    mutable bool clip_has_ = false;
    std::string flash_;
    double flash_until_ = 0.0;
};

// ===========================================================================
// The multiplayer lobby = the Host Screen 0xBC / Join Screen 0xBD (built
// from the Skirmish page: 8 player rows with country / colour combos, the
// same options + sliders, map choice with the preview well) + Team / Start
// columns, slot kinds, Accept, lobby chat; Customize Battle = dialog 0x6B
// (Game Type + Game Map lists).
// ===========================================================================
class MpLobbyScene : public MpPage {
public:
    enum Role { HOST, CLIENT };
    enum Net { LAN, ONLINE };

    // LAN host / online host.
    static MpLobbyScene* host(const Rules& r, bool na, const MenuArt& a, Net net,
                              std::unique_ptr<ra2mp::ServerLink> link = nullptr, bool priv = false) {
        auto* s = new MpLobbyScene(r, na, a, HOST, net);
        s->link_ = std::move(link);
        s->private_ = priv;
        s->start_host();
        return s;
    }
    // LAN join (ip:port) / online join (tunnel + host v3 id).
    static MpLobbyScene* join_lan(const Rules& r, bool na, const MenuArt& a, const ra2mp::Addr& host) {
        auto* s = new MpLobbyScene(r, na, a, CLIENT, LAN);
        s->start_client_lan(host);
        return s;
    }
    static MpLobbyScene* join_online(const Rules& r, bool na, const MenuArt& a, std::unique_ptr<ra2mp::ServerLink> link,
                                     std::uint32_t my_v3, std::uint32_t host_v3, const std::string& code) {
        auto* s = new MpLobbyScene(r, na, a, CLIENT, ONLINE);
        s->link_ = std::move(link);
        s->code_ = code;
        s->start_client_online(my_v3, host_v3);
        return s;
    }
    ~MpLobbyScene() override {
        // NET-PLAY: after the hand-off the peers are in the match on the same
        // socket -- a SIGN_OFF now would drop them from it.
        if (host_ && !handed_off_) host_->close();
        if (client_ && client_->state() == ra2mp::LobbyClient::State::Joined) client_->leave();
        if (link_ && role_ == HOST) link_->close_room();
    }
    const char* name() const override { return "mp-lobby"; }

private:
    enum Act { A_NONE, A_START, A_MAP, A_BACK, A_READY, A_USEMAP, A_CANCEL, A_RANDMAP };
    enum Combo { CB_NONE, CB_KIND, CB_COUNTRY, CB_COLOR, CB_TEAM, CB_START };
    // CK_BOA = 0x69D GUI:BuildOffAlly (W9-03-05). NET-PLAY clone extras:
    // CK_AIT (a leaver's house -> AI) and CK_XP (cross-platform, experimental).
    enum Check { CK_SHORT, CK_MCV, CK_CRATES, CK_SW, CK_BOA, CK_AIT, CK_XP, CK_COUNT };
    enum Slider { SL_SPEED, SL_CREDITS, SL_UNITS, SL_COUNT };
    enum Confirm { C_LEAVE = 1, C_INFO, C_QUIT };
    // W9-04-07: how long a lobby member's background is excused (the match's
    // AWAY value, 05 §7.2 -- the original never backgrounds; the clone keeps
    // one number for both).
    static constexpr std::uint16_t kLobbyAwaySeconds = 90;

    MpLobbyScene(const Rules& r, bool na, const MenuArt& a, Role role, Net net) : MpPage(r, na, a), role_(role), net_(net) {
        prefs_ = mpui::Prefs::load();
        countries_ = gf_lobby_countries();
        text_.load(countries_);
        auto ri = [&](const char* k, int d) { return static_cast<int>(r.section_double("MultiplayerDialogSettings", k, d)); };
        min_money_ = ri("MinMoney", 5000);
        max_money_ = std::max(min_money_, ri("MaxMoney", 10000));
        money_step_ = std::max(1, ri("MoneyIncrement", 100));
        min_units_ = ri("MinUnitCount", 0);
        max_units_ = std::max(min_units_, ri("MaxUnitCount", 10));
        defaults_.credits = std::max(min_money_, std::min(max_money_, ri("Money", 10000)));
        defaults_.unit_count = std::max(min_units_, std::min(max_units_, ri("UnitCount", 10)));
        defaults_.game_speed = std::max(0, std::min(6, ri("GameSpeed", 1)));
        defaults_.tech_level = ri("TechLevel", 10);
        // W9-03-26: the [MultiplayerDialogSettings] booleans (the Skirmish
        // page's keys) seed the host's options.
        auto rb = [&](const char* k, bool d) {
            return mpui::ini_yes(r.section_string("MultiplayerDialogSettings", k, d ? "yes" : "no"), d);
        };
        defaults_.short_game = rb("ShortGame", true);
        defaults_.superweapons = rb("SuperWeaponsAllowed", true);
        defaults_.build_off_ally = rb("BuildOffAlly", true);
        defaults_.mcv_repacks = rb("MCVRedeploys", true);
        defaults_.crates = rb("Crates", true);
        mt_.players = mpui::yr_text("GUI:Players", "Players");
        mt_.side = mpui::yr_text("GUI:Side", "Side");
        mt_.color = mpui::yr_text("GUI:Color", "Color");
        mt_.start = mpui::yr_text("GUI:StartPosition", "Start");
        mt_.team = mpui::yr_text("GUI:Team", "Team");
        mt_.accept = mpui::yr_text("GUI:Accept", "Accept");
        mt_.host_screen = mpui::yr_text("GUI:HostScreen", "Host Screen");
        mt_.join_screen = mpui::yr_text("GUI:JoinScreen", "Join Screen");
        mt_.only_host = mpui::yr_text("TXT_ONLY_HOST_CAN_MODIFY", "Only the host can modify this option.");
        mt_.open = mpui::yr_text("GUI:Open", "Open");
        mt_.closed = mpui::yr_text("GUI:Closed", "Closed");
        try {
            // W9-03-04: every game type's maps (filtered per mode below).
            catalog_ = std::make_unique<ra2engine::MapCatalog>(ra2engine::load_yr_map_catalog(env_install_dir(), ""));
            ra2engine::filter_lobby_theaters(*catalog_);
        } catch (const std::exception&) { catalog_.reset(); }
        const mpui::LobbyLayout& L = mpui::lobby_layout();
        edits_.push_back(mpui::Edit{"chat", {L.chat_x, L.edit_y, L.chat_w, 19}, "", 100, true});
        edits_.back().ime = ra2engine::kOsImeChat;
    }
    struct MpText {
        std::string players, side, color, start, team, accept, host_screen, join_screen, only_host, open, closed;
    };

    // --- setup ---------------------------------------------------------------
    void start_host() {
        ra2mp::MpLobbyModel m;
        m.opt = defaults_;
        m.mode = mpui::game_modes().front().index;
        int mi = -1;   // the first map of the first game type (Battle: "standard")
        for (int i = 0; i < static_cast<int>(maps().size()) && mi < 0; ++i)
            if (mpui::map_has_mode(maps()[static_cast<std::size_t>(i)], mpui::game_mode(m.mode).filter)) mi = i;
        if (mi < 0 && !maps().empty()) mi = 0;
        m.map = map_info(mi);
        for (int i = std::max(1, m.map.max_players); i < ra2mp::kSlots; ++i) m.slots[i].kind = ra2mp::SlotKind::Closed;
        if (net_ == LAN) {
            auto t = std::make_shared<ra2mp::MpNet>();
            if (!t->open_direct(prefs_.lan_port) && !t->open_direct(0)) status_ = "Could not open a network port.";
            port_ = t->port();
            transport_ = t;
        } else {
            my_v3_ = mpui::random_u32();
            auto t = std::make_shared<ra2mp::MpNet>();
            t->open_tunnel(link_ ? link_->tunnel_addr() : ra2mp::Addr{}, my_v3_);
            transport_ = t;
        }
        host_ = std::make_unique<ra2mp::LobbyHost>(*transport_, mpui::identity(prefs_), m);
        host_->set_room_name(prefs_.nick + "'s game");
        host_->on_event = [](const std::string& s) { mpui::log_line(s); };
        if (net_ == LAN) {
            ra2mp::LanBeacon b;
            b.rules_crc = mpui::rules_crc();
            b.game_port = port_;
            b.nonce = mpui::random_u32();
            b.room = host_->room_name();
            b.host = prefs_.nick;
            b.version = RA2_VERSION;
            b.platform = static_cast<std::uint8_t>(mpui::platform_code());   // W9-04-13
            announcer_.open(b);
            // The address other players type into "Join by IP" (first LAN address).
            const std::vector<std::uint32_t> ips = ra2mp::local_ipv4s();
            where_ = ra2mp::Addr{ips.empty() ? ra2mp::kLoopback : ips.front(), port_}.str();
            mpui::log_line("hosting LAN game on port " + std::to_string(port_));
        } else if (link_) {
            link_->create(host_->room_name(), host_->model().map.max_players, private_, false, map_display(), my_v3_);
            where_ = "Creating room...";
        }
        map_ix_ = mi;
    }
    void start_client_lan(const ra2mp::Addr& host) {
        auto t = std::make_shared<ra2mp::MpNet>();
        t->open_direct(0);
        transport_ = t;
        client_ = std::make_unique<ra2mp::LobbyClient>(*transport_, ra2mp::peer_of(host), mpui::identity(prefs_));
        where_ = "Host " + host.str();
        mpui::log_line("joining LAN game at " + host.str());
        wire_client();
    }
    void start_client_online(std::uint32_t my_v3, std::uint32_t host_v3) {
        my_v3_ = my_v3;
        auto t = std::make_shared<ra2mp::MpNet>();
        t->open_tunnel(link_ ? link_->tunnel_addr() : ra2mp::Addr{}, my_v3);
        transport_ = t;
        client_ = std::make_unique<ra2mp::LobbyClient>(*transport_, ra2mp::peer_of_v3(host_v3), mpui::identity(prefs_));
        host_v3_ = host_v3;
        where_ = "Room " + code_;
        mpui::log_line("joining room " + code_ + " via tunnel " + (link_ ? link_->tunnel_addr().str() : std::string("?")));
        wire_client();
    }
    void wire_client() {
        client_->have_map = [this](const ra2mp::MpMap& m) { return local_map_matches(m); };
    }

    // --- model access ----------------------------------------------------------
    ra2mp::LobbyBase& lobby() { return host_ ? static_cast<ra2mp::LobbyBase&>(*host_) : *client_; }
    const ra2mp::LobbyBase& lobby() const { return host_ ? static_cast<const ra2mp::LobbyBase&>(*host_) : *client_; }
    const ra2mp::MpLobbyModel& model() const { return lobby().model(); }
    int me() const { return lobby().local_slot(); }
    bool is_host() const { return role_ == HOST; }
    bool joined() const { return host_ || (client_ && client_->state() == ra2mp::LobbyClient::State::Joined); }
    // Who may edit row k's country/colour/team/start.
    bool can_edit(int k) const {
        if (!joined()) return false;
        const ra2mp::MpSlot& s = model().slots[k];
        if (!s.occupied()) return false;
        return is_host() ? (k == 0 || s.kind == ra2mp::SlotKind::AI) : k == me();
    }

    // --- map catalog -----------------------------------------------------------
    const std::vector<ra2engine::MapEntry>& maps() const {
        static const std::vector<ra2engine::MapEntry> kEmpty;
        return catalog_ ? catalog_->entries : kEmpty;
    }
    std::string map_text(int i) const {
        if (!catalog_ || i < 0 || i >= static_cast<int>(maps().size())) return {};
        try {
            if (const ra2assets::MixEntry* e = catalog_->maps.find(maps()[static_cast<std::size_t>(i)].file_name)) {
                const std::vector<uint8_t> raw = catalog_->maps.read(*e);
                return std::string(raw.begin(), raw.end());
            }
        } catch (const std::exception&) {}
        return {};
    }
    ra2mp::MpMap map_info(int i) const {
        ra2mp::MpMap m;
        if (i < 0 || i >= static_cast<int>(maps().size())) return m;
        const ra2engine::MapEntry& e = maps()[static_cast<std::size_t>(i)];
        m.file = e.file_name;
        m.max_players = (e.max_players > 1) ? std::min(8, e.max_players) : 8;
        const std::string text = map_text(i);
        m.size = static_cast<std::uint32_t>(text.size());
        m.digest = ra2mp::normalize_digest(ra2mp::map_digest_of(text));
        m.official = true;
        return m;
    }
    int map_index_of(const std::string& file) const {
        for (int i = 0; i < static_cast<int>(maps().size()); ++i)
            if (maps()[static_cast<std::size_t>(i)].file_name == file) return i;
        return -1;
    }
    // [Digest] check (01 §7.1): a local map with the same file and digest.
    // Built-in maps are never transferred (map download is a follow-up).
    bool local_map_matches(const ra2mp::MpMap& m) {
        const auto it = digest_cache_.find(m.file);
        std::string d;
        if (it != digest_cache_.end()) d = it->second;
        else {
            const int i = map_index_of(m.file);
            d = i < 0 ? std::string("\x01missing") : ra2mp::normalize_digest(ra2mp::map_digest_of(map_text(i)));
            digest_cache_[m.file] = d;
        }
        return d != "\x01missing" && d == m.digest;
    }
    std::string map_display() const {
        const int i = map_index_of(model().map.file);
        return i >= 0 ? maps()[static_cast<std::size_t>(i)].display_name : model().map.file;
    }

    // --- labels ---------------------------------------------------------------
    std::string kind_label(int k) const {
        const ra2mp::MpSlot& s = model().slots[k];
        switch (s.kind) {
            case ra2mp::SlotKind::Open: return "Open";
            case ra2mp::SlotKind::Closed: return "Closed";
            case ra2mp::SlotKind::AI: return s.ai == ra2engine::kLobbyAiBrutal ? text_.ai[3]
                                             : s.ai == ra2engine::kLobbyAiMedium ? text_.ai[2] : text_.ai[1];
            default: return s.name;
        }
    }
    std::string country_label(int c) const {
        return (c >= 0 && c < static_cast<int>(text_.country.size())) ? text_.country[static_cast<std::size_t>(c)]
                                                                      : text_.random_country;
    }
    std::string color_label(int c) const { return (c >= 0 && c < 8) ? text_.color_abbrev[c] : text_.random_color; }
    static std::string team_label(int t) { return t < 0 ? "-" : std::string(1, static_cast<char>('A' + t)); }
    static std::string start_label(int s) { return s < 0 ? "?" : std::to_string(s + 1); }

    // --- layout (logical 640x480; mpui::LobbyLayout) ----------------------------
    static const mpui::LobbyLayout& lay() { return mpui::lobby_layout(); }
    static int rowY(int k) { return lay().row0 + lay().pitch * k; }
    static ra2engine::SbRect accept_rect(int k) { return {lay().accept_x, rowY(k) + 1, lay().accept_w, lay().row_h - 2}; }
    static ra2engine::SbRect kind_rect(int k) { return {lay().name_x, rowY(k), lay().name_w, lay().row_h}; }
    static ra2engine::SbRect flag_rect(int k) { return {lay().flag_x, rowY(k), lay().flag_w, lay().row_h}; }
    static ra2engine::SbRect country_rect(int k) { return {lay().side_x, rowY(k), lay().side_w, lay().row_h}; }
    static ra2engine::SbRect color_rect(int k) { return {lay().color_x, rowY(k), lay().color_w, lay().row_h}; }
    static ra2engine::SbRect start_rect(int k) { return {lay().start_x, rowY(k), lay().start_w, lay().row_h}; }
    static ra2engine::SbRect team_rect(int k) { return {lay().team_x, rowY(k), lay().team_w, lay().row_h}; }
    // Check-boxes: Short / MCV / Crates / Superweapons down the left column
    // (0x54E / 0x3E8 / 0x54D / 0x69A), Build Off Ally beside Superweapons in
    // the slider column (0x69D), the clone extras on their own row below.
    static ra2engine::SbRect check_rect(int c) {
        const mpui::LobbyLayout& L = lay();
        if (c <= CK_SW) return {L.check_x, L.opt_y + L.opt_pitch * c, L.check_w, 16};
        if (c == CK_BOA) return {L.boa_x, L.opt_y + L.opt_pitch * 3, L.boa_w, 16};
        return {c == CK_AIT ? L.check_x : L.slabel_x, L.opt_y + L.opt_pitch * 4,
                c == CK_AIT ? L.check_w : L.boa_w, 16};
    }
    static ra2engine::SbRect slider_rect(int s) {
        return {lay().slider_x, lay().opt_y + lay().opt_pitch * s, lay().slider_w, lay().slider_h};
    }
    static ra2engine::SbRect slider_label(int s) { return {lay().slabel_x, slider_rect(s).y, lay().slabel_w, 16}; }
    static ra2engine::SbRect chat_rect() { return {lay().chat_x, lay().chat_y, lay().chat_w, lay().chat_h}; }
    static ra2engine::SbRect preview_rect() { return {486, 37, 144, 112}; }
    // Dialog 0x6B: Game Type (0x6EB) + Game Map (0x553) lists (the Skirmish
    // page's Customize Battle geometry).
    static ra2engine::SbRect type_list_rect() { return {30, 126, 195, 260}; }
    static ra2engine::SbRect map_list_rect() { return {252, 126, 195, 260}; }
    // Every control rect of the options block (W9-03-01 overlap check).
    std::vector<ra2engine::SbRect> option_rects() const {
        std::vector<ra2engine::SbRect> v;
        for (int c = 0; c < CK_COUNT; ++c) {
            const ra2engine::SbRect r = check_rect(c);
            const int w = 26 + static_cast<int>(check_label(c).size()) * 6;   // lamp + label text (6 px glyphs)
            v.push_back({r.x, r.y, std::min(r.w, w), r.h});
        }
        for (int s = 0; s < SL_COUNT; ++s) {
            v.push_back(slider_label(s));
            v.push_back(slider_rect(s));
        }
        return v;
    }
    int layout_overlaps() const {
        const std::vector<ra2engine::SbRect> v = option_rects();
        int n = 0;
        for (std::size_t i = 0; i < v.size(); ++i)
            for (std::size_t j = i + 1; j < v.size(); ++j)
                if (v[i].x < v[j].x + v[j].w && v[j].x < v[i].x + v[i].w && v[i].y < v[j].y + v[j].h &&
                    v[j].y < v[i].y + v[i].h)
                    ++n;
        return n;
    }
    const mpui::GameMode& mode() const { return mpui::game_mode(model().mode); }
    int visible_rows() const { return std::max(1, std::min(ra2mp::kSlots, model().map.max_players)); }

    int slider_range(int s) const {
        if (s == SL_SPEED) return 6;
        if (s == SL_CREDITS) return (max_money_ - min_money_) / money_step_;
        return max_units_ - min_units_;
    }
    int slider_pos(int s) const {
        const ra2mp::MpOptions& o = model().opt;
        if (s == SL_SPEED) return 6 - o.game_speed;
        if (s == SL_CREDITS) return (o.credits - min_money_) / money_step_;
        return o.unit_count - min_units_;
    }
    void set_slider(int s, int pos) {
        if (!host_) return;
        pos = std::max(0, std::min(slider_range(s), pos));
        ra2mp::MpOptions o = model().opt;
        if (s == SL_SPEED) o.game_speed = 6 - pos;
        else if (s == SL_CREDITS) o.credits = min_money_ + pos * money_step_;
        else o.unit_count = min_units_ + pos;
        if (o.game_speed != model().opt.game_speed || o.credits != model().opt.credits || o.unit_count != model().opt.unit_count)
            host_->set_options(o);
    }
    std::string slider_text(int s) const {
        if (s == SL_CREDITS) return std::to_string(model().opt.credits);
        if (s == SL_UNITS) return std::to_string(model().opt.unit_count);
        return std::to_string(slider_pos(s));
    }
    bool check_val(int c) const {
        const ra2mp::MpOptions& o = model().opt;
        return c == CK_SHORT ? o.short_game : c == CK_MCV ? o.mcv_repacks : c == CK_CRATES ? o.crates
             : c == CK_AIT ? o.ai_takeover : c == CK_XP ? o.cross_platform : c == CK_BOA ? o.build_off_ally
             : o.superweapons;
    }
    std::string check_label(int c) const {
        if (c == CK_AIT) return "AI takes over";
        if (c == CK_XP) return "Cross-platform (exp.)";
        return c == CK_SHORT ? text_.short_game : c == CK_MCV ? text_.mcv : c == CK_CRATES ? text_.crates
             : c == CK_BOA ? text_.boa : text_.sw;
    }
    static const char* check_id(int c) {
        static const char* k[CK_COUNT] = {"short", "mcv", "crates", "superweapons", "buildoffally", "aitakeover",
                                          "crossplatform"};
        return k[c];
    }
    static const char* slider_id(int s) {
        static const char* k[SL_COUNT] = {"speed", "credits", "units"};
        return k[s];
    }

    // --- buttons ----------------------------------------------------------------
    std::vector<ShellButton> buttons() const override {
        // 0x6B: Use Map (0x6C5) / Random Map (0x583) / Cancel (0x5C0). The
        // random map generator (RMG, "RandMap.img" FUN_00641850 after dialog
        // FUN_005E60B0) is not reproduced: the button stays greyed like the
        // Skirmish page's (W9-03-04).
        if (choosing_)
            return {btn(0, text_.use_map, true, A_USEMAP), btn(1, text_.random_map, false, A_RANDMAP),
                    btn(5, text_.cancel, true, A_CANCEL)};
        if (is_host())
            return {btn(1, text_.start, joined(), A_START), btn(2, text_.choose, joined(), A_MAP),
                    btn(5, text_.back, true, A_BACK)};
        // 0xBD 0x59F GUI:Accept (W9-03-09): a toggle; the acceptance picture
        // left of the row shows the state.
        return {btn(1, mt_.accept, joined(), A_READY), btn(5, text_.back, true, A_BACK)};
    }
    std::string header_text() const override {
        if (choosing_) return text_.choose;
        // W9-03-19: 0x694 GUI:HostScreen / GUI:JoinScreen.
        return is_host() ? mt_.host_screen : mt_.join_screen;
    }
    int back_action() const override { return choosing_ ? A_CANCEL : A_BACK; }

    void press(int act) override {
        switch (act) {
            case A_START: {
                if (!host_) return;
                const ra2mp::StartCheck c = host_->start(start_seed());
                if (c != ra2mp::StartCheck::Ok) ask(C_INFO, ra2mp::start_check_text(c), {text_.ok});
                else mpui::log_line("start requested seed=" + std::to_string(host_->model().seed));
                return;
            }
            case A_MAP: {
                choosing_ = true;
                type_list_ = ShellList{};
                type_list_.r = type_list_rect();
                type_list_.visible = 16;
                for (const mpui::GameMode& m : mpui::game_modes()) {
                    if (mode_map_count(m) == 0) continue;   // a game type without maps is not offered
                    type_list_.items.push_back(mpui::yr_text(m.name_key, m.name_key));
                    type_list_.data.push_back(m.index);
                    if (m.index == model().mode) type_list_.sel = static_cast<int>(type_list_.items.size()) - 1;
                }
                if (type_list_.sel < 0 && !type_list_.items.empty()) type_list_.sel = 0;
                fill_map_list(model().map.file);
                return;
            }
            case A_USEMAP: {
                const int mi = map_list_.sel >= 0 && map_list_.sel < static_cast<int>(map_list_.data.size())
                                   ? map_list_.data[static_cast<std::size_t>(map_list_.sel)] : -1;
                if (host_ && mi >= 0) {
                    const mpui::GameMode& gm = mpui::game_mode(chosen_mode());
                    const ra2mp::MpMap mm = map_info(mi);
                    // W9-03-13: TXT_SCENARIO_TOO_SMALL (FUN_005DBD30 @0x5DC934) --
                    // the host stays in the chooser and nobody is removed.
                    const int seated = gm.ai_allowed() ? ra2mp::player_count(model()) : ra2mp::human_count(model());
                    if (seated > mm.max_players) {
                        ask(C_INFO, skp::fmt_d(text_.too_small, mm.max_players), {text_.ok});
                        return;
                    }
                    if (gm.index != model().mode) host_->set_mode(gm.index, gm.ai_allowed(), gm.allies_allowed);
                    if (!host_->set_map(mm)) {
                        ask(C_INFO, skp::fmt_d(text_.too_small, mm.max_players), {text_.ok});
                        return;
                    }
                    map_ix_ = mi;
                    if (link_) link_->update_room(ra2mp::player_count(model()), model().map.max_players, map_display(), false);
                }
                choosing_ = false;
                return;
            }
            case A_RANDMAP: return;   // greyed (no random map generator)
            case A_CANCEL: choosing_ = false; return;
            case A_READY:
                if (client_) client_->set_ready(!client_->ready());
                return;
            case A_BACK: leave(); return;
            default: return;
        }
    }
    std::uint32_t start_seed() const {
        // Host-chosen seed: wall clock + player id (any value works; every
        // machine receives it in GO and seeds identically from it).
        return static_cast<std::uint32_t>(ra2mp::now_ms() * 2654435761ull) ^ static_cast<std::uint32_t>(prefs_.pid) ^
               gf_seed_of(model().map.file);
    }
    // --- Customize Battle (0x6B) ---------------------------------------------------
    int chosen_mode() const {
        return type_list_.sel >= 0 && type_list_.sel < static_cast<int>(type_list_.data.size())
                   ? type_list_.data[static_cast<std::size_t>(type_list_.sel)] : model().mode;
    }
    int mode_map_count(const mpui::GameMode& m) const {
        int n = 0;
        for (const ra2engine::MapEntry& e : maps()) n += mpui::map_has_mode(e, m.filter) ? 1 : 0;
        return n;
    }
    // The Game Map list = the maps whose GameMode= carries the type's filter.
    void fill_map_list(const std::string& keep_file) {
        map_list_ = ShellList{};
        map_list_.r = map_list_rect();
        map_list_.visible = 16;
        const std::string tag = mpui::game_mode(chosen_mode()).filter;
        for (int i = 0; i < static_cast<int>(maps().size()); ++i) {
            const ra2engine::MapEntry& e = maps()[static_cast<std::size_t>(i)];
            if (!mpui::map_has_mode(e, tag)) continue;
            map_list_.items.push_back(e.display_name);
            map_list_.data.push_back(i);
            if (e.file_name == keep_file) map_list_.sel = static_cast<int>(map_list_.items.size()) - 1;
        }
        if (map_list_.sel < 0 && !map_list_.items.empty()) map_list_.sel = 0;
        if (map_list_.sel >= 0) map_list_.reveal(map_list_.sel);
    }

    void leave() {
        announcer_.close();
        if (host_) host_->close();
        if (client_) client_->leave();
        if (link_ && role_ == HOST) link_->close_room();
        next_ = make_return_page();
    }
    Scene* make_return_page();   // defined after the browser pages

    // --- combos -------------------------------------------------------------------
    void open_combo(Combo kind, int row) {
        combo_ = kind;
        combo_row_ = row;
        list_ = ShellList{};
        const ra2mp::MpSlot& s = model().slots[row];
        ra2engine::SbRect face = kind == CB_KIND ? kind_rect(row) : kind == CB_COUNTRY ? country_rect(row)
                                 : kind == CB_COLOR ? color_rect(row) : kind == CB_TEAM ? team_rect(row) : start_rect(row);
        int cur = 0;
        auto add = [&](const std::string& label, int v, bool sel) {
            list_.items.push_back(label);
            list_.data.push_back(v);
            if (sel) cur = static_cast<int>(list_.items.size()) - 1;
        };
        switch (kind) {
            case CB_KIND:   // data: -1 open, -2 closed, 0..2 AI level
                add("Open", -1, s.kind == ra2mp::SlotKind::Open);
                add("Closed", -2, s.kind == ra2mp::SlotKind::Closed);
                if (mode().ai_allowed()) {   // W9-03-04: humans-only game types offer no AI
                    add(text_.ai[1], ra2engine::kLobbyAiEasy, s.kind == ra2mp::SlotKind::AI && s.ai == ra2engine::kLobbyAiEasy);
                    add(text_.ai[2], ra2engine::kLobbyAiMedium, s.kind == ra2mp::SlotKind::AI && s.ai == ra2engine::kLobbyAiMedium);
                    add(text_.ai[3], ra2engine::kLobbyAiBrutal, s.kind == ra2mp::SlotKind::AI && s.ai == ra2engine::kLobbyAiBrutal);
                }
                list_.visible = static_cast<int>(list_.items.size());
                face.w = 150;
                break;
            case CB_COUNTRY:
                add(text_.random_country, ra2mp::kRandom, s.country < 0);
                for (int c = 0; c < static_cast<int>(countries_.size()); ++c) add(country_label(c), c, c == s.country);
                list_.visible = 7;
                break;
            case CB_COLOR:
                add(text_.random_color, ra2mp::kRandom, s.color < 0);
                list_.swatch.push_back(SDL_Color{0, 0, 0, 0});
                for (int c = 0; c < 8; ++c) {
                    if (ra2mp::color_owner(model(), c, row) >= 0) continue;
                    add(color_label(c), c, c == s.color);
                    Uint8 r, g, b;
                    ra2engine::lobby_color_rgb(c, r, g, b);
                    list_.swatch.push_back(SDL_Color{r, g, b, 255});
                }
                list_.visible = 9;
                face.w += 22;
                break;
            case CB_TEAM:
                for (int t = -1; t < 4; ++t) add(team_label(t), t, t == s.team);
                list_.visible = 5;
                break;
            case CB_START:
                add("?", ra2mp::kRandomStart, s.start < 0);
                for (int st = 0; st < model().map.max_players; ++st)
                    if (ra2mp::start_owner(model(), st, row) < 0) add(start_label(st), st, st == s.start);
                list_.visible = 9;
                break;
            default: break;
        }
        list_.sel = cur;
        const int h = list_.rows() * list_.item_h + 2;
        int y = face.y + face.h;
        if (y + h > 448) y = face.y - h;
        list_.r = {face.x, y, face.w, h};
        list_.reveal(cur);
        play_ui("menu:acbopen");
    }
    void close_combo(bool commit) {
        if (combo_ == CB_NONE) return;
        if (commit && list_.sel >= 0 && list_.sel < static_cast<int>(list_.data.size())) {
            const int v = list_.data[static_cast<std::size_t>(list_.sel)];
            const int k = combo_row_;
            const ra2mp::MpSlot& s = model().slots[k];
            if (combo_ == CB_KIND && host_) {
                host_->set_slot_kind(k, v == -1 ? ra2mp::SlotKind::Open : v == -2 ? ra2mp::SlotKind::Closed : ra2mp::SlotKind::AI,
                                     v >= 0 ? v : 1);
            } else {
                int country = s.country, color = s.color, team = s.team, start = s.start;
                if (combo_ == CB_COUNTRY) country = v;
                if (combo_ == CB_COLOR) color = v;
                if (combo_ == CB_TEAM) team = v;
                if (combo_ == CB_START) start = v;
                if (host_) host_->set_slot(k, country, color, team, start);
                else if (client_) client_->request_slot(country, color, team, start);
            }
        }
        combo_ = CB_NONE;
        play_ui("menu:acbclose");
    }

    // --- input ---------------------------------------------------------------------
    bool body_modal(int lx, int ly) override {
        if (choosing_) {
            const int t = type_list_.hit(lx, ly);
            if (t >= 0) {
                if (t != type_list_.sel) {
                    const std::string keep = map_list_.sel >= 0 && map_list_.sel < static_cast<int>(map_list_.data.size())
                                                 ? maps()[static_cast<std::size_t>(map_list_.data[static_cast<std::size_t>(map_list_.sel)])].file_name
                                                 : model().map.file;
                    type_list_.sel = t;
                    fill_map_list(keep);
                }
                play_ui("menu:click");
                return true;
            }
            if (map_list_.scrolls() && map_list_.strip_up().contains(lx, ly)) { map_list_.scroll(-map_list_.visible); return true; }
            if (map_list_.scrolls() && map_list_.strip_down().contains(lx, ly)) { map_list_.scroll(map_list_.visible); return true; }
            const int i = map_list_.hit(lx, ly);
            if (i >= 0) { map_list_.sel = i; play_ui("menu:click"); return true; }
            return false;
        }
        if (combo_ == CB_NONE) return false;
        if (list_.scrolls() && list_.strip_up().contains(lx, ly)) { list_.scroll(-1); return true; }
        if (list_.scrolls() && list_.strip_down().contains(lx, ly)) { list_.scroll(+1); return true; }
        const int i = list_.hit(lx, ly);
        if (i >= 0) { list_.sel = i; close_combo(true); }
        else close_combo(false);
        return true;
    }
    void body_click(int lx, int ly) override {
        if (choosing_ || !joined()) return;
        for (int k = 0; k < visible_rows(); ++k) {
            if (is_host() && k > 0 && model().slots[k].kind != ra2mp::SlotKind::Human && kind_rect(k).contains(lx, ly)) {
                open_combo(CB_KIND, k);
                return;
            }
            if (!can_edit(k)) continue;
            if (country_rect(k).contains(lx, ly)) { open_combo(CB_COUNTRY, k); return; }
            if (color_rect(k).contains(lx, ly)) { open_combo(CB_COLOR, k); return; }
            if (team_rect(k).contains(lx, ly) && mode().allies_allowed) { open_combo(CB_TEAM, k); return; }
            if (start_rect(k).contains(lx, ly)) { open_combo(CB_START, k); return; }
        }
        if (!host_) {
            // W9-03-12: a guest clicking a host-only option (TXT_ONLY_HOST_CAN_MODIFY).
            bool host_only = false;
            for (int c = 0; c < CK_COUNT; ++c) host_only = host_only || check_rect(c).contains(lx, ly);
            for (int s2 = 0; s2 < SL_COUNT; ++s2) host_only = host_only || slider_rect(s2).contains(lx, ly);
            for (int k = 0; k < visible_rows(); ++k)
                if (!can_edit(k) && (country_rect(k).contains(lx, ly) || color_rect(k).contains(lx, ly) ||
                                     team_rect(k).contains(lx, ly) || start_rect(k).contains(lx, ly) ||
                                     (k > 0 && kind_rect(k).contains(lx, ly) && k != me())))
                    host_only = true;
            if (host_only) {
                flash(mt_.only_host);
                play_ui("menu:click");
            }
            return;
        }
        // Host: a click on a guest's name offers to remove them.
        if (is_host())
            for (int k = 1; k < visible_rows(); ++k)
                if (model().slots[k].kind == ra2mp::SlotKind::Human && kind_rect(k).contains(lx, ly)) {
                    kick_row_ = k;
                    ask(C_LEAVE, "Remove " + model().slots[k].name + " from the game?", {text_.ok, text_.cancel});
                    return;
                }
        if (!host_) return;
        for (int c = 0; c < CK_COUNT; ++c)
            if (check_rect(c).contains(lx, ly)) {
                ra2mp::MpOptions o = model().opt;
                bool* f = c == CK_SHORT ? &o.short_game : c == CK_MCV ? &o.mcv_repacks : c == CK_CRATES ? &o.crates
                        : c == CK_AIT ? &o.ai_takeover : c == CK_XP ? &o.cross_platform
                        : c == CK_BOA ? &o.build_off_ally : &o.superweapons;
                *f = !*f;
                host_->set_options(o);
                play_ui("menu:click");
                return;
            }
        for (int s = 0; s < SL_COUNT; ++s) {
            const ra2engine::SbRect r = slider_rect(s);
            const ra2engine::SbRect track{r.x, r.y, r.w - ra2engine::kTrackbarReadoutW, r.h};
            if (!track.contains(lx, ly)) continue;
            const int tx = r.x + ra2engine::trackbar_thumb_x(slider_pos(s), slider_range(s), r.w, ra2engine::kTrackbarReadoutW);
            if (lx >= tx && lx < tx + ra2engine::kTrackbarThumbW) drag_ = s;
            else set_slider(s, ra2engine::trackbar_pos_from_x(lx - r.x, slider_range(s), r.w, ra2engine::kTrackbarReadoutW));
            return;
        }
    }
    void body_drag(const ra2engine::InputEvent& ev, int lx, int /*ly*/) override {
        if (drag_ < 0) return;
        const ra2engine::SbRect r = slider_rect(drag_);
        set_slider(drag_, ra2engine::trackbar_pos_from_x(lx - r.x, slider_range(drag_), r.w, ra2engine::kTrackbarReadoutW));
        if (ev.kind == ra2engine::InputEvent::PointerUp) drag_ = -1;
    }
    // W9-04-05: a finger drag -- on a list it scrolls it, on a slider (from
    // its track, thumb or label row) it moves the thumb under the finger.
    void body_pan(const ra2engine::InputEvent& ev, int sx, int sy, int lx, int /*ly*/) override {
        if (choosing_) {
            if (map_list_.r.contains(sx, sy)) list_pan(map_list_, ev);
            return;
        }
        if (combo_ != CB_NONE) {
            if (list_.r.contains(sx, sy)) list_pan(list_, ev);
            return;
        }
        if (!host_ || !joined()) return;
        for (int s = 0; s < SL_COUNT; ++s) {
            const ra2engine::SbRect r = slider_rect(s);
            if (sy < r.y - 3 || sy >= r.y + r.h + 3 || sx < r.x - 8 || sx >= r.x + r.w) continue;
            set_slider(s, ra2engine::trackbar_pos_from_x(lx - r.x, slider_range(s), r.w, ra2engine::kTrackbarReadoutW));
            return;
        }
    }
    void touch_extra(std::vector<ra2engine::SbRect>& out) const override {
        if (choosing_ || combo_ != CB_NONE || !host_ || !joined()) return;
        for (int c = 0; c < CK_COUNT; ++c) out.push_back(check_rect(c));
        for (int s = 0; s < SL_COUNT; ++s) {
            const ra2engine::SbRect r = slider_rect(s);
            out.push_back({r.x, r.y, r.w - ra2engine::kTrackbarReadoutW, r.h});
        }
    }
    void page_lists(std::vector<ShellList*>& out) override {
        out.push_back(&map_list_);
        out.push_back(&list_);
    }
    // W9-04-07: the lobby's AWAY / BACK (the match has its own, game_scene_net.inc).
    void on_background() override {
        if (bg_away_) return;
        bg_away_ = true;
        if (host_) host_->away(false, kLobbyAwaySeconds);
        else if (client_) client_->away(false, kLobbyAwaySeconds);
        set_focus(-1);
        mpui::log_line("app in the background: lobby AWAY sent (" + std::to_string(kLobbyAwaySeconds) + " s)");
    }
    void on_foreground() override {
        if (!bg_away_) return;
        bg_away_ = false;
        const std::uint64_t now = ra2mp::now_ms();
        if (host_) {
            host_->resume(now);
            host_->away(true, 0);
        } else if (client_) {
            client_->resume(now);
            client_->away(true, 0);
        }
        mpui::log_line("app in the foreground: lobby BACK sent");
    }
    bool body_key(const ra2engine::InputEvent& ev) override {
        if (combo_ != CB_NONE) {
            if (ev.key == ra2engine::kKeyEscape) close_combo(false);
            else if (ev.key == ra2engine::kKeyEnter) close_combo(true);
            else if (ev.key == ra2engine::kKeyUp && list_.sel > 0) list_.reveal(--list_.sel);
            else if (ev.key == ra2engine::kKeyDown && list_.sel + 1 < static_cast<int>(list_.items.size())) list_.reveal(++list_.sel);
            return true;
        }
        if (choosing_ && ev.key == ra2engine::kKeyEnter) { press(A_USEMAP); return true; }
        // W9-04-09: Esc / Android Back (an edge swipe is easy to make by
        // accident) asks first when leaving would drop other players -- the
        // host closes the game for everyone. The Back button leaves at once
        // (a deliberate click, as in the original).
        if (ev.key == ra2engine::kKeyEscape && !choosing_ && combo_ == CB_NONE && leave_needs_confirm()) {
            set_focus(-1);
            ask(C_QUIT, !is_host() ? "Leave the game?"
                        : host_ && host_->members() > 0 ? "Close the game for everyone?" : "Close this game?",
                {text_.ok, text_.cancel});
            return true;
        }
        return false;
    }
    // The host always (leaving closes the room: its code / the LAN listing
    // are gone), a joined client too.
    bool leave_needs_confirm() const {
        if (is_host()) return host_ != nullptr;
        return client_ && client_->state() == ra2mp::LobbyClient::State::Joined;
    }
    void on_enter(int edit) override {
        if (edits_[static_cast<std::size_t>(edit)].id == "chat") {
            const std::string t = edits_[static_cast<std::size_t>(edit)].text;
            edits_[static_cast<std::size_t>(edit)].text.clear();
            ime_sync("", ra2engine::kOsImeChat, 100);   // W9-04-01: the keyboard's copy starts over
            if (!t.empty()) {
                if (host_) host_->chat(t);
                else if (client_) client_->chat(t);
            }
        }
    }
    void confirm_done(int action, int button) override {
        if (action == C_QUIT && button == 0) leave();   // W9-04-09
        if (action == C_LEAVE && button == 0 && host_ && kick_row_ > 0) host_->kick(kick_row_);
        if (action == C_INFO && client_ && (client_->state() == ra2mp::LobbyClient::State::Rejected ||
                                             client_->state() == ra2mp::LobbyClient::State::Lost))
            leave();
    }

    // --- per frame -----------------------------------------------------------------
    void tick(std::uint64_t now) override {
        if (host_) host_->update(now);
        if (client_) client_->update(now);
        if (ra2mp::human_count(model()) != last_humans_) {
            last_humans_ = ra2mp::human_count(model());
            humans_since_ms_ = now;
        }
        announcer_.update(now);
        if (announcer_.is_open() && host_) {
            ra2mp::LanBeacon b = announcer_.beacon();
            b.players = static_cast<std::uint8_t>(ra2mp::player_count(model()));
            b.max = static_cast<std::uint8_t>(model().map.max_players);
            b.map = map_display();
            b.started = host_->started();
            b.cross_platform = model().opt.cross_platform;   // W9-04-13
            announcer_.set(b);
        }
        if (link_) {
            link_->update(now);
            while (!link_->events().empty()) {
                const ra2mp::ServerEvent ev = link_->events().front();
                link_->events().pop_front();
                if (ev.kind == ra2mp::ServerEvent::Created) {
                    code_ = ev.code;
                    where_ = "Room code " + code_ + (private_ ? " (private)" : "");
                    lobby().system_line("Room code: " + code_ + "  -- give it to your friends.");
                    mpui::log_line("room created code=" + code_);
                } else if (ev.kind == ra2mp::ServerEvent::Peer) {
                    mpui::log_line("server: " + ev.nick + " is joining (tunnel id " + std::to_string(ev.v3) + ")");
                } else if (ev.kind == ra2mp::ServerEvent::RoomClosed && client_) {
                    // The host's control connection dropped; the lobby link may still live.
                    mpui::log_line("server: room closed");
                } else if (ev.kind == ra2mp::ServerEvent::Error) {
                    status_ = "Server: " + ev.op + " failed (" + ev.reason + ")";
                } else if (ev.kind == ra2mp::ServerEvent::Disconnected) {
                    status_ = "Lost the connection to the server (the game lobby continues).";
                }
            }
            if (host_ && link_->state() == ra2mp::ServerLink::State::Ready) {
                const int players = ra2mp::player_count(model());
                if (players != last_players_ || host_->started() != last_started_) {
                    link_->update_room(players, model().map.max_players, map_display(), host_->started());
                    last_players_ = players;
                    last_started_ = host_->started();
                }
            }
        }
        if (client_) {
            const auto st = client_->state();
            if ((st == ra2mp::LobbyClient::State::Rejected || st == ra2mp::LobbyClient::State::Lost) &&
                !confirm_.open() && !told_) {
                told_ = true;
                std::string msg = client_->reject_message();   // W9-05-02: names both versions
                mpui::log_line("join refused: " + (msg.empty() ? std::string("left the game") : msg));
                if (msg.empty()) msg = "You left the game.";
                // W9-04-15(a): a host that never answered was never "lost".
                if (st == ra2mp::LobbyClient::State::Lost && client_->local_slot() < 0)
                    msg = "No answer from the host. Check the address, and that both devices are on the same network.";
                ask(C_INFO, msg, {text_.ok});
            }
            if (st == ra2mp::LobbyClient::State::Joining) status_ = "Contacting the host...";
            else if (st == ra2mp::LobbyClient::State::Joined)
                status_ = client_->ready() ? "Waiting for the host to start the game."
                                           : "Press Accept when you are ready to play.";
            else if (st != ra2mp::LobbyClient::State::Joining && status_ == "Contacting the host...") status_.clear();
        }
        if (host_ && !confirm_.open()) {
            const ra2mp::StartCheck c = ra2mp::can_start(model());
            status_ = c == ra2mp::StartCheck::Ok ? "Everyone is ready." : ra2mp::start_check_text(c);
        }
        if (lobby().started() && !handed_off_) {
            handed_off_ = true;
            ra2mp::MpSessionConfig s = lobby().session();
            ra2mp::resolve_session(s, static_cast<int>(countries_.size()));
            s.transport = net_ == LAN ? ra2mp::MpTransportKind::Lan : ra2mp::MpTransportKind::Tunnel;
            if (net_ == LAN) s.host_addr = client_ ? transport_->describe(client_->host()) : ("0.0.0.0:" + std::to_string(port_));
            else if (link_) s.host_addr = link_->tunnel_addr().str();
            s.local_v3 = my_v3_;
            s.host_v3 = is_host() ? my_v3_ : host_v3_;
            // (MpPlayer::peer: resolve_session copied GO's peer table.)
            announcer_.close();
            next_ = mp_start_session(rules_, no_audio_, art_, s, transport_);
        }
    }

    // --- drawing ---------------------------------------------------------------------
    // W9-03-23: the decoded preview lives in the shared TextureCache (it
    // outlives this scene); its size is read back from the texture itself, so
    // a second lobby in the session draws it too.
    SDL_Texture* preview_tex(ra2engine::TextureCache& tc, int map_i, int& w, int& h) {
        w = h = 0;
        if (map_i < 0 || map_i >= static_cast<int>(maps().size())) return nullptr;
        const std::string& f = maps()[static_cast<std::size_t>(map_i)].file_name;
        const std::string key = "skp:prev:" + f;
        SDL_Texture* t = static_cast<SDL_Texture*>(tc.peek(key));
        if (!t) {
            if (no_preview_.count(f)) return nullptr;
            std::vector<uint8_t> rgba;
            int dw = 0, dh = 0;
            skp::decode_preview(map_text(map_i), dw, dh, rgba);
            if (dw <= 0 || dh <= 0) {
                no_preview_.insert(f);
                return nullptr;
            }
            t = static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), dw, dh));
        }
        if (!t || SDL_QueryTexture(t, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) return nullptr;
        return t;
    }

    void draw_body(const ShellPainter& p, ra2engine::TextureCache& tc) override {
        edits_[0].enabled = joined() && !choosing_;
        // Preview well 0x468 + game type 0x6EC + map name 0x5A8 + where (LAN address / room code).
        const ra2engine::SbRect pr = preview_rect();
        p.fill(p.dev(pr), 0, 0, 0);
        const int mi = choosing_ ? (map_list_.sel >= 0 && map_list_.sel < static_cast<int>(map_list_.data.size())
                                        ? map_list_.data[static_cast<std::size_t>(map_list_.sel)] : -1)
                                 : map_index_of(model().map.file);
        int pw = 0, ph = 0;
        preview_w_ = 0;
        if (SDL_Texture* t = preview_tex(tc, mi, pw, ph)) {
            const double sc = std::min(double(pr.w - 2) / pw, double(pr.h - 2) / ph);
            const int w = std::max(1, int(pw * sc)), h = std::max(1, int(ph * sc));
            const SDL_Rect d = p.dev(pr.x + (pr.w - w) / 2, pr.y + (pr.h - h) / 2, w, h);
            SDL_RenderCopy(p.ren, t, nullptr, &d);
            preview_w_ = w;
        }
        if (choosing_) {
            p.header(text_.sel_engagement, 34, 32, 385, 19);
            p.label(text_.game_type, 30, 97, 195, 16, 0);
            p.label(text_.game_map, 252, 97, 195, 16, 0);
            type_list_.draw(p, false);
            map_list_.draw(p, false);
            return;
        }
        p.label(p.fit(where_, 150), 486, 151, 150, 16, 1);
        p.label(p.fit(mpui::yr_text(mode().name_key, mode().name_key), 135), 490, 167, 135, 16, 1);
        p.label(p.fit(map_display(), 150), 486, 188, 150, 16, 1);
        if (client_ && client_->state() == ra2mp::LobbyClient::State::Joined && !model().slots[me()].has_map)
            p.label("MAP MISSING", 486, 206, 150, 12, 1);

        // W9-03-06: the column headers (0x796 / 0x791 / 0x792 / 0x793 / 0x794).
        const mpui::LobbyLayout& L = lay();
        p.header(mt_.players, L.name_x, L.head_y, L.name_w, 16, 0);
        p.header(mt_.side, L.side_x, L.head_y, L.side_w, 16, 0);
        p.header(mt_.color, L.color_x, L.head_y, L.color_w, 16, 0);
        p.header(mt_.start, L.start_x, L.head_y, L.start_w, 16, 0);
        p.header(mt_.team, L.team_x, L.head_y, L.team_w, 16, 0);
        for (int k = 0; k < visible_rows(); ++k) {
            const ra2mp::MpSlot& s = model().slots[k];
            const bool occ = s.occupied();
            const bool kind_en = is_host() && k > 0 && s.kind != ra2mp::SlotKind::Human;
            std::string nm = kind_label(k);
            if (s.kind == ra2mp::SlotKind::Human && k == me()) nm += " (you)";
            if (kind_en) p.combo(kind_rect(k), nm, joined(), combo_ == CB_KIND && combo_row_ == k);
            else {
                p.frame(p.dev(kind_rect(k)), occ);
                p.label(p.fit(nm, kind_rect(k).w - 8), kind_rect(k).x + 4, kind_rect(k).y, kind_rect(k).w - 8, 19, 0, occ);
            }
            // W9-03-08: the acceptance picture (0x6D2..) left of the player:
            // lit once the player is checked in (the host always is).
            if (s.kind == ra2mp::SlotKind::Human) {
                const bool lit = s.host || s.ready;
                const ra2engine::SbRect ar = accept_rect(k);
                p.fill(p.dev(ar), lit ? 40 : 60, lit ? 200 : 0, 0);
                p.frame(p.dev(ar), true);
            }
            const bool ed = can_edit(k);
            if (!occ) continue;
            // W9-03-07: the flag picture (0x6DA..; FUN_004D1120: the country's
            // PCX, rani.pcx for Random) -- the Skirmish page's painter.
            const int fi = (s.country >= 0 && s.country < 9) ? s.country : 9;
            if (art_.flags[fi]) {
                const ra2engine::SbRect fr = flag_rect(k);
                const std::string key = "skp:flag" + std::to_string(fi);
                p.pcx(key.c_str(), art_.flags[fi], p.dev(fr.x, fr.y - 2, art_.flags[fi]->w, art_.flags[fi]->h),
                      /*key_magenta=*/true);
            }
            p.combo(country_rect(k), country_label(s.country), ed, combo_ == CB_COUNTRY && combo_row_ == k);
            SDL_Color sw{0, 0, 0, 0};
            if (s.color >= 0) {
                Uint8 r, g, b;
                ra2engine::lobby_color_rgb(s.color, r, g, b);
                sw = SDL_Color{r, g, b, 255};
            }
            p.combo(color_rect(k), color_label(s.color), ed, combo_ == CB_COLOR && combo_row_ == k, sw.a ? &sw : nullptr);
            p.combo(start_rect(k), start_label(s.start), ed, combo_ == CB_START && combo_row_ == k);
            p.combo(team_rect(k), team_label(s.team), ed && mode().allies_allowed, combo_ == CB_TEAM && combo_row_ == k);
        }
        for (int c = 0; c < CK_COUNT; ++c) p.checkbox(check_rect(c), check_val(c), check_label(c), host_ != nullptr && joined());
        const std::string sl[SL_COUNT] = {text_.speed, text_.credits, text_.units};
        for (int s = 0; s < SL_COUNT; ++s) {
            const ra2engine::SbRect lr = slider_label(s);
            p.label(sl[s], lr.x, lr.y, lr.w, lr.h, 0);
            const ra2engine::SbRect r = slider_rect(s);
            const bool en = host_ != nullptr && joined();
            p.trackbar(r, slider_pos(s), slider_range(s), true, slider_text(s), en);
            // W9-03-02: 0xBD keeps showing the values on a guest (only the
            // thumb is disabled): the readout is redrawn at full brightness.
            if (!en) p.label(slider_text(s), r.x + r.w - 49, r.y, 49, r.h, 1, true);
        }
        // Chat log 0x53F (last lines that fit).
        const ra2engine::SbRect cr = chat_rect();
        p.fill(p.dev(cr), 0, 0, 0, 160);
        p.frame(p.dev(cr), true);
        const auto& log = lobby().chat_log();
        const int lines = cr.h / 16;
        int y = cr.y + 2;
        const int first = std::max(0, static_cast<int>(log.size()) - lines);
        for (int i = first; i < static_cast<int>(log.size()); ++i) {
            const ra2mp::ChatLine& l = log[static_cast<std::size_t>(i)];
            const std::string s = l.from.empty() ? l.text : l.from + ": " + l.text;
            p.label(p.fit(s, cr.w - 8), cr.x + 4, y, cr.w - 8, 16, 0);
            y += 16;
        }
    }
    void draw_overlay(const ShellPainter& p) override {
        if (combo_ != CB_NONE) list_.draw(p);
    }

    // --- tooltips (status line 0x695; STT:Host* on 0xBC, STT:Guest* on 0xBD) ----------
    std::string hover_tip(int lx, int ly) const override {
        if (confirm_.open() || combo_ != CB_NONE) return {};
        const char* who = is_host() ? "STT:Host" : "STT:Guest";
        auto tip = [&](const std::string& k) { return mpui::yr_text(who + k, ""); };
        if (choosing_) {
            const int t = type_list_.hit(lx, ly);
            if (t >= 0 && t < static_cast<int>(type_list_.data.size()))
                return mpui::yr_text(mpui::game_mode(type_list_.data[static_cast<std::size_t>(t)]).stt_key, "");
            return {};
        }
        for (const ShellButton& b : buttons())
            if (b.rect.contains(lx, ly)) {
                if (b.action == A_START) return tip("ButtonGo");
                if (b.action == A_MAP) return tip("ButtonChooseMap");
                if (b.action == A_READY) return tip("ButtonAccept");
                if (b.action == A_BACK) return tip("ButtonBack");
            }
        if (preview_rect().contains(lx, ly)) return tip("MapThumbnail");
        if (ra2engine::SbRect{486, 186, 150, 32}.contains(lx, ly)) return tip("LabelScenario");
        for (int k = 0; k < visible_rows(); ++k) {
            if (accept_rect(k).contains(lx, ly)) return tip("PictureAcceptance");
            if (kind_rect(k).contains(lx, ly)) return tip("ComboPlayer");
            if (flag_rect(k).contains(lx, ly)) return tip("PictureFlag");
            if (country_rect(k).contains(lx, ly)) return tip("ComboCountry");
            if (color_rect(k).contains(lx, ly)) return tip("ComboColor");
        }
        static const char* kChk[CK_COUNT] = {"CBoxShortGame", "CBoxRedeploys", "CBoxCrates", "CBoxSWAllowed",
                                             "CBoxBuildOffAlly", "", ""};
        for (int c = 0; c < CK_COUNT; ++c)
            if (check_rect(c).contains(lx, ly)) {
                if (c == CK_AIT) return "A player who leaves is replaced by a computer player.";
                if (c == CK_XP) return "Admit Windows and Android players into one game (experimental).";
                return tip(kChk[c]);
            }
        static const char* kSl[SL_COUNT] = {"SliderSpeed", "SliderCredits", "SliderUnit"};
        for (int s = 0; s < SL_COUNT; ++s)
            if (slider_rect(s).contains(lx, ly)) return tip(kSl[s]);
        if (chat_rect().contains(lx, ly)) return tip("EditOutput");
        if (edits_[0].r.contains(lx, ly)) return tip("EditInput");
        return {};
    }
    // --- --play ------------------------------------------------------------------------
    void body_targets(std::map<std::string, ra2engine::SbRect>& t) const override {
        // Old button names (scripts of other branches): Ready -> Accept.
        if (!choosing_ && !is_host() && joined())
            for (const ShellButton& b : buttons())
                if (b.action == A_READY) t["button:Ready"] = b.rect;
        if (choosing_) {
            for (int i = type_list_.top; i < type_list_.top + type_list_.rows(); ++i) {
                const mpui::GameMode& gm = mpui::game_mode(type_list_.data[static_cast<std::size_t>(i)]);
                t["mode:" + std::to_string(gm.index)] = type_list_.item_rect(i);
                t["mode:" + type_list_.items[static_cast<std::size_t>(i)]] = type_list_.item_rect(i);
            }
            for (int i = map_list_.top; i < map_list_.top + map_list_.rows(); ++i)
                t["map:" + maps()[static_cast<std::size_t>(map_list_.data[static_cast<std::size_t>(i)])].file_name] =
                    map_list_.item_rect(i);
            if (map_list_.scrolls()) {
                t["list:up"] = map_list_.strip_up();
                t["list:down"] = map_list_.strip_down();
            }
            t["list:maps"] = map_list_.r;   // the whole list (touch drag rel ...)
            return;
        }
        if (combo_ != CB_NONE) {
            for (int i = list_.top; i < list_.top + list_.rows(); ++i) {
                t["item:" + std::to_string(i)] = list_.item_rect(i);
                t["pick:" + list_.items[static_cast<std::size_t>(i)]] = list_.item_rect(i);
            }
            if (list_.scrolls()) {
                t["list:up"] = list_.strip_up();
                t["list:down"] = list_.strip_down();
            }
            return;
        }
        if (!joined()) return;
        for (int k = 0; k < visible_rows(); ++k) {
            const std::string n = std::to_string(k);
            if (is_host() && k > 0) t["combo:kind" + n] = kind_rect(k);
            if (!can_edit(k)) continue;
            t["combo:country" + n] = country_rect(k);
            t["combo:color" + n] = color_rect(k);
            t["combo:team" + n] = team_rect(k);
            t["combo:start" + n] = start_rect(k);
        }
        for (int k = 0; k < visible_rows(); ++k) {   // hover / tap targets of the row pictures
            t["accept:" + std::to_string(k)] = accept_rect(k);
            t["flag:" + std::to_string(k)] = flag_rect(k);
        }
        if (!host_)   // a guest may still tap a host-only option (TXT_ONLY_HOST_CAN_MODIFY)
            for (int c = 0; c < CK_COUNT; ++c) t[std::string("guestcheck:") + check_id(c)] = {check_rect(c).x, check_rect(c).y, 18, 16};
        if (host_) {
            for (int c = 0; c < CK_COUNT; ++c) t[std::string("check:") + check_id(c)] = {check_rect(c).x, check_rect(c).y, 18, 16};
            for (int s = 0; s < SL_COUNT; ++s) {
                const ra2engine::SbRect r = slider_rect(s);
                const int range = slider_range(s);
                // The track itself (LobbyLayout; no readout): touch drag rel targets.
                t[std::string("slider:") + slider_id(s)] = {r.x, r.y, r.w - ra2engine::kTrackbarReadoutW, r.h};
                for (int pos = 0; pos <= range; ++pos) {
                    std::string v = s == SL_CREDITS ? std::to_string(min_money_ + pos * money_step_)
                                  : s == SL_UNITS ? std::to_string(min_units_ + pos) : std::to_string(pos);
                    t[std::string("slider:") + slider_id(s) + "=" + v] =
                        {r.x + shell_trackbar_x_for(pos, range, r.w, ra2engine::kTrackbarReadoutW), r.y + r.h / 2, 1, 1};
                }
            }
        }
    }
    void body_probe(ra2play::Probe& p) override {
        static const char* kKind[4] = {"Open", "Closed", "Human", "AI"};
        p.set_s("mp_role", is_host() ? "host" : "client");
        p.set_s("mp_net", net_ == LAN ? "lan" : "online");
        std::string st = "joined";
        if (client_) {
            static const char* kSt[5] = {"joining", "joined", "rejected", "lost", "started"};
            st = kSt[static_cast<int>(client_->state())];
            p.set_s("reject_reason", ra2mp::reject_text(client_->reject_reason()));
            p.set_s("reject_message", client_->reject_message());   // W9-05-02
        }
        if (lobby().started()) st = "started";
        p.set_s("mp_state", st);
        p.set("local_slot", me());
        p.set("members", host_ ? host_->members() : 0);
        p.set("rev", lobby().rev());
        p.set("players", ra2mp::player_count(model()));
        p.set("humans", ra2mp::human_count(model()));
        p.set("max_players", model().map.max_players);
        p.set_s("map", model().map.file);
        p.set_s("map_digest", model().map.digest);
        p.set_s("room_code", code_);
        p.set_s("where", where_);
        p.set("lan_port", port_);
        p.set("credits", model().opt.credits);
        p.set("unit_count", model().opt.unit_count);
        p.set("game_speed", model().opt.game_speed);
        for (int c = 0; c < CK_COUNT; ++c) p.set(std::string("check(") + check_id(c) + ")", check_val(c) ? 1 : 0);
        p.set("can_start", ra2mp::can_start(model()) == ra2mp::StartCheck::Ok ? 1 : 0);
        p.set("choosing", choosing_ ? 1 : 0);
        p.set("map_list_top", map_list_.top);   // W9-04-05
        p.set("map_count", static_cast<double>(maps().size()));
        // W9-03-04 game types.
        p.set("game_mode", model().mode);
        p.set_s("game_mode_name", mpui::yr_text(mode().name_key, mode().name_key));
        p.set("mode_ai_allowed", mode().ai_allowed() ? 1 : 0);
        p.set("mode_allies_allowed", mode().allies_allowed ? 1 : 0);
        p.set("mode_count", static_cast<double>(choosing_ ? type_list_.items.size() : mpui::game_modes().size()));
        p.set("mode_sel", chosen_mode());
        p.set("mode_map_count", static_cast<double>(choosing_ ? map_list_.items.size()
                                                              : static_cast<std::size_t>(mode_map_count(mode()))));
        for (const mpui::GameMode& gm : mpui::game_modes())
            p.set("maps_of_mode(" + std::to_string(gm.index) + ")", mode_map_count(gm));
        // W9-03-01 / 02 / 06 / 23.
        p.set("layout_overlaps", layout_overlaps());
        p.set("header_count", 5);
        for (int sl = 0; sl < SL_COUNT; ++sl) p.set_s(std::string("slider_text(") + slider_id(sl) + ")", slider_text(sl));
        p.set("preview_w", preview_w_);
        // Scripts: wall-clock seconds the seated human count has been unchanged.
        p.set("humans_stable_s", static_cast<double>((ra2mp::now_ms() - humans_since_ms_) / 1000));
        p.set_s("combo_open", combo_ == CB_NONE ? "" : combo_ == CB_KIND ? "kind" : combo_ == CB_COUNTRY ? "country"
                              : combo_ == CB_COLOR ? "color" : combo_ == CB_TEAM ? "team" : "start");
        for (int k = 0; k < ra2mp::kSlots; ++k) {
            const ra2mp::MpSlot& s = model().slots[k];
            const std::string n = "(" + std::to_string(k) + ")";
            p.set_s("slot_kind" + n, kKind[static_cast<int>(s.kind) & 3]);
            p.set_s("slot_name" + n, s.name);
            p.set("slot_ready" + n, s.ready ? 1 : 0);
            p.set("slot_has_map" + n, s.has_map ? 1 : 0);
            p.set("country" + n, s.country);
            p.set("color" + n, s.color);
            p.set("team" + n, s.team);
            p.set("start" + n, s.start);
            p.set("ai" + n, s.ai);
        }
        const auto& log = lobby().chat_log();
        p.set("chat_count", static_cast<double>(log.size()));
        p.set_s("chat_last", log.empty() ? "" : (log.back().from.empty() ? log.back().text : log.back().from + ": " + log.back().text));
        if (client_) p.set("ready", client_->ready() ? 1 : 0);
    }

    Role role_;
    Net net_;
    mpui::Prefs prefs_;
    std::vector<std::string> countries_;
    skp::Text text_;
    ra2mp::MpOptions defaults_;
    int min_money_ = 5000, max_money_ = 10000, money_step_ = 100, min_units_ = 0, max_units_ = 10;
    std::unique_ptr<ra2engine::MapCatalog> catalog_;
    std::map<std::string, std::string> digest_cache_;
    std::set<std::string> no_preview_;   // maps without a [PreviewPack]
    int preview_w_ = 0;                  // the drawn preview width (probe)
    int last_humans_ = -1;
    std::uint64_t humans_since_ms_ = 0;
    MpText mt_;
    ShellList type_list_;
    std::shared_ptr<ra2mp::MpNet> transport_;
    std::unique_ptr<ra2mp::LobbyHost> host_;
    std::unique_ptr<ra2mp::LobbyClient> client_;
    std::unique_ptr<ra2mp::ServerLink> link_;
    ra2mp::LanAnnouncer announcer_;
    std::uint16_t port_ = 0;
    std::uint32_t my_v3_ = 0, host_v3_ = 0;
    std::string code_, where_;
    bool private_ = false;
    int map_ix_ = -1;
    bool choosing_ = false;
    ShellList map_list_, list_;
    Combo combo_ = CB_NONE;
    int combo_row_ = 0, drag_ = -1, kick_row_ = -1;
    int last_players_ = -1;
    bool last_started_ = false, told_ = false, handed_off_ = false;
    bool bg_away_ = false;   // W9-04-07: we announced AWAY
};

// ===========================================================================
// Network = the Lobby page 0xBB (W9-03-11 / W9-03-22): Your Name, the
// Players on the page, the Games list (LAN beacons), lobby chat, New / Join
// / Main Menu (+ the clone's Join by IP).
// ===========================================================================
class MpLanScene : public MpPage {
public:
    MpLanScene(const Rules& r, bool na, const MenuArt& a) : MpPage(r, na, a) {
        // W9-04-10 (Android): LAN sockets (created from here on: browser,
        // lobby, match) go over the Wi-Fi even when mobile data is the
        // default network. Before any socket is opened.
        if (ra2engine::os_android_bind_lan(true) == 2)
            mpui::log_line("mobile data is the default network: LAN play is bound to the Wi-Fi");
        prefs_ = mpui::Prefs::load();
        edits_.push_back(mpui::Edit{"nick", {120, 16, 150, 19}, prefs_.nick, 16, true});
        edits_.push_back(mpui::Edit{"ip", {120, 404, 200, 19}, "", 40, true});
        edits_.back().ime = ra2engine::kOsImeUri;   // W9-04-14
        edits_.push_back(mpui::Edit{"lobbychat", {12, 376, 458, 19}, "", 100, true});
        edits_.back().ime = ra2engine::kOsImeChat;
        browser_.rules_crc = mpui::rules_crc();
        if (!browser_.open()) status_ = "Could not open the LAN discovery port.";
        presence_nonce_ = mpui::random_u32();
        browser_.set_presence(prefs_.nick, presence_nonce_);
        list_.r = {172, 58, 298, 196};
        list_.visible = 12;
        list_.item_h = 16;
        players_.r = {12, 58, 150, 196};
        players_.visible = 12;
        players_.item_h = 16;
        t_.header = mpui::yr_text("GUI:Lobby", "Lobby");
        t_.name = mpui::yr_text("GUI:YourName", "Your Name");
        t_.players = mpui::yr_text("GUI:Players", "Players");
        t_.games = mpui::yr_text("GUI:Games", "Games");
        t_.new_ = mpui::yr_text("GUI:New", "New");
        t_.join = mpui::yr_text("GUI:Join", "Join");
        t_.main = mpui::yr_text("GUI:MainMenu", "Main Menu");
    }
    ~MpLanScene() override { browser_.close(); }
    const char* name() const override { return "mp-lan"; }

private:
    enum Act { A_NONE, A_HOST, A_JOIN, A_JOINIP, A_BACK };
    struct Text { std::string header, name, players, games, new_, join, main; };
    std::vector<ShellButton> buttons() const override {
        // 0xBB: New 0x6CC (row 0), Join 0x6CD (row 1), Main Menu 0x5C0 (row 5);
        // the clone's Join by IP rides the hidden Observe row (2).
        return {btn(0, t_.new_, true, A_HOST), btn(1, t_.join, list_.sel >= 0, A_JOIN),
                btn(2, "Join by IP", !edits_[1].text.empty(), A_JOINIP), btn(5, t_.main, true, A_BACK)};
    }
    std::string header_text() const override { return t_.header; }
    int back_action() const override { return A_BACK; }
    void commit_nick() {
        const std::string n = ra2mp::sanitize_name(edits_[0].text);
        if (!n.empty() && n != prefs_.nick) {
            prefs_.nick = n;
            prefs_.save();
            browser_.set_presence(prefs_.nick, presence_nonce_);   // same player, new name
        }
    }
    void press(int act) override {
        commit_nick();
        switch (act) {
            case A_HOST:
                browser_.close();
                next_ = MpLobbyScene::host(rules_, no_audio_, art_, MpLobbyScene::LAN);
                return;
            case A_JOIN:
                if (list_.sel >= 0 && list_.sel < static_cast<int>(browser_.games().size())) {
                    const ra2mp::Addr h = browser_.games()[static_cast<std::size_t>(list_.sel)].host;
                    browser_.close();
                    next_ = MpLobbyScene::join_lan(rules_, no_audio_, art_, h);
                }
                return;
            case A_JOINIP: {
                ra2mp::Addr h;
                if (!ra2mp::resolve(edits_[1].text, ra2mp::kLanGamePort, h)) {
                    ask(0, "Cannot find \"" + edits_[1].text + "\". Type the host's address, e.g. 192.168.1.20 or 192.168.1.20:50020.", {"OK"});
                    return;
                }
                browser_.close();
                next_ = MpLobbyScene::join_lan(rules_, no_audio_, art_, h);
                return;
            }
            case A_BACK:
                browser_.close();
                next_ = new ShellScene(rules_, no_audio_, art_, PAGE_MAIN);
                return;
            default: return;
        }
    }
    void on_enter(int edit) override {
        const std::string& id = edits_[static_cast<std::size_t>(edit)].id;
        if (id == "lobbychat") {   // STT:LobbyEditInput "Type your message here and hit enter."
            std::string t = edits_[static_cast<std::size_t>(edit)].text;
            edits_[static_cast<std::size_t>(edit)].text.clear();
            while (!t.empty() && t.back() == ' ') t.pop_back();
            if (!t.empty()) {
                commit_nick();
                browser_.say(t);
            }
            return;   // keep typing
        }
        set_focus(-1);
        if (id == "ip") press(A_JOINIP);
        else commit_nick();
    }
    void tick(std::uint64_t now) override {
        browser_.update(now);
        const auto& g = browser_.games();
        list_.items.clear();
        list_.data.clear();
        for (std::size_t i = 0; i < g.size(); ++i) {
            const ra2mp::LanBeacon& b = g[i].beacon;
            std::string row = b.room + "  -  " + b.map + "  " + std::to_string(b.players) + "/" + std::to_string(b.max);
            if (b.started) row += "  (started)";
            // W9-05-02: tell a different build from different game data.
            if (b.net_proto != ra2mp::kNetProto || b.sim_ver != ra2mp::sim_version()) row += "  (different version)";
            else if (b.rules_crc != mpui::rules_crc()) row += "  (different game data)";
            row += mpui::platform_tag(b.platform, b.cross_platform);   // W9-04-13
            list_.items.push_back(row);
            list_.data.push_back(static_cast<int>(i));
        }
        if (list_.sel >= static_cast<int>(g.size())) list_.sel = -1;
        players_.items.clear();
        players_.data.clear();
        players_.items.push_back(prefs_.nick);   // this player first
        players_.data.push_back(-1);
        for (std::size_t i = 0; i < browser_.players().size(); ++i) {
            players_.items.push_back(browser_.players()[i].nick);
            players_.data.push_back(static_cast<int>(i));
        }
        if (status_.empty() || status_.rfind("Games found", 0) == 0 || status_.rfind("Looking", 0) == 0)
            status_ = g.empty() ? "Looking for games on your network..." : "Games found: " + std::to_string(g.size());
    }
    bool body_modal(int lx, int ly) override {
        if (list_.scrolls() && list_.strip_up().contains(lx, ly)) { list_.scroll(-1); return true; }
        if (list_.scrolls() && list_.strip_down().contains(lx, ly)) { list_.scroll(1); return true; }
        const int i = list_.hit(lx, ly);
        if (i >= 0) { list_.sel = i; play_ui("menu:click"); return true; }
        return false;
    }
    // W9-04-05/-06: drag-scroll the game list; its strip widens on touch.
    void body_pan(const ra2engine::InputEvent& ev, int sx, int sy, int, int) override {
        if (list_.r.contains(sx, sy)) list_pan(list_, ev);
    }
    void page_lists(std::vector<ShellList*>& out) override { out.push_back(&list_); }
    static ra2engine::SbRect chat_rect() { return {12, 260, 458, 112}; }
    void draw_body(const ShellPainter& p, ra2engine::TextureCache&) override {
        p.label(t_.name, 12, 16, 100, 19, 2);
        p.label(t_.players, 15, 40, 145, 16, 0);
        p.label(t_.games, 175, 40, 220, 16, 0);
        players_.draw(p);
        list_.draw(p);
        // 0x53F lobby chat (last lines that fit).
        const ra2engine::SbRect cr = chat_rect();
        p.fill(p.dev(cr), 0, 0, 0, 160);
        p.frame(p.dev(cr), true);
        const auto& log = browser_.chat();
        const int lines = cr.h / 16;
        int y = cr.y + 2;
        for (int i = std::max(0, static_cast<int>(log.size()) - lines); i < static_cast<int>(log.size()); ++i) {
            const ra2mp::LanChatLine& l = log[static_cast<std::size_t>(i)];
            p.label(p.fit(l.from + ": " + l.text, cr.w - 8), cr.x + 4, y, cr.w - 8, 16, 0);
            y += 16;
        }
        p.label("Join by IP", 12, 404, 100, 19, 2);
        p.label("Not listed? Ask the host for the address shown in their lobby.", 12, 428, 458, 16, 0);
        text_block(p, "Players on the same Wi-Fi or wired network see each other here. "
                      "Allow the game through the firewall for private networks.", 486, 40, 144, 8);
    }
    std::string hover_tip(int lx, int ly) const override {
        if (confirm_.open()) return {};
        auto tip = [](const char* k) { return mpui::yr_text(k, ""); };
        for (const ShellButton& b : buttons())
            if (b.rect.contains(lx, ly)) {
                if (b.action == A_HOST) return tip("STT:LobbyButtonNew");
                if (b.action == A_JOIN) return tip("STT:LobbyButtonJoin");
                if (b.action == A_BACK) return tip("STT:LobbyButtonBack");
                return {};
            }
        if (edits_[0].r.contains(lx, ly)) return tip("STT:LobbyEditYourName");
        if (players_.r.contains(lx, ly)) return tip("STT:LobbyListUsers");
        if (list_.r.contains(lx, ly)) return tip("STT:LobbyListGames");
        if (chat_rect().contains(lx, ly)) return tip("STT:LobbyEditOutput");
        if (edits_[2].r.contains(lx, ly)) return tip("STT:LobbyEditInput");
        return {};
    }
    void body_targets(std::map<std::string, ra2engine::SbRect>& t) const override {
        for (int i = list_.top; i < list_.top + list_.rows(); ++i) t["game:" + std::to_string(i)] = list_.item_rect(i);
        for (int i = players_.top; i < players_.top + players_.rows(); ++i)
            t["player:" + std::to_string(i)] = players_.item_rect(i);
        // Old button names (scripts of other branches).
        for (const ShellButton& b : buttons()) {
            if (!b.enabled) continue;
            if (b.action == A_HOST) t["button:Host Game"] = b.rect;
            if (b.action == A_JOIN) t["button:Join Game"] = b.rect;
            if (b.action == A_BACK) t["button:Back"] = b.rect;
        }
    }
    void body_probe(ra2play::Probe& p) override {
        p.set("lan_games", static_cast<double>(browser_.games().size()));
        p.set("lan_listening", browser_.listening() ? 1 : 0);
        p.set("lan_sel", list_.sel);
        for (std::size_t i = 0; i < browser_.games().size(); ++i) {
            const auto& g = browser_.games()[i];
            p.set_s("lan_game_host(" + std::to_string(i) + ")", g.host.str());
            p.set_s("lan_game_room(" + std::to_string(i) + ")", g.beacon.room);
        }
        p.set("lan_players", static_cast<double>(browser_.players().size()));
        for (std::size_t i = 0; i < browser_.players().size(); ++i)
            p.set_s("lan_player(" + std::to_string(i) + ")", browser_.players()[i].nick);
        const auto& log = browser_.chat();
        p.set("lobby_chat_count", static_cast<double>(log.size()));
        p.set_s("lobby_chat_last", log.empty() ? "" : log.back().from + ": " + log.back().text);
        p.set_s("nick", prefs_.nick);
    }

    mpui::Prefs prefs_;
    ra2mp::LanBrowser browser_;
    ShellList list_, players_;
    Text t_;
    std::uint32_t presence_nonce_ = 0;
};

// ===========================================================================
// Internet: your server, room list, host (public / private with code),
// join by list or by code.
// ===========================================================================
class MpOnlineScene : public MpPage {
public:
    MpOnlineScene(const Rules& r, bool na, const MenuArt& a) : MpPage(r, na, a) {
        ra2engine::os_android_bind_lan(false);   // W9-04-10: the server is reached over the default network
        prefs_ = mpui::Prefs::load();
        edits_.push_back(mpui::Edit{"server", {130, 40, 220, 19}, prefs_.server, 64, true});
        edits_.back().ime = ra2engine::kOsImeUri;    // W9-04-14
        edits_.push_back(mpui::Edit{"nick", {130, 64, 160, 19}, prefs_.nick, 16, true});
        edits_.push_back(mpui::Edit{"code", {130, 404, 90, 19}, "", 6, true});
        edits_.back().ime = ra2engine::kOsImeCode;
        list_.r = {30, 120, 440, 268};
        list_.visible = 16;
        link_ = std::make_unique<ra2mp::ServerLink>();
        if (!prefs_.server.empty()) connect();
        else status_ = "Type the address of your game server and press Connect.";   // W9-03-17
    }
    const char* name() const override { return "mp-online"; }

private:
    enum Act { A_NONE, A_CONNECT, A_HOST, A_HOSTPRIV, A_JOIN, A_JOINCODE, A_BACK };
    bool ready() const { return link_ && link_->state() == ra2mp::ServerLink::State::Ready; }
    std::vector<ShellButton> buttons() const override {
        return {btn(0, "Connect", !edits_[0].text.empty(), A_CONNECT), btn(1, "Host Game", ready(), A_HOST),
                btn(2, "Host Private", ready(), A_HOSTPRIV), btn(3, "Join Game", ready() && list_.sel >= 0, A_JOIN),
                btn(4, "Join Code", ready() && edits_[2].text.size() == 6, A_JOINCODE), btn(5, "Back", true, A_BACK)};
    }
    std::string header_text() const override { return "Internet"; }
    int back_action() const override { return A_BACK; }
    void connect() {
        prefs_.server = edits_[0].text;
        const std::string n = ra2mp::sanitize_name(edits_[1].text);
        if (!n.empty()) prefs_.nick = n;
        prefs_.save();
        ra2mp::ServerHello h;
        h.ver = RA2_VERSION;
        h.sim = ra2mp::sim_version();
        h.proto = ra2mp::kNetProto;
        h.rules = mpui::rules_crc();
        h.plat = mpui::platform_code();
        h.pid = prefs_.pid;
        h.nick = prefs_.nick;
        status_ = "Connecting to " + prefs_.server + "...";
        failure_told_ = false;
        if (!link_->connect(prefs_.server, h)) server_failed(link_->error());
        mpui::log_line("connecting to server " + prefs_.server);
    }
    void join(const std::string& code) {
        if (!ready()) return;
        pending_v3_ = mpui::random_u32();
        pending_code_ = code;
        for (char& c : pending_code_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        link_->join(pending_code_, pending_v3_);
        status_ = "Joining room " + pending_code_ + "...";
    }
    void press(int act) override {
        switch (act) {
            case A_CONNECT: connect(); return;
            case A_HOST:
            case A_HOSTPRIV:
                next_ = MpLobbyScene::host(rules_, no_audio_, art_, MpLobbyScene::ONLINE, std::move(link_), act == A_HOSTPRIV);
                return;
            case A_JOIN:
                if (list_.sel >= 0 && list_.sel < static_cast<int>(link_->rooms().size()))
                    join(link_->rooms()[static_cast<std::size_t>(list_.sel)].code);
                return;
            case A_JOINCODE: join(edits_[2].text); return;
            case A_BACK:
                next_ = new ShellScene(rules_, no_audio_, art_, PAGE_MAIN);
                return;
            default: return;
        }
    }
    void on_enter(int edit) override {
        set_focus(-1);
        const std::string& id = edits_[static_cast<std::size_t>(edit)].id;
        if (id == "server") connect();
        else if (id == "code" && edits_[2].text.size() == 6) join(edits_[2].text);
    }
    void tick(std::uint64_t now) override {
        if (!link_) return;
        link_->update(now);
        while (!link_->events().empty()) {
            const ra2mp::ServerEvent ev = link_->events().front();
            link_->events().pop_front();
            switch (ev.kind) {
                case ra2mp::ServerEvent::HelloOk:
                    status_ = "Connected to " + link_->server_name() + ".";
                    mpui::log_line("connected: " + link_->server_name());
                    link_->list();
                    last_list_ = now;
                    break;
                case ra2mp::ServerEvent::Rejected:
                    status_ = link_->error();
                    failure_told_ = true;
                    ask(0, "The game server refused the connection: " + link_->error(), {gf_csf("TXT_OK", "OK")});
                    break;
                case ra2mp::ServerEvent::Joined:
                    mpui::log_line("joined room " + ev.code + " host tunnel id " + std::to_string(ev.host_v3));
                    next_ = MpLobbyScene::join_online(rules_, no_audio_, art_, std::move(link_), pending_v3_, ev.host_v3, ev.code);
                    return;
                case ra2mp::ServerEvent::Error: {
                    static const std::map<std::string, std::string> kWhy = {
                        {"no_room", "No room with that code."}, {"full", "That game is full."},
                        {"started", "That game has already started."},
                        {"version", "That game uses a different game version or game data."},
                        {"rate", "Too many attempts: wait a minute."}, {"busy", "The server is busy."}};
                    const auto it = kWhy.find(ev.reason);
                    status_ = it != kWhy.end() ? it->second : "The server said: " + ev.reason;
                    ask(0, status_, {gf_csf("TXT_OK", "OK")});   // W9-03-18: a message box, like WOL's
                    break;
                }
                case ra2mp::ServerEvent::Disconnected: server_failed(link_->error()); break;
                default: break;
            }
        }
        if (link_->state() == ra2mp::ServerLink::State::Failed && !link_->error().empty()) server_failed(link_->error());
        if (ready() && now - last_list_ >= 5000) {
            link_->list();
            last_list_ = now;
        }
        list_.items.clear();
        list_.data.clear();
        for (const ra2mp::ServerRoom& r : link_->rooms()) {
            std::string row = r.name + "  -  " + r.map + "  " + std::to_string(r.players) + "/" + std::to_string(r.max);
            if (r.started) row += "  (started)";
            row += mpui::platform_tag(static_cast<std::uint8_t>(r.plat), false);   // W9-04-13
            list_.items.push_back(row);
            list_.data.push_back(0);
        }
        if (list_.sel >= static_cast<int>(list_.items.size())) list_.sel = -1;
    }
    bool body_modal(int lx, int ly) override {
        if (list_.scrolls() && list_.strip_up().contains(lx, ly)) { list_.scroll(-1); return true; }
        if (list_.scrolls() && list_.strip_down().contains(lx, ly)) { list_.scroll(1); return true; }
        const int i = list_.hit(lx, ly);
        if (i >= 0) { list_.sel = i; play_ui("menu:click"); return true; }
        return false;
    }
    // W9-04-05/-06: drag-scroll the game list; its strip widens on touch.
    void body_pan(const ra2engine::InputEvent& ev, int sx, int sy, int, int) override {
        if (list_.r.contains(sx, sy)) list_pan(list_, ev);
    }
    void page_lists(std::vector<ShellList*>& out) override { out.push_back(&list_); }
    void draw_body(const ShellPainter& p, ra2engine::TextureCache&) override {
        p.label("Server", 30, 40, 95, 19, 0);
        p.label("Your name", 30, 64, 95, 19, 0);
        if (link_ && ready()) p.label(p.fit(link_->motd(), 440), 30, 90, 440, 16, 0);
        p.header("Public games", 30, 104, 440, 16);
        list_.draw(p);
        p.label("Room code", 30, 404, 95, 19, 0);
        p.label("Private games are joined by their 6-letter code.", 230, 404, 240, 19, 0);
        text_block(p, "Internet games run through your own server. All traffic is relayed: players never see each "
                      "other's IP address.", 486, 40, 144, 8);
    }
    void body_targets(std::map<std::string, ra2engine::SbRect>& t) const override {
        for (int i = list_.top; i < list_.top + list_.rows(); ++i) t["room:" + std::to_string(i)] = list_.item_rect(i);
    }
    void body_probe(ra2play::Probe& p) override {
        static const char* kSt[6] = {"idle", "resolving", "connecting", "hello", "ready", "failed"};
        p.set_s("online_state", link_ ? kSt[static_cast<int>(link_->state())] : "moved");
        p.set("rooms", link_ ? static_cast<double>(link_->rooms().size()) : 0.0);
        if (link_) {
            for (std::size_t i = 0; i < link_->rooms().size(); ++i)
                p.set_s("room_code(" + std::to_string(i) + ")", link_->rooms()[i].code);
            p.set_s("server_name", link_->server_name());
            p.set_s("motd", link_->motd());
            p.set_s("online_error", link_->error());
        }
    }

    // W9-03-18: a server failure is a message box with a full sentence (the
    // original's WOL errors are message boxes, e.g. TXT_WOL_CONNLOST); the
    // status line keeps the short reason.
    void server_failed(const std::string& why) {
        status_ = why;
        if (failure_told_ || confirm_.open()) return;
        failure_told_ = true;
        std::string msg = "Could not reach the game server at " + prefs_.server +
                          ". Check the address and that the server is running.";
        if (!why.empty()) msg += " (" + why + ")";
        ask(0, msg, {gf_csf("TXT_OK", "OK")});
    }

    mpui::Prefs prefs_;
    std::unique_ptr<ra2mp::ServerLink> link_;
    ShellList list_;
    bool failure_told_ = false;
    std::uint64_t last_list_ = 0;
    std::uint32_t pending_v3_ = 0;
    std::string pending_code_;
};

Scene* MpLobbyScene::make_return_page() {
    if (net_ == LAN) return new MpLanScene(rules_, no_audio_, art_);
    return new MpOnlineScene(rules_, no_audio_, art_);
}

// ===========================================================================
// NET-PLAY: the match hand-off (replaces the "Match Ready" placeholder).
// ===========================================================================
// W9-01-04 [RE: ComputeFrameCRC FUN_0064D520 runs every frame from Queue_AI
// FUN_00647090]: the sync CRC is the cheap per-frame net_frame_crc, computed
// EVERY frame like the original's. The debug full-World hash ([Net]
// FullHash=1) costs 10-100 ms (W9-02-04) and keeps the old period of
// kNetFullHashEvery frames. RA2.INI [Net] HashEvery overrides either.
constexpr int kNetHashEvery = 1;
constexpr int kNetFullHashEvery = 15;

Scene* defer_net_game_scene(bool no_audio, ra2engine::SkirmishConfig cfg, std::shared_ptr<NetPlay> np,
                            std::uint32_t seed, std::vector<int> starts, int local_house) {
    auto c = std::make_shared<ra2engine::SkirmishConfig>(std::move(cfg));
    auto art = std::make_shared<SkirmishLoadArt>();
    std::function<void(SDL_Renderer*, ra2engine::TextureCache&, double)> backdrop;
    g_net_local_house = local_house;   // the loading art follows the local player's country
    art->load(*c);
    g_net_local_house = -1;
    // W9-03-20: the loading list names the players by their handles (the
    // country stays after the dash); computer players stay "Computer".
    for (std::size_t k = 0; k < np->session.players.size() && k < art->players.size(); ++k) {
        const ra2mp::MpPlayer& pl = np->session.players[k];
        const std::string row = art->players[k];
        const std::size_t dash = row.find("  -  ");
        art->players[k] = (pl.is_ai || pl.name.empty() ? std::string("Computer") : pl.name) +
                          (dash == std::string::npos ? std::string() : row.substr(dash));
    }
    backdrop = [art](SDL_Renderer* ren, ra2engine::TextureCache& tc, double frac) { art->draw(ren, tc, frac); };
    return defer_scene("LOADING", [no_audio, c, np, seed, starts, local_house]() -> Scene* {
        { int gw = 1280, gh = 720; game_logical_size(gw, gh); GameScene::set_canvas_size(gw, gh); }
        // Session-wide determinism inputs for the build: the local house
        // (local_slot_of), the session seed (crates), the lobby's resolved
        // start waypoints (the real-play start pick), the NetPlay itself.
        struct Restore {
            ~Restore() {
                g_net_local_house = -1;
                g_crate_seed_override = -1;
                g_simdiff_starts.clear();
                g_pending_netplay.reset();
            }
        } restore;
        g_net_local_house = local_house;
        g_crate_seed_override = seed;
        c->session_seed = seed;   // Net L0: every sim RNG stream derives from the lobby's session seed
        g_simdiff_starts = starts;
        g_pending_netplay = np;
        return new GameScene(no_audio, *c);
    }, backdrop);
}

Scene* mp_start_session(const Rules& rules, bool no_audio, const MenuArt& art, const ra2mp::MpSessionConfig& s,
                        std::shared_ptr<ra2mp::MpNet> transport) {
    (void)art;
    const std::string text = ra2mp::describe_session(s);
    std::size_t a = 0;
    while (a < text.size()) {
        std::size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        mpui::log_line(text.substr(a, b - a));
        a = b + 1;
    }
    ++g_mp_games_played;   // W9-03-16 game number
    ra2mp::MpConvertCtx ctx;
    ctx.countries = gf_lobby_countries();
    const Rules* r = &rules;
    ctx.side_of = [r](const std::string& k) { return gf_side_of_country(*r, k); };
    ra2engine::SkirmishConfig cfg = ra2mp::session_to_skirmish(s, ctx);
    // W9-03-04: the game type's rules override (MPModesMD.ini third field;
    // Battle's MPBattleMD.ini only says AlliesAllowed=yes, so it is skipped
    // and a Battle match keeps the plain rulesmd.ini).
    {
        const mpui::GameMode& gm = mpui::game_mode(s.lobby.mode);
        if (gm.index != 1 && !gm.rules.empty()) cfg.mode_rules = gm.rules;
        mpui::log_line("game type " + std::to_string(gm.index) + " " + gm.name_key +
                       (cfg.mode_rules.empty() ? "" : " rules override " + cfg.mode_rules));
    }

    auto np = std::make_shared<NetPlay>();
    np->mpnet = transport;
    np->session = s;
    np->ai_takeover = s.lobby.opt.ai_takeover;
    np->full_hash = net_full_hash_mode();
    np->hash_every = net_ini_int("HashEvery", np->full_hash ? kNetFullHashEvery : kNetHashEvery);
    ra2::net::NetGameConfig nc;
    nc.local_house = s.local_house;
    std::vector<int> starts;
    for (const ra2mp::MpPlayer& p : s.players) {
        starts.push_back(p.start);   // combatant k (slot order) -> the lobby's start waypoint
        if (p.is_ai) continue;
        nc.human_houses.push_back(p.house);
        if (!p.local && p.peer) nc.remotes.emplace_back(p.house, transport->addr_of(p.peer));
    }
    nc.master = s.is_host;
    for (const ra2mp::MpPlayer& p : s.players)   // W9-05-12: the host sits in slot 0
        if (p.slot == 0 && !p.is_ai) nc.master_house = p.house;
    const bool lan = s.transport == ra2mp::MpTransportKind::Lan;
    nc.mode = lan ? ra2::net::GameMode::LAN : ra2::net::GameMode::Internet;
    // Start values (net-protocol.md §8): LAN FSR 5 / MaxAhead 10; Internet
    // FSR 5 / MaxAhead 15 (3 x FSR, the master's TIMING floor). The host's
    // TIMING events then size both from the measured round trip.
    nc.frame_send_rate = static_cast<std::uint32_t>(net_ini_int("FrameSendRate", 5));
    nc.max_ahead = static_cast<std::uint32_t>(net_ini_int("MaxAhead", lan ? 10 : 15));
    nc.hash_every = np->hash_every;
    // W9-01-01 [RE: FUN_00647090 @0x6476DA reads GameSpeed 0xA8DAC8 for the
    // TIMING cap 60 / 45 / 60÷n]: the lobby's Game Speed (GameScene keeps it
    // current with GAMESPEED events).
    nc.game_speed = std::max(0, std::min(6, s.lobby.opt.game_speed));
    np->game = std::make_unique<ra2::net::NetGame>(transport->net(), nc);
    mpui::log_line("lockstep session: local house " + std::to_string(nc.local_house) + ", " +
                   std::to_string(nc.remotes.size()) + " remote player(s), " + (lan ? "LAN" : "tunnel") +
                   ", master " + (nc.master ? "yes" : "no"));
    for (const auto& rm : nc.remotes)
        mpui::log_line("  house " + std::to_string(rm.first) + " at " + transport->describe(transport->peer_of_addr(rm.second)));
    return defer_net_game_scene(no_audio, cfg, np, s.seed, starts, s.local_house);
}
