// W2-AR robustness layer (Android + desktop). Textually included by main.cpp
// right before run_scene_driver (anonymous namespace), so it sees Scene,
// draw_text and the Platform. Not reached by any byte-identity gate.
//
//   * W2-AR-30  a non-blocking SDL assertion handler (log, never prompt);
//   * W2-AR-02  a loading screen with progress, drawn while a GameScene is
//               built (load_progress() checkpoints in the loaders);
//   * W2-AR-10  DeferredScene: a transition that is BUILT only after the
//               current scene is destroyed (no two games in memory at once);
//   * W2-AR-05  a scene whose construction throws (a missing data file in
//               the middle of a session) becomes an error page that returns
//               to the main menu instead of killing the process;
//   * W2-AR-03  the message page used for the first-run missing-files screen.

bool g_session_no_audio = false;
// W2-AR-06: the current logical canvas width (set by run_scene_driver from
// Scene::logical_width()) and the Platform it drives.
int g_ui_w = 1280, g_ui_h = 720;
ra2engine::Platform* g_platform = nullptr;
// The game canvas for the panel: H = 720 on desktop; on Android (touch) the
// height that makes one canvas pixel ~0.75 dp (W4-AM-02: a 6" 1080p 420 dpi
// phone -> ~548, clamped 520..720 -- tablets keep 720; it was 0.85 dp / 484
// px, which showed only 42% of the desktop's map area), so the sidebar's
// 60x48 cameos stay ~45x36 dp. W = H * aspect, at least 16:9 (narrower panels
// letterbox, as before) and at most ~21:9: a 20:9 phone gets the extra width
// as more map. 1280x720 under --play (hidden 1280x720 window).
void game_logical_size(int& out_w, int& out_h) {
    out_w = 1280;
    out_h = 720;
    if (g_play_canvas_w > 0 && g_play_canvas_h > 0) {   // W6-01-15 (--play `set canvas`)
        out_w = g_play_canvas_w;
        out_h = g_play_canvas_h;
        return;
    }
    if (!g_platform) return;
    int w = 0, h = 0;
    g_platform->output_size(w, h);
    if (w <= 0 || h <= 0) return;
    int lh = 720;
#ifdef __ANDROID__
    float ddpi = 0, hdpi = 0, vdpi = 0;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && vdpi > 0.0f) {
        const double px_per_dp = vdpi / 160.0;
        lh = static_cast<int>(std::lround(h / (0.75 * px_per_dp)));
        lh = std::max(520, std::min(720, lh)) & ~1;
        // Crisp pixels: the art and the bitmap fonts are pixel art, and SDL
        // scales the logical canvas with nearest-neighbour. A fractional
        // factor (1080 / 598 = 1.81 on a 386-dpi phone) draws some source
        // pixels 1 px wide and others 2 px, so text looks ragged. Prefer the
        // nearest logical height that divides the output height exactly
        // (1080 -> 540 = 2x) while it stays inside the 520..720 range.
        int best = 0, best_d = 1 << 30;
        for (int k = 1; k <= 8; ++k) {
            if (h % k) continue;
            const int c = h / k;
            if (c < 520 || c > 720 || (c & 1)) continue;
            const int d = std::abs(c - lh);
            if (d < best_d) { best = c; best_d = d; }
        }
        if (best > 0) lh = best;
    }
#endif
    // Floor (not round): SDL scales by min(w / lw, h / lh), so a width rounded
    // UP would pull the factor below the exact integer chosen above.
    const int lw = static_cast<int>(static_cast<long long>(lh) * w / h) & ~1;
    out_h = lh;
    out_w = std::max(lh * 16 / 9, std::min(lh * 239 / 100, lw));
}
bool g_interactive_session = false;   // run_menu: fatal errors also get a message box
#ifndef RA2_VERSION
#define RA2_VERSION "dev"
#endif
constexpr const char* kWindowTitle = "Red Alert 2 engine " RA2_VERSION;   // run_menu's --no-audio (the error page's way back)

// Evidence hook (dev/QA, desktop + Android): with RA2_AUTOSHOT_DIR set, the
// first frame of each tagged screen (loading / missing-files / error page) is
// written to <dir>/<tag>.png -- the interactive paths have no --play capture.
// Android has no env vars; there the dir is <external files>/autoshot/ when
// that folder exists (created by hand with adb/MTP for a test session).
std::set<std::string> g_autoshot_done;
std::string autoshot_dir() {
#ifdef __ANDROID__
    const char* ext = SDL_AndroidGetExternalStoragePath();
    if (!ext) return {};
    const std::string d = std::string(ext) + "/autoshot/";
    std::error_code ec;
    return std::filesystem::is_directory(d, ec) ? d : std::string();
#else
    const char* d = std::getenv("RA2_AUTOSHOT_DIR");
    return d && *d ? with_trailing_sep(d) : std::string();
#endif
}
void dev_autoshot(ra2engine::Platform& plat, const std::string& tag) {
    if (g_autoshot_done.count(tag)) return;
    const std::string dir = autoshot_dir();
    if (dir.empty()) return;
    g_autoshot_done.insert(tag);
    capture_png(static_cast<SDL_Renderer*>(plat.renderer()), dir + tag + ".png", plat.width(), plat.height());
    SDL_Log("[autoshot] %s%s.png", dir.c_str(), tag.c_str());
}

// ---------------------------------------------------------------------------
// W2-AR-30: SDL assertion handler.
int g_sdl_assert_count = 0;

SDL_AssertState SDLCALL ra2_sdl_assert_handler(const SDL_AssertData* d, void*) {
    ++g_sdl_assert_count;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL ASSERTION (ignored) '%s' at %s:%d %s (hit %u)",
                 d ? d->condition : "?", d ? d->filename : "?", d ? d->linenum : 0,
                 d ? d->function : "?", d ? d->trigger_count : 0u);
    std::cerr << "[sdl] ASSERTION (ignored) '" << (d ? d->condition : "?") << "' at "
              << (d ? d->filename : "?") << ":" << (d ? d->linenum : 0) << "\n";
    // Log once per call site; never block on a console / dialog prompt.
    return SDL_ASSERTION_ALWAYS_IGNORE;
}
void install_sdl_assert_handler() { SDL_SetAssertionHandler(&ra2_sdl_assert_handler, nullptr); }

// ---------------------------------------------------------------------------
// W2-AR-02: the loading screen.
struct LoadingScreen {
    ra2engine::Platform* plat = nullptr;
    ra2engine::TextureCache* tc = nullptr;
    std::string title;          // "LOADING" / "LOADING SAVED GAME"
    // Optional backdrop (e.g. the campaign LS800 art); draws the whole frame
    // below the bar. frac in [0,1].
    std::function<void(SDL_Renderer*, ra2engine::TextureCache&, double)> backdrop;
    int step = 0;
    Uint32 t0 = 0, last_draw = 0, last_step = 0;
    std::string stage;
};
LoadingScreen* g_loading = nullptr;
int g_load_expected_steps = 48;   // a 1v1 skirmish build reports ~49   // refined from the previous load's count

void draw_loading_frame(LoadingScreen& L) {
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(L.plat->renderer());
    const double frac = std::min(0.97, static_cast<double>(L.step) / std::max(1, g_load_expected_steps));
    L.plat->begin_frame();
    const int W = L.plat->width(), H = L.plat->height();
    if (L.backdrop) {   // the backdrop draws its own bar (campaign LS800 screen)
        L.backdrop(ren, *L.tc, frac);
        dev_autoshot(*L.plat, frac >= 0.5 ? "loading_campaign_mid" : "loading_campaign_start");
        L.plat->present();
        return;
    }
    const int tw = static_cast<int>(L.title.size()) * 6 * 4;
    draw_text(ren, L.title, (W - tw) / 2, H / 2 - 70, 4, 230, 200, 120);
    const SDL_Rect bar{W / 4, H - 90, W / 2, 16};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xC0);
    SDL_RenderFillRect(ren, &bar);
    SDL_Rect fill{bar.x + 2, bar.y + 2, static_cast<int>((bar.w - 4) * frac), bar.h - 4};
    SDL_SetRenderDrawColor(ren, 230, 200, 120, 255);
    SDL_RenderFillRect(ren, &fill);
    SDL_RenderDrawRect(ren, &bar);
    char pct[16];
    std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(frac * 100.0 + 0.5));
    draw_text(ren, pct, bar.x + bar.w + 12, bar.y + 1, 2, 230, 200, 120);
    if (!L.stage.empty()) draw_text(ren, L.stage, bar.x, bar.y + 26, 2, 160, 160, 160);
    dev_autoshot(*L.plat, frac >= 0.5 ? "loading_mid" : "loading_start");
    L.plat->present();
}

// A checkpoint inside a long load. A no-op unless a loading screen is up (so
// the gates and the --play harness, which build scenes directly, are
// unaffected). Keeps the OS event queue pumped (no "not responding" ghost on
// Windows) and redraws at most ~30 times a second.
void load_progress(const char* stage) {
    if (!g_loading) return;
    LoadingScreen& L = *g_loading;
    ++L.step;
    const Uint32 now = SDL_GetTicks();
    SDL_Log("[load] %3d %-28s +%5u ms (total %6u ms)", L.step, stage, now - L.last_step, now - L.t0);
    L.last_step = now;
    L.stage = stage;
    SDL_PumpEvents();
    if (now - L.last_draw < 33) return;
    L.last_draw = now;
    draw_loading_frame(L);
}

// ---------------------------------------------------------------------------
// W2-AR-10 / -02 / -05: deferred scene construction.
struct DeferredScene : Scene {
    std::string title;
    std::function<Scene*()> make;
    std::function<void(SDL_Renderer*, ra2engine::TextureCache&, double)> backdrop;
    const char* name() const override { return "deferred"; }
};
// Build `make()` after the current scene is gone, under a loading screen.
Scene* defer_scene(std::string title, std::function<Scene*()> make,
                   std::function<void(SDL_Renderer*, ra2engine::TextureCache&, double)> backdrop = {}) {
    auto* d = new DeferredScene;
    d->title = std::move(title);
    d->make = std::move(make);
    d->backdrop = std::move(backdrop);
    return d;
}

Scene* make_main_menu_scene(bool no_audio);   // campaign_scenes.inc

// W2-AR-15: tell SDL (-> Android's IME) where the edited field is, in WINDOW
// coordinates, so the soft keyboard pans the view to keep it (and the
// dialog buttons just below) visible instead of covering it.
void set_text_input_rect_logical(int x, int y, int w, int h) {
    if (!g_platform) return;
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(g_platform->renderer());
    int wx0 = 0, wy0 = 0, wx1 = 0, wy1 = 0;
    SDL_RenderLogicalToWindow(ren, static_cast<float>(x), static_cast<float>(y), &wx0, &wy0);
    SDL_RenderLogicalToWindow(ren, static_cast<float>(x + w), static_cast<float>(y + h), &wx1, &wy1);
    SDL_Rect r{wx0, wy0, std::max(1, wx1 - wx0), std::max(1, wy1 - wy0)};
    SDL_SetTextInputRect(&r);
}

// W9-04-01/-02/-03/-12/-14: one engine edit on the soft keyboard.
// ime_open: seed the IME with the field's text / kind / limit (Android:
// SDLActivity.ra2SetImeField, so its delete key can erase pre-filled text),
// point the IME at the field (adjustPan) and (re)start text input -- also
// when it is already focused, because the IME's Back hides the keyboard
// behind the engine's back (SDL_StopTextInput) and a tap must bring it back.
// ime_sync: the engine changed the text itself (sent / cleared / pasted).
// ime_screen_keyboard: a soft-keyboard platform (Android; --play `ime
// screenkb` for the desktop tests): leaving a field always hides the
// keyboard (SDL_IsTextInputActive() is the TEXTINPUT event state, enabled by
// default, so the "was it active before?" check never stopped it).
bool g_ime_force_screen_kb = false;
std::string g_ime_seed;                 // --play probes ime_seed / ime_kind
int g_ime_kind = -1;
bool ime_screen_keyboard() { return g_ime_force_screen_kb || SDL_HasScreenKeyboardSupport() == SDL_TRUE; }
void ime_sync(const std::string& text, int kind, int max) {
    g_ime_seed = text;
    g_ime_kind = kind;
    ra2engine::os_android_ime_field(text, kind, max);
}
void ime_open(int x, int y, int w, int h, const std::string& text, int kind, int max) {
    ime_sync(text, kind, max);
    set_text_input_rect_logical(x, y, w, h);
    SDL_StartTextInput();
}

// W9-04-06: window (logical render) pixels per Android dp (160 dpi), from the
// display DPI and the panel / canvas ratio. 1.0 when unknown (desktop).
double window_px_per_dp() {
    if (!g_platform) return 1.0;
    float ddpi = 0.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0.0f) return 1.0;
    int ow = 0, oh = 0;
    g_platform->output_size(ow, oh);
    const int lw = g_platform->width();
    if (ow <= 0 || lw <= 0) return 1.0;
    const double phys_per_win = static_cast<double>(ow) / lw;
    return (ddpi / 160.0) / phys_per_win;
}

// A first scene that immediately hands over to `n` (typically a
// DeferredScene, so even the first game of a run gets the loading screen).
struct BootScene : Scene {
    Scene* n = nullptr;
    explicit BootScene(Scene* next_scene) : n(next_scene) {}
    Scene* next() override { Scene* r = n; n = nullptr; return r; }
    ~BootScene() override { delete n; }
    const char* name() const override { return "boot"; }
};

// A full-screen message with one MAIN MENU button (W2-AR-05: a scene that
// could not be built). Esc / Enter / Back also return to the menu.
class ErrorScene : public Scene {
public:
    ErrorScene(std::string title, std::string detail) : title_(std::move(title)), detail_(std::move(detail)) {}
    void handle(const ra2engine::InputEvent& ev) override {
        if (ev.kind == ra2engine::InputEvent::Key && (ev.key == ra2engine::kKeyEscape || ev.key == ra2engine::kKeyEnter))
            back_ = true;
        if (ev.kind == ra2engine::InputEvent::PointerUp && in_button(ev.x, ev.y)) back_ = true;
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache&) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        draw_text(ren, title_, 80, 120, 3, 255, 120, 90);
        int y = 190;
        for (const std::string& line : wrap(detail_, 80)) {
            draw_text(ren, line, 80, y, 2, 220, 220, 220);
            y += 24;
        }
        const SDL_Rect b = button();
        SDL_SetRenderDrawColor(ren, 70, 30, 26, 255);
        SDL_RenderFillRect(ren, &b);
        SDL_SetRenderDrawColor(ren, 210, 70, 60, 255);
        SDL_RenderDrawRect(ren, &b);
        draw_text(ren, "MAIN MENU", b.x + (b.w - 9 * 12) / 2, b.y + (b.h - 14) / 2, 2, 235, 235, 240);
        dev_autoshot(plat, "error_page");
    }
    Scene* next() override {
        if (!back_) return nullptr;
        back_ = false;
        if (Scene* m = make_main_menu_scene(g_session_no_audio)) return m;
        quit_ = true;
        return nullptr;
    }
    bool wants_quit() const override { return quit_; }
    const char* name() const override { return "error"; }
    bool locate(const std::string& what, int& x, int& y) const override {
        if (what != "button:MAIN MENU") return false;
        const SDL_Rect b = button();
        x = b.x + b.w / 2; y = b.y + b.h / 2;
        return true;
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "error";
        p.set_s("title", title_);
        const SDL_Rect b = button();
        p.targets["button:MAIN MENU"] = ra2play::Rect{b.x, b.y, b.w, b.h};
    }
    static std::vector<std::string> wrap(const std::string& s, std::size_t cols) {
        std::vector<std::string> out;
        std::string line;
        std::istringstream in(s);
        std::string word;
        while (in >> word) {
            if (!line.empty() && line.size() + 1 + word.size() > cols) { out.push_back(line); line.clear(); }
            line += (line.empty() ? "" : " ") + word;
        }
        if (!line.empty()) out.push_back(line);
        return out;
    }

private:
    static SDL_Rect button() { return SDL_Rect{80, 560, 240, 64}; }   // >= 48 dp on a phone
    bool in_button(int x, int y) const {
        const SDL_Rect b = button();
        return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
    }
    std::string title_, detail_;
    bool back_ = false, quit_ = false;
};

// The friendly text for an exception thrown while building a scene: name the
// missing data file when it is a "cannot open file" error.
std::string describe_load_failure(const std::string& what) {
    const std::string key = "cannot open file: ";
    const std::size_t p = what.find(key);
    if (p != std::string::npos) {
        std::string path = what.substr(p + key.size());
        const std::size_t sep = path.find_last_of("\\/");
        const std::string file = sep == std::string::npos ? path : path.substr(sep + 1);
        return "THE GAME FILE " + file + " IS MISSING. COPY IT INTO " + env_install_dir() +
               " AND TRY AGAIN.";
    }
    return what;
}

// Runs `d` with the current scene already destroyed (the caller reset it).
// Never throws: a failure becomes an ErrorScene.
Scene* build_deferred(DeferredScene& d, ra2engine::Platform& plat, ra2engine::TextureCache& tc,
                      bool show_screen) {
    LoadingScreen L;
    L.plat = &plat;
    L.tc = &tc;
    L.title = d.title.empty() ? "LOADING" : d.title;
    L.backdrop = d.backdrop;
    L.t0 = L.last_step = SDL_GetTicks();
    if (show_screen) {
        g_loading = &L;
        draw_loading_frame(L);
        L.last_draw = SDL_GetTicks();
    }
    Scene* s = nullptr;
    try {
        s = d.make();
    } catch (const std::exception& ex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[load] scene build failed: %s", ex.what());
        std::cerr << "[load] scene build failed: " << ex.what() << "\n";
        s = new ErrorScene("COULD NOT START THE GAME", describe_load_failure(ex.what()));
    }
    if (show_screen) {
        if (L.step > 0) g_load_expected_steps = L.step;
        L.step = std::max(L.step, g_load_expected_steps);
        draw_loading_frame(L);   // a full bar before the first game frame
        SDL_Log("[load] %s done in %u ms (%d checkpoints)", L.title.c_str(), SDL_GetTicks() - L.t0, L.step);
        std::cerr << "[load] " << L.title << " done in " << (SDL_GetTicks() - L.t0) << " ms\n";
        g_loading = nullptr;
        // Dev/QA: RA2_QUIT_AFTER_LOAD_MS=<ms> ends the run that long after the
        // first load (desktop load-time measurement without a human).
        if (const char* q = std::getenv("RA2_QUIT_AFTER_LOAD_MS")) {
            SDL_AddTimer(static_cast<Uint32>(std::max(1, std::atoi(q))), [](Uint32, void*) -> Uint32 {
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_QUIT;
                SDL_PushEvent(&e);
                return 0;
            }, nullptr);
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// W2-AR-03: a simple message page (no game data needed: rect font only).
// Returns the index of the chosen button, or -1 on Quit.
int run_message_page(ra2engine::Platform& plat, const std::string& title,
                     const std::vector<std::string>& lines, const std::vector<std::string>& buttons) {
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
    const int bw = 260, bh = 64, gap = 24;
    const int total = static_cast<int>(buttons.size()) * bw + (static_cast<int>(buttons.size()) - 1) * gap;
    auto button_rect = [&](int i) {
        return SDL_Rect{(plat.width() - total) / 2 + i * (bw + gap), plat.height() - 110, bw, bh};
    };
    int hover = -1;
    for (;;) {
        ra2engine::InputEvent ev;
        while (plat.poll(ev)) {
            if (ev.kind == ra2engine::InputEvent::Quit) return -1;
            if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEnter) return 0;
            if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape)
                return static_cast<int>(buttons.size()) - 1;
            if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerUp) {
                hover = -1;
                for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
                    const SDL_Rect r = button_rect(i);
                    if (ev.x >= r.x && ev.x < r.x + r.w && ev.y >= r.y && ev.y < r.y + r.h) hover = i;
                }
                if (ev.kind == ra2engine::InputEvent::PointerUp && hover >= 0) return hover;
            }
        }
        plat.begin_frame();
        draw_text(ren, title, 60, 50, 3, 255, 200, 90);
        int y = 110;
        for (const std::string& l : lines) {
            const bool head = !l.empty() && l[0] == '#';
            draw_text(ren, head ? l.substr(1) : l, 60, y, 2, head ? 255 : 215, head ? 140 : 215, head ? 110 : 215);
            y += 22;
        }
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            const SDL_Rect r = button_rect(i);
            SDL_SetRenderDrawColor(ren, i == hover ? 110 : 70, 30, 26, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 210, 70, 60, 255);
            SDL_RenderDrawRect(ren, &r);
            const int lw = static_cast<int>(buttons[i].size()) * 12;
            draw_text(ren, buttons[i], r.x + (r.w - lw) / 2, r.y + (r.h - 14) / 2, 2, 235, 235, 240);
        }
        dev_autoshot(plat, "message_page");
        plat.present();
        SDL_Delay(16);
    }
}

// The first-run page: lists the missing files and where to put them. Loops
// until every required file is present (RETRY), the user picks another
// folder (BROWSE, desktop), or quits. Returns false to exit the app.
bool ensure_data_files(ra2engine::Platform& plat) {
    for (;;) {
        const DataPreflight p = preflight_data_files(env_install_dir());
        for (const DataFileSpec* f : p.missing_optional)
            std::cerr << "[assets] optional file missing: " << f->name << " (" << f->purpose << ")\n";
        if (p.ok()) return true;
        std::vector<std::string> lines;
        lines.push_back("THE ORIGINAL GAME FILES WERE NOT FOUND IN:");
        lines.push_back("#" + p.dir);
        lines.push_back("");
        lines.push_back("MISSING (REQUIRED):");
        for (const DataFileSpec* f : p.missing_required)
            lines.push_back(std::string("#  ") + f->name + "  -  " + f->purpose);
        if (!p.missing_optional.empty()) {
            lines.push_back("MISSING (OPTIONAL):");
            std::string opt = "  ";
            for (const DataFileSpec* f : p.missing_optional) {
                if (opt.size() + std::strlen(f->name) > 90) { lines.push_back(opt); opt = "  "; }
                opt += std::string(f->name) + "  ";
            }
            lines.push_back(opt);
        }
        lines.push_back("");
#ifdef __ANDROID__
        lines.push_back("COPY THEM FROM YOUR RED ALERT 2 INSTALL:");
        lines.push_back(" - IMPORT: PUT THEM ON THE PHONE (E.G. DOWNLOAD/RA2), TAP IMPORT AND");
        lines.push_back("   PICK THAT FOLDER - THE APP COPIES THEM IN.");
        lines.push_back(" - OR COPY THEM INTO THE FOLDER ABOVE OVER USB (MTP).");
        const std::vector<std::string> buttons = {"RETRY", "IMPORT", "EXIT"};
#else
        lines.push_back("COPY THEM FROM YOUR RED ALERT 2 INSTALL INTO THAT FOLDER, OR");
        lines.push_back("PICK THE INSTALL FOLDER WITH BROWSE (SAVED IN RA2.INI [ENGINE] DATADIR),");
        lines.push_back("OR START WITH --DATA <FOLDER> / SET RA2_INSTALL_DIR.");
        const std::vector<std::string> buttons = {"RETRY", "BROWSE", "EXIT"};
#endif
        SDL_Log("[assets] %lu required file(s) missing in %s", static_cast<unsigned long>(p.missing_required.size()), p.dir.c_str());
        const int choice = run_message_page(plat, "GAME FILES NEEDED", lines, buttons);
        if (choice < 0 || choice == static_cast<int>(buttons.size()) - 1) return false;
#ifdef __ANDROID__
        if (choice == 1 && ra2engine::os_android_open_importer()) return false;   // the importer takes over
#else
        if (choice == 1) {
            const std::string picked = ra2engine::os_browse_folder("Select your Red Alert 2 folder");
            if (!picked.empty()) {
                save_data_dir_to_ini(picked);
                g_data_dir_cli.clear();
            }
        }
#endif
        reset_data_dir();
    }
}

// W5-F: perf overlay / perf log (R-170..172) and input record / replay (R-184).
#include "app_perf.inc"
