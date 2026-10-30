// ingame_pages.inc -- W2-UI-13/14/15/16: the in-game options flow drawn as
// FULL-SCREEN shell pages (draw_shell + the §7.2 button grid), replacing the
// old rect-font popups. Included by main.cpp where SaveLoadDialog/InGameMenu
// used to live (after shell_pages.inc, before GameScene).
//
// [RE: wave2/04 RE #4/#5 + "Exact layout specs"]
//   0xB5  Game Options (campaign + skirmish): rows Game Controls 0x521 / Load
//         0x51E / Save 0x51F / Delete 0x520 / Abort Mission 0x522 / Resume
//         Mission 0x686; status line 0x695; no 0x71A movie / 0x71C monitor
//         (static MNSCRNS + SDTP frame 0); Esc = Resume.
//   0xBBB Game Controls: Game Speed slider 0x529 (130,141,192,21) 0..6
//         (GameSpeed = 6 - pos), Scroll Rate 0x52A (130,191,192,21); labels
//         right-aligned at x 6 w 117, "Faster" at x 331; checks Target Lines
//         (48,313), Tooltips (235,313), See Hidden (48,342); rows Sound /
//         Keyboard / Back.
//   0xB8  Sound: Music/Sound/Voice labels (13, 43/79/115, 135, 24), sliders
//         (157, 45/81/117, 262, 21) 0..10 + readout, Shuffle (43,251), score
//         listbox (157,157,262,160), Play (43,347,124,24), Stop (295,347,124,24).
//   0xB6  Abort: "What would you like to do?" centred in (66,222,345,30); row 0
//         Abort (code 2), row 1 Restart (campaign only, code 5), row 5 Resume.
//   0xB7 / 0x2B4 / 0x2B5 Load / Save / Delete: title static (36,26,399,39),
//         listbox (36,81,399,303) [Save (36,81,399,255) + edit (37,362,399,22)],
//         action row 0, Back row 5; confirms are 0x120 popups.
// [RE-FALLBACK: listbox row height 16 logical px; the Keyboard row is greyed
// (no in-game Keyboard page, W2-UI-27); the page header of 0xB8 / 0xB6 is the
// CSF GUI:SoundOptions / GUI:AbortMission label.]

// ============================================================================
// P7 Load / Save / Delete (LoadOptionsClass modes 1/2/3, saveload.md §2/§3).
// ============================================================================

// <SDL pref dir>/Saved Games/ (created on demand). The install dir is never
// written (read-only on Android). Empty when SDL has no writable path.
std::string save_games_dir() {
    const char* o = std::getenv("RA2_PREF_DIR");   // G0 --play isolation
    char* p = o ? nullptr : SDL_GetPrefPath("ra2engine", "RA2");
    if (!o && !p) return {};
    std::string s = std::string(o ? o : p) + "Saved Games";
    if (p) SDL_free(p);
    std::error_code ec;
    std::filesystem::create_directories(s, ec);
    return s;
}

class SaveLoadDialog {
public:
    enum Mode { Load = 1, Save = 2, Delete = 3 };
    enum Result { None, Back, DoLoad, DoSave };

    void open(Mode m, const std::string& default_desc) {
        mode_ = m;
        dir_ = save_games_dir();
        slots_ = ra2engine::save::list_saves(dir_);
        top_ = 0;
        sel_ = (m == Save) ? 0 : (slots_.empty() ? -1 : 0);
        desc_ = ra2engine::save::clean_description(default_desc);
        message_.clear();
        confirm_ = false;
        open_ = true;
        down_ = false;
        last_click_ms_ = 0;
        last_click_row_ = -1;
        if (m == Save) {
            // W2-AR-15 / R-152: the IME keeps the description edit AND the SAVE
            // button (shell row 0, ABOVE the edit) in view: the text-input rect
            // spans from the button's top to the edit's bottom, so Android's
            // adjustPan no longer pushes SAVE off the top while typing.
            const ra2engine::SbRect e = edit_rect_l();
            const ra2engine::SbRect a = shell_row(0);
            const int top = std::min(a.y, e.y);
            const SDL_Rect d = ui_xform().dev(e.x, top, std::max(e.x + e.w, a.x + a.w) - e.x, e.y + e.h - top);
            // W9-04-01: the IME starts from the description (its delete key erases it).
            ime_open(d.x, d.y, d.w, d.h, desc_, ra2engine::kOsImeText, 0);
            replace_ = !desc_.empty();   // R-152: the prefilled text is selected
        }
    }
    void close() {
        open_ = false;
        SDL_StopTextInput();
    }
    bool is_open() const { return open_; }
    Mode mode() const { return mode_; }
    const std::string& path() const { return path_; }
    const std::string& description() const { return desc_; }
    void set_message(const std::string& m) { message_ = m; }

    Result handle(const ra2engine::InputEvent& ev) {
        if (!open_) return None;
        if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerDown ||
            ev.kind == ra2engine::InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == ra2engine::InputEvent::Left) {
            if (ev.kind == ra2engine::InputEvent::PointerDown) down_ = true;
            if (ev.kind == ra2engine::InputEvent::PointerUp) down_ = false;
        }
        if (ev.kind == ra2engine::InputEvent::Quit ||
            (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape)) {
            if (confirm_) { confirm_ = false; message_.clear(); return None; }
            close();   // G-01: Esc = the page's Back
            return Back;
        }
        if (confirm_) {   // modal 0x120 popup: Ok / Cancel
            if (ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left) {
                if (yes_rect().contains(ev.x, ev.y)) { confirm_ = false; return commit(true); }
                if (no_rect().contains(ev.x, ev.y)) { confirm_ = false; message_.clear(); }
            }
            return None;
        }
        if (ev.kind == ra2engine::InputEvent::Text && mode_ == Save) {
            if (replace_) desc_.clear();   // R-152: typing replaces the selected default
            replace_ = false;
            desc_ = ra2engine::save::clean_description(desc_ + ev.text);
            return None;
        }
        if (ev.kind == ra2engine::InputEvent::Key) {
            if (ev.key == ra2engine::kKeyBackspace && mode_ == Save && replace_) {
                desc_.clear();   // R-152: Backspace deletes the whole selection
                replace_ = false;
            } else if (ev.key == ra2engine::kKeyBackspace && mode_ == Save && !desc_.empty()) {
                desc_.pop_back();
                while (!desc_.empty() && (static_cast<unsigned char>(desc_.back()) & 0xC0) == 0x80)
                    desc_.pop_back();   // whole UTF-8 sequence
            } else if (ev.key == ra2engine::kKeyUp) {
                move_sel(-1);
            } else if (ev.key == ra2engine::kKeyDown) {
                move_sel(+1);
            } else if (ev.key == ra2engine::kKeyEnter) {
                return commit(false);
            }
            return None;
        }
        if (ev.kind != ra2engine::InputEvent::PointerDown || ev.button != ra2engine::InputEvent::Left)
            return None;
        if (back_rect().contains(ev.x, ev.y)) { close(); return Back; }
        if (action_rect().contains(ev.x, ev.y)) return commit(false);
        if (rows() > visible()) {
            if (ui_dev(strip_up()).contains(ev.x, ev.y)) { top_ = std::max(0, top_ - 1); return None; }
            if (ui_dev(strip_down()).contains(ev.x, ev.y)) {
                top_ = std::min(std::max(0, rows() - visible()), top_ + 1);
                return None;
            }
        }
        for (int v = 0; v < visible(); ++v) {
            if (!row_rect(v).contains(ev.x, ev.y)) continue;
            const int row = top_ + v;
            if (row >= rows()) return None;
            const bool dbl = (row == last_click_row_ && ev.t_ms - last_click_ms_ < 400);
            sel_ = row;
            last_click_row_ = row;
            last_click_ms_ = ev.t_ms;
            if (mode_ == Save && row > 0) {
                desc_ = slots_[static_cast<size_t>(row - 1)].header.description;
                replace_ = !desc_.empty();   // R-152: a picked slot's text is selected too
            }
            message_.clear();
            return dbl ? commit(false) : None;   // LBN_DBLCLK == the action button
        }
        return None;
    }

    void render(SDL_Renderer* ren, ra2engine::TextureCache& tc, Uint32 now_ms, bool soviet = false) const {
        if (!open_) return;
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        const std::vector<ShellButton> btns = buttons();
        int hover = -1;
        for (int i = 0; i < static_cast<int>(btns.size()); ++i)
            if (btns[(size_t)i].enabled && ui_dev(btns[(size_t)i].rect).contains(mx_, my_) && !confirm_) hover = i;
        draw_shell(ren, tc, art, xf, btns, hover, (down_ && hover >= 0) ? hover : -1, header(), "", -1);
        // Title static + listbox (black well, red frame, 12 px yellow rows).
        p.header(title(), 36, 26, 399, 39, 0);
        const ra2engine::SbRect lr = list_rect();
        p.fill(p.dev(lr), 0, 0, 0);
        const bool strip = rows() > visible();
        const int row_w = lr.w - 2 - (strip ? ShellList::kStripW : 0);
        for (int v = 0; v < visible(); ++v) {
            const int row = top_ + v;
            if (row >= rows()) break;
            const ra2engine::SbRect r{lr.x + 1, lr.y + 1 + v * kRowH, row_w, kRowH};
            if (row == sel_) p.fill(p.dev(r), 128, 0, 0);
            std::string label, when;
            bool bad = false;
            if (mode_ == Save && row == 0) {
                label = "<New Saved Game>";
            } else {
                const ra2engine::save::SlotEntry& s = slots_[static_cast<size_t>(row - (mode_ == Save ? 1 : 0))];
                label = s.header.description.empty() ? s.file_name : s.header.description;
                bad = s.wrong_version || s.damaged;
                if (s.damaged) when = "Damaged";   // W2-AR-21
                // W6-06-01: say which way (until saves carry over, decision D-1).
                else if (bad) when = s.header.version < ra2engine::save::kFormatVersion ? "Older version" : "Newer version";
                else if (s.header.unix_time > 0) when = format_time(s.header.unix_time);
            }
            p.label(p.fit(label, r.w - 130), r.x + 3, r.y, r.w - 130, r.h, 0, !bad);
            p.label(when, r.x + r.w - 124, r.y, 120, r.h, 2, !bad);
        }
        if (strip) {
            p.frame(p.dev(strip_up()), true);
            p.frame(p.dev(strip_down()), true);
        }
        p.frame(p.dev(lr), true);
        if (mode_ == Save) {
            p.edit(edit_rect_l(), desc_, true, !replace_ && (now_ms / 400) % 2 == 0);
            if (replace_ && !desc_.empty()) {   // R-152: the selection highlight (over the text)
                const ra2engine::SbRect e = edit_rect_l();
                const SDL_Rect hl = p.dev(e.x + 2, e.y + 3, e.w - 4, e.h - 6);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 70, 110, 220, 0x70);
                SDL_RenderFillRect(ren, &hl);
            }
        }
        // Status line: the dialog message (errors), else the STT help.
        std::string st = message_;
        if (st.empty()) {
            if (hover == 0) st = gf_csf(mode_ == Load ? "STT:LoadButtonLoad" : mode_ == Save ? "STT:MsnSaveButtonSave"
                                                                                           : "STT:MsnDltButtonDelete", "");
            else if (hover == 1) st = gf_csf("STT:LoadButtonBack", "");
        }
        shell_status_line(p, st);
        if (confirm_) {
            int ph = -1;
            if (yes_rect().contains(mx_, my_)) ph = 0;
            if (no_rect().contains(mx_, my_)) ph = 1;
            popup_render(ren, tc, confirm_text_, {gf_csf("GUI:OK", "Ok"), gf_csf("GUI:Cancel", "Cancel")}, ph,
                         down_ ? ph : -1, soviet);
        }
    }
    // G0 --play: the page's click targets + state (play_driver.inc).
    void play_probe(ra2play::Probe& p) const {
        if (!open_) return;
        auto put = [&](const std::string& k, const ra2engine::SbRect& r) {
            p.targets[k] = ra2play::Rect{r.x, r.y, r.w, r.h};
        };
        p.set("dialog_open", 1);
        p.set("dialog_mode", static_cast<int>(mode_));
        p.set("dialog_rows", rows());
        p.set("dialog_sel", sel_);
        p.set("dialog_confirm", confirm_ ? 1 : 0);
        p.set_s("dialog_message", message_);
        p.set_s("dialog_page", header());
        put("dialog:action", action_rect());
        put("dialog:back", back_rect());
        for (int v = 0; v < visible() && top_ + v < rows(); ++v) put("dialog:row" + std::to_string(top_ + v), row_rect(v));
        if (confirm_) { put("dialog:yes", yes_rect()); put("dialog:no", no_rect()); }
    }

private:
    static constexpr int kRowH = 16;   // [RE-FALLBACK] listbox item height (logical)
    int visible() const { return (list_rect().h - 2) / kRowH; }
    ra2engine::SbRect list_rect() const {
        return mode_ == Save ? ra2engine::SbRect{36, 81, 399, 255} : ra2engine::SbRect{36, 81, 399, 303};
    }
    static ra2engine::SbRect edit_rect_l() { return {37, 362, 399, 22}; }
    ra2engine::SbRect strip_up() const {
        const ra2engine::SbRect l = list_rect();
        return {l.x + l.w - ShellList::kStripW - 1, l.y + 1, ShellList::kStripW, l.h / 2 - 1};
    }
    ra2engine::SbRect strip_down() const {
        const ra2engine::SbRect l = list_rect();
        return {l.x + l.w - ShellList::kStripW - 1, l.y + l.h / 2, ShellList::kStripW, l.h - l.h / 2 - 1};
    }
    ra2engine::SbRect row_rect(int v) const {   // DEVICE
        const ra2engine::SbRect l = list_rect();
        return ui_dev({l.x + 1, l.y + 1 + v * kRowH, l.w - 2 - (rows() > visible() ? ShellList::kStripW : 0), kRowH});
    }
    static ra2engine::SbRect action_rect() { return ui_dev(shell_row(0)); }
    static ra2engine::SbRect back_rect() { return ui_dev(shell_row(5)); }
    static ra2engine::SbRect yes_rect() { return popup_button_dev(0, 2); }
    static ra2engine::SbRect no_rect() { return popup_button_dev(1, 2); }
    std::string header() const {
        return mode_ == Load ? gf_csf("GUI:LoadMissionMenu", "Load Mission")
               : mode_ == Save ? gf_csf("GUI:SaveMissionMenu", "Save Mission")
                               : gf_csf("GUI:DeleteMissionMenu", "Delete Mission");
    }
    std::string title() const {
        return mode_ == Load ? gf_csf("GUI:SelectMission", "Select Mission")
               : mode_ == Save ? gf_csf("GUI:SaveMission", "Save Mission")
                               : gf_csf("GUI:DeleteMission", "Delete Mission");
    }
    std::vector<ShellButton> buttons() const {
        const std::string act = mode_ == Load ? gf_csf("GUI:Load", "Load")
                                : mode_ == Save ? gf_csf("GUI:Save", "Save") : gf_csf("GUI:Delete", "Delete");
        return {ShellButton{shell_row(0), act, action_enabled(), ACT_NONE, PAGE_MAIN},
                ShellButton{shell_row(5), gf_csf("GUI:Back", "Back"), true, ACT_NONE, PAGE_MAIN}};
    }
    static std::string format_time(std::int64_t t) {
        const std::time_t tt = static_cast<std::time_t>(t);
        const std::tm* tm = std::localtime(&tt);
        if (!tm) return {};
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", tm);
        return b;
    }
    int rows() const { return static_cast<int>(slots_.size()) + (mode_ == Save ? 1 : 0); }
    void move_sel(int d) {
        if (rows() == 0) return;
        sel_ = std::max(0, std::min(rows() - 1, (sel_ < 0 ? 0 : sel_) + d));
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + visible()) top_ = sel_ - visible() + 1;
    }
    const ra2engine::save::SlotEntry* selected_slot() const {
        const int i = sel_ - (mode_ == Save ? 1 : 0);
        return (i >= 0 && i < static_cast<int>(slots_.size())) ? &slots_[static_cast<size_t>(i)] : nullptr;
    }
    bool action_enabled() const {
        if (mode_ == Save) return sel_ >= 0;
        return selected_slot() != nullptr;
    }
    // The action button / double-click / Enter. `confirmed` = the popup
    // already answered Ok.
    Result commit(bool confirmed) {
        if (!action_enabled()) return None;
        const ra2engine::save::SlotEntry* s = selected_slot();
        if (mode_ == Load) {
            if (s->damaged) { message_ = "This saved game is damaged."; return None; }   // W2-AR-21
            if (s->wrong_version) {   // W6-06-01: plain words, and why
                message_ = "Saved by another version of the engine: it cannot be loaded.";
                return None;
            }
            path_ = s->path;
            close();
            return DoLoad;
        }
        if (mode_ == Delete) {
            if (!confirmed) { ask("Delete this saved game?"); return None; }
            ra2engine::save::delete_save(s->path);
            slots_ = ra2engine::save::list_saves(dir_);
            sel_ = slots_.empty() ? -1 : std::min(sel_, static_cast<int>(slots_.size()) - 1);
            top_ = 0;
            return None;   // the page stays open (saveload.md §2)
        }
        if (ra2engine::save::clean_description(desc_).empty()) {
            message_ = "Please enter a description.";
            return None;
        }
        if (dir_.empty()) { message_ = "No writable save folder."; return None; }
        if (s && !confirmed) { ask("Overwrite this saved game?"); return None; }
        path_ = s ? s->path
                  : dir_ + "/" + ra2engine::save::new_save_name(dir_, static_cast<std::uint32_t>(SDL_GetTicks()) ^ 0x9E37u);
        close();
        return DoSave;
    }
    void ask(const char* text) {
        confirm_ = true;
        confirm_text_ = text;
    }

    bool open_ = false;
    Mode mode_ = Load;
    std::string dir_;
    std::vector<ra2engine::save::SlotEntry> slots_;
    int sel_ = -1, top_ = 0;
    std::string desc_, message_, path_, confirm_text_;
    bool replace_ = false;   // R-152: the description is selected (the next key replaces it)
    bool confirm_ = false;
    Uint32 last_click_ms_ = 0;
    int last_click_row_ = -1;
    int mx_ = -1, my_ = -1;
    bool down_ = false;
};

// ============================================================================
// 0xB5 / 0xBBB / 0xB8 / 0xB6: the in-game options pages.
// ============================================================================
#include "keyboard_page.inc"   // W5-E: dialog 0xA3 (R-140)

// W5-E (R-034): the mission briefing page -- Restate.cpp FUN_0065EF60
// (Restate_Mission): MISSIONMD.INI [<map>] Briefing= (CSF) into dialog 0x10C,
// "Brief:Error" when the key is missing; a modal loop until the Return button
// (0x423). It is reached from the sidebar's second top tab (0x80F3 ->
// special dialog 1, FUN_00653290) and from the event dispatcher
// (FUN_004C6BD0). [RE-FALLBACK: the 0x10C template was not dumped -- the
// text well (24,60,423,380) and Return in button row 5 follow the other
// in-game pages; the clone also opens it with F1.]
struct RestatePage {
    static ra2engine::SbRect text_rect() { return {24, 92, 423, 340}; }
    static ra2engine::SbRect return_rect() { return shell_row(5); }
    bool return_hit(int dx, int dy) const { return ui_dev(return_rect()).contains(dx, dy); }
    void render(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& name, const std::string& text,
                int mx, int my) const {
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        const bool hov = ui_dev(return_rect()).contains(mx, my);
        const std::vector<ShellButton> b{ShellButton{return_rect(), gf_csf("GUI:Return", "Return"), true, 0, PAGE_MAIN}};
        draw_shell(ren, tc, art, xf, b, hov ? 0 : -1, -1, gf_csf("GUI:Briefing", "Briefing"), "", -1);
        p.header(name, 24, 56, 423, 26, 1);
        const ra2engine::SbRect r = text_rect();
        p.fill(p.dev(r), 0, 0, 0, 150);
        p.frame(p.dev(r), true);
        const std::string body = text.empty() ? gf_csf("Brief:Error", "No briefing available.") : text;
        const std::vector<std::string> lines = shell_wrap(p, body, r.w - 16);
        for (std::size_t i = 0; i < lines.size() && i < 20; ++i)
            p.label(lines[i], r.x + 8, r.y + 8 + static_cast<int>(i) * 16, r.w - 16, 16, 0);
        if (hov) shell_status_line(p, gf_csf("STT:BriefingButtonReturn", ""));
    }
};

class InGameMenu {
public:
    enum Choice { None, Resume, Load, Save, Delete, Abort, Restart, Settings, Committed, MusicPlay, MusicStop,
                  Leave, Observe, DipAlly, DipListen,   // W9-05-06/03/07 (network game)
                  Gestures, Briefing };                 // TG-16 (touch: cheat sheet, mission briefing)
    enum Page { P_OPTIONS = 0, P_CONTROLS, P_SOUND, P_ABORT, P_DIPLOMACY };
    // W2-UI-29: one row of the Diplomacy page 0x73 (read-only in skirmish).
    // W9-05-03/07/11: `house`, the live Allies box (ally_enabled) and the
    // Chat listener box (listen / listen_enabled) of a network game.
    struct DipRow { std::string name; SDL_Color color{0, 0, 0, 255}; bool ally = false; bool self = false; int kills = 0;
                    int house = -1; bool ally_enabled = false; bool listen = true; bool listen_enabled = false; };
    void set_diplomacy(std::vector<DipRow> rows) { dip_ = std::move(rows); }
    // W9-05-06 [RE: dialog 0xB6 proc 0x4F17C0 in LAN/WOL: GUI:Leave +
    // GUI:Observe (alive, >= 2 humans alive) + Resume; the MP options page has
    // no Load / Save / Delete]. The scene refreshes it every frame.
    void set_mp(bool net, bool can_observe) { mp_net_ = net; mp_can_observe_ = can_observe; }
    void set_touch(bool t) { touch_ = t; }   // TG-16: Game Controls adds Gestures / Briefing
    int dip_pick() const { return dip_pick_; }
    // `open_` stays a public flag (gf_scene / controls set it); open() also
    // resets the page and reseeds the controls from the user's settings.
    bool open_ = false;

    void configure(bool campaign, bool soviet) {
        campaign_ = campaign;
        soviet_ = soviet;
    }
    void set_tracks(std::vector<std::string> t) { tracks_.items = std::move(t); }
    void open(Page pg = P_OPTIONS) {
        open_ = true;
        page_ = pg;
        down_ = false;
        drag_ = -1;
        set_ = g_user_settings ? *g_user_settings : set_;
    }
    void close() { open_ = false; page_ = P_OPTIONS; drag_ = -1; kb_.open_ = false; }
    KeyboardPage& keyboard() { return kb_; }
    Page page() const { return page_; }
    const ra2engine::Settings& settings() const { return set_; }
    int music_track() const { return tracks_.sel; }

    Choice handle(const ra2engine::InputEvent& ev) {
        using IE = ra2engine::InputEvent;
        if (!open_) return None;
        if (kb_.open_) {   // W5-E Keyboard page over Game Controls
            kb_.handle(ev);
            return None;
        }
        if (ev.kind == IE::PointerMove || ev.kind == IE::PointerDown || ev.kind == IE::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == IE::Left && ev.kind == IE::PointerUp) down_ = false;
        if (ev.kind == IE::Quit || (ev.kind == IE::Key && ev.key == ra2engine::kKeyEscape)) {
            // G-01: Esc = the page's bottom button (Resume / Back).
            if (page_ == P_OPTIONS || page_ == P_ABORT || page_ == P_DIPLOMACY) { close(); return Resume; }
            const Choice c = page_ == P_SOUND ? (page_ = P_CONTROLS, Committed) : (page_ = P_OPTIONS, Committed);
            return c;
        }
        int lx = 0, ly = 0;
        ui_to_logical(ev.x, ev.y, lx, ly);
        // Slider drags.
        if (drag_ >= 0 && (ev.kind == IE::PointerMove || ev.kind == IE::PointerUp)) {
            set_slider(drag_, lx);
            if (ev.kind == IE::PointerUp) drag_ = -1;
            return Settings;
        }
        if (ev.kind != IE::PointerDown || ev.button != IE::Left) return None;
        down_ = true;
        for (const ShellButton& b : buttons()) {
            if (!b.enabled || !b.rect.contains(lx, ly)) continue;
            return press(b.action);
        }
        if (page_ == P_DIPLOMACY) {   // W9-05-03 Allies / W9-05-07 Chat boxes
            for (std::size_t k = 0; k < dip_.size() && k < 8; ++k) {
                const DipRow& r = dip_[k];
                const int y = 73 + static_cast<int>(k * 65 / 2);
                if (r.self) continue;
                if (r.ally_enabled && ra2engine::SbRect{262, y, 40, 16}.contains(lx, ly)) {
                    dip_pick_ = r.house;
                    return DipAlly;
                }
                if (r.listen_enabled && ra2engine::SbRect{330, y, 40, 16}.contains(lx, ly)) {
                    dip_pick_ = r.house;
                    return DipListen;
                }
            }
        }
        // Page controls.
        for (int i = 0; i < kSliders; ++i) {
            if (!slider_on_page(i)) continue;
            const SliderDef d = slider(i);
            const int ro = d.readout ? ra2engine::kTrackbarReadoutW : 0;
            const ra2engine::SbRect track{d.r.x, d.r.y, d.r.w - ro, d.r.h};
            if (!track.contains(lx, ly)) continue;
            const int tx = d.r.x + ra2engine::trackbar_thumb_x(slider_pos(i), d.range, d.r.w, ro);
            if (!(lx >= tx && lx < tx + ra2engine::kTrackbarThumbW)) set_slider(i, lx);
            drag_ = i;
            return Settings;
        }
        for (int i = 0; i < kChecks; ++i) {
            if (!check_on_page(i) || !check_rect(i).contains(lx, ly)) continue;
            bool& v = check_ref(i);
            v = !v;
            publish();
            return Settings;
        }
        if (page_ == P_SOUND) {
            if (tracks_.scrolls() && tracks_.strip_up().contains(lx, ly)) { tracks_.scroll(-1); return None; }
            if (tracks_.scrolls() && tracks_.strip_down().contains(lx, ly)) { tracks_.scroll(+1); return None; }
            const int hit = tracks_.hit(lx, ly);
            if (hit >= 0) { tracks_.sel = hit; return None; }
            if (kPlay.contains(lx, ly) && tracks_.sel >= 0) return MusicPlay;
            if (kStop.contains(lx, ly)) return MusicStop;
        }
        return None;
    }

    void render(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        if (!open_) return;
        if (kb_.open_) { kb_.render(ren, tc); return; }
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        int lx = 0, ly = 0;
        ui_to_logical(mx_, my_, lx, ly);
        const std::vector<ShellButton> btns = buttons();
        int hover = -1;
        for (int i = 0; i < static_cast<int>(btns.size()); ++i)
            if (btns[(size_t)i].enabled && btns[(size_t)i].rect.contains(lx, ly)) hover = i;
        // ShellButton rects are LOGICAL here; draw_shell maps them.
        draw_shell(ren, tc, art, xf, btns, hover, (down_ && hover >= 0) ? hover : -1, header(), "", -1);
        std::string status = hover >= 0 ? stt_for(btns[(size_t)hover].action) : std::string();
        if (page_ == P_CONTROLS) {
            p.label(gf_csf("GUI:GameSpeed", "Game Speed"), 6, 139, 117, 24, 2);
            p.label(gf_csf("GUI:Faster", "Faster"), 331, 139, 100, 24, 0);
            p.label(gf_csf("GUI:ScrollRate", "Scroll Rate"), 6, 189, 117, 24, 2);
            p.label(gf_csf("GUI:Faster", "Faster"), 331, 189, 100, 24, 0);
        } else if (page_ == P_SOUND) {
            static const char* kL[3] = {"GUI:MusicVolume", "GUI:SoundVolume", "GUI:VoiceVolume"};
            static const char* kF[3] = {"Music Volume", "Sound Volume", "Voice Volume"};
            for (int i = 0; i < 3; ++i) p.label(gf_csf(kL[i], kF[i]), 13, 43 + 36 * i, 135, 24, 2);
            tracks_.draw(p);
            shell_small_button(p, kPlay, gf_csf("GUI:Play", "Play"), kPlay.contains(lx, ly),
                               down_ && kPlay.contains(lx, ly), tracks_.sel >= 0);
            shell_small_button(p, kStop, gf_csf("GUI:Stop", "Stop"), kStop.contains(lx, ly),
                               down_ && kStop.contains(lx, ly));
            if (tracks_.r.contains(lx, ly)) status = gf_csf("STT:SoundOptListScore", "");
            if (kPlay.contains(lx, ly)) status = gf_csf("STT:SoundOptButtonPlay", "");
            if (kStop.contains(lx, ly)) status = gf_csf("STT:SoundOptButtonStop", "");
        } else if (page_ == P_ABORT) {
            p.label(gf_csf("GUI:AskAbortMission", "What would you like to do?"), 66, 222, 345, 30, 1);
        } else if (page_ == P_DIPLOMACY) {
            // Dialog 0x73 template (px): Player (82,48) / Allies (262,48) / Chat
            // (330,48) / Kills (397,48); rows at y 73 + 32.5k: colour well
            // (19, y-4, 48, 19), name (82, y, 165, 16), Allies / Chat checks
            // (262 / 330, y), kills (397, y, 61, 16).
            p.header(gf_csf("GUI:Player", "Player"), 82, 48, 165, 16, 0);
            p.header(gf_csf("GUI:Allies", "Allies"), 262, 48, 52, 16, 0);
            p.header(gf_csf("GUI:Chat", "Chat"), 330, 48, 52, 16, 0);
            p.header(gf_csf("GUI:Kills", "Kills"), 397, 48, 90, 16, 0);
            for (std::size_t k = 0; k < dip_.size() && k < 8; ++k) {
                const DipRow& r = dip_[k];
                const int y = 73 + static_cast<int>(k * 65 / 2);
                p.fill(p.dev(19, y - 4, 48, 19), r.color.r, r.color.g, r.color.b);
                p.frame(p.dev(19, y - 4, 48, 19), true);
                p.label(p.fit(r.name, 165), 82, y, 165, 16, 0);
                if (!r.self) {
                    p.checkbox({262, y, 40, 16}, r.ally, "", r.ally_enabled);
                    p.checkbox({330, y, 40, 16}, r.listen, "", r.listen_enabled);
                }
                p.label(std::to_string(r.kills), 397, y, 61, 16, 0);
            }
            if (lx >= 262 && lx < 290 && ly >= 70 && ly < 320) status = gf_csf("STT:DiplomacyCBoxAllies", "");
            if (lx >= 397 && lx < 460 && ly >= 70 && ly < 320) status = gf_csf("STT:DiplomacyCBoxKills", "");
        }
        for (int i = 0; i < kSliders; ++i) {
            if (!slider_on_page(i)) continue;
            const SliderDef d = slider(i);
            p.trackbar(d.r, slider_pos(i), d.range, d.readout, std::to_string(slider_pos(i)), true);
            if (d.r.contains(lx, ly)) status = gf_csf(slider_stt(i), "");
        }
        for (int i = 0; i < kChecks; ++i) {
            if (!check_on_page(i)) continue;
            p.checkbox(check_rect(i), check_val(i), check_label(i), true);
            if (check_rect(i).contains(lx, ly)) status = gf_csf(check_stt(i), "");
        }
        shell_status_line(p, status);
    }

    // G0 --play: targets + state. Button targets keep the historical
    // "menu:<UPPER LABEL>" names (menu:LOAD GAME, menu:ABORT MISSION, ...).
    void play_probe(ra2play::Probe& p) const {
        p.set("menu_open", open_ ? 1 : 0);
        kb_.play_probe(p);
        if (!open_ || kb_.open_) return;
        static const char* kPages[5] = {"options", "controls", "sound", "abort", "diplomacy"};
        p.set("diplomacy_rows", static_cast<int>(dip_.size()));
        if (page_ == P_DIPLOMACY)   // W9-05: the self row, the live boxes
            for (std::size_t k = 0; k < dip_.size() && k < 8; ++k) {
                const DipRow& r = dip_[k];
                const std::string hs = std::to_string(r.house);
                if (r.self) p.set("dip_self", r.house);
                p.set("dip_ally(" + hs + ")", r.ally ? 1 : 0);
                p.set("dip_ally_enabled(" + hs + ")", r.ally_enabled ? 1 : 0);
                p.set("dip_listen(" + hs + ")", r.listen ? 1 : 0);
                p.set_s("dip_name(" + hs + ")", r.name);
                const int y = 73 + static_cast<int>(k * 65 / 2);
                const ra2engine::SbRect ra = ui_dev({262, y, 18, 16}), rc = ui_dev({330, y, 18, 16});
                if (r.ally_enabled) p.targets["dip:ally:" + hs] = ra2play::Rect{ra.x, ra.y, ra.w, ra.h};
                if (r.listen_enabled) p.targets["dip:chat:" + hs] = ra2play::Rect{rc.x, rc.y, rc.w, rc.h};
            }
        p.set_s("menu_page", kPages[page_]);
        auto put = [&](const std::string& k, const ra2engine::SbRect& lr) {
            const ra2engine::SbRect r = ui_dev(lr);
            p.targets[k] = ra2play::Rect{r.x, r.y, r.w, r.h};
        };
        std::string names;   // W9-03-10: the page's button set, in order
        for (const ShellButton& b : buttons()) {
            if (b.enabled) put("menu:" + upper(b.label), b.rect);
            names += (names.empty() ? "" : "|") + upper(b.label);
        }
        if (mp_net_) {   // W9-03-10 / W9-03-03 (network matches only: single-player play_hash unchanged)
            p.set_s("menu_buttons", names);
            p.set("menu_net", 1);
            for (std::size_t k = 0; k < dip_.size(); ++k) {
                p.set_s("diplomacy_name(" + std::to_string(k) + ")", dip_[k].name);
                p.set("diplomacy_self(" + std::to_string(k) + ")", dip_[k].self ? 1 : 0);
            }
        }
        p.set("menu_game_speed", set_.game_speed);
        p.set("menu_scroll_rate", set_.scroll_rate);
        p.set("menu_tooltips", set_.tooltips ? 1 : 0);
        p.set("menu_target_lines", set_.unit_action_lines ? 1 : 0);
        p.set("menu_music", ra2engine::volume_to_slider(set_.score_volume));
        p.set("menu_tracks", static_cast<int>(tracks_.items.size()));
        p.set("menu_track_sel", tracks_.sel);
        static const char* kSl[kSliders] = {"speed", "scroll", "music", "sound", "voice"};
        for (int i = 0; i < kSliders; ++i) {
            if (!slider_on_page(i)) continue;
            const SliderDef d = slider(i);
            const int ro = d.readout ? ra2engine::kTrackbarReadoutW : 0;
            for (int v = 0; v <= d.range; ++v)
                put(std::string("menu:slider:") + kSl[i] + "=" + std::to_string(v),
                    {d.r.x + shell_trackbar_x_for(v, d.range, d.r.w, ro), d.r.y + d.r.h / 2, 1, 1});
        }
        static const char* kCk[kChecks] = {"targetlines", "tooltips", "hidden", "shuffle"};
        for (int i = 0; i < kChecks; ++i)
            if (check_on_page(i)) put(std::string("menu:check:") + kCk[i], check_rect(i));
        if (page_ == P_SOUND) {
            for (int i = tracks_.top; i < tracks_.top + tracks_.rows(); ++i)
                put("menu:track:" + std::to_string(i), tracks_.item_rect(i));
            put("menu:PLAY", kPlay);
            put("menu:STOP", kStop);
        }
        // 0xB6 keeps the old confirm_* surface (scripts answer it as before).
        if (page_ == P_ABORT) {
            p.set("confirm_open", 1);
            p.set_s("confirm_text", gf_csf("GUI:AskAbortMission", "What would you like to do?"));
            for (const ShellButton& b : buttons()) {
                if (!b.enabled) continue;
                const ra2engine::SbRect r = ui_dev(b.rect);
                const ra2play::Rect pr{r.x, r.y, r.w, r.h};
                p.targets["confirm:" + b.label] = pr;
                if (b.action == A_ABORT) p.targets["confirm:yes"] = pr;
                if (b.action == A_RESUME) p.targets["confirm:no"] = pr;
            }
        }
    }

private:
    enum Act { A_NONE = 0, A_CONTROLS, A_LOAD, A_SAVE, A_DELETE, A_ABORTPAGE, A_RESUME, A_SOUND, A_KEYBOARD,
               A_BACK, A_ABORT, A_RESTART, A_LEAVE, A_OBSERVE, A_GESTURES, A_BRIEFING };
    bool mp_net_ = false, mp_can_observe_ = false;   // W9-05-06
    bool touch_ = false;                             // TG-16
    int dip_pick_ = -1;                              // W9-05-03/07: the row's house
    enum { S_SPEED = 0, S_SCROLL, S_MUSIC, S_SOUND, S_VOICE, kSliders };
    enum { C_TARGET = 0, C_TOOLTIPS, C_HIDDEN, C_SHUFFLE, kChecks };
    struct SliderDef { ra2engine::SbRect r; int range; bool readout; };
    static constexpr ra2engine::SbRect kPlay{43, 347, 124, 24};
    static constexpr ra2engine::SbRect kStop{295, 347, 124, 24};

    static std::string upper(std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    std::string header() const {
        switch (page_) {
            case P_SOUND: return gf_csf("GUI:SoundOptions", "Sound Options");
            case P_ABORT: return gf_csf("GUI:AbortMission", "Abort Mission");
            case P_DIPLOMACY: return gf_csf("GUI:DiplomacyMenu", "Diplomacy Options");
            default: return gf_csf("GUI:GameOptions", "Game Options");
        }
    }
    std::vector<ShellButton> buttons() const {
        auto b = [](int row, const std::string& s, bool en, int act) {
            return ShellButton{shell_row(row), s, en, act, PAGE_MAIN};
        };
        switch (page_) {
            case P_CONTROLS: {
                std::vector<ShellButton> v{b(0, gf_csf("GUI:Sound", "Sound"), true, A_SOUND),
                                           b(1, gf_csf("GUI:Keyboard", "Keyboard"), true, A_KEYBOARD)};
                // TG-16 [engine affordance, touch]: the gesture cheat sheet and,
                // in a campaign, the briefing (the old touch column's [BRF]).
                if (touch_) v.push_back(b(2, "Gestures", true, A_GESTURES));
                if (touch_ && campaign_) v.push_back(b(3, gf_csf("GUI:Briefing", "Briefing"), true, A_BRIEFING));
                v.push_back(b(5, gf_csf("GUI:Back", "Back"), true, A_BACK));
                return v;
            }
            case P_SOUND:
                return {b(5, gf_csf("GUI:Back", "Back"), true, A_BACK)};
            case P_DIPLOMACY:   // 0x423 GUI:Return
                return {b(5, gf_csf("GUI:Return", "Return"), true, A_RESUME)};
            case P_ABORT: {
                if (mp_net_) {   // W9-05-06: Leave / Observe / Resume
                    std::vector<ShellButton> v{b(0, gf_csf("GUI:Leave", "Quit"), true, A_LEAVE)};
                    if (mp_can_observe_) v.push_back(b(1, gf_csf("GUI:Observe", "Observe"), true, A_OBSERVE));
                    v.push_back(b(5, gf_csf("GUI:ResumeMission", "Resume Mission"), true, A_RESUME));
                    return v;
                }
                std::vector<ShellButton> v{b(0, gf_csf("GUI:Abort", "Abort"), true, A_ABORT)};
                if (campaign_) v.push_back(b(1, gf_csf("GUI:Restart", "Restart"), true, A_RESTART));
                v.push_back(b(5, gf_csf("GUI:ResumeMission", "Resume Mission"), true, A_RESUME));
                return v;
            }
            default:
                if (mp_net_)   // 0xBBA: Game Controls 0x521 (row 0), Abort Mission 0x522 (row 1), Resume 0x686
                    return {b(0, gf_csf("GUI:GameControls", "Game Controls"), true, A_CONTROLS),
                            b(1, gf_csf("GUI:AbortMission", "Abort Mission"), true, A_ABORTPAGE),
                            b(5, gf_csf("GUI:ResumeMission", "Resume Mission"), true, A_RESUME)};
                return {b(0, gf_csf("GUI:GameControls", "Game Controls"), true, A_CONTROLS),
                        b(1, gf_csf("GUI:LoadGame", "Load Game"), true, A_LOAD),
                        b(2, gf_csf("GUI:SaveGame", "Save Game"), true, A_SAVE),
                        b(3, gf_csf("GUI:DeleteGame", "Delete Game"), true, A_DELETE),
                        b(4, gf_csf("GUI:AbortMission", "Abort Mission"), true, A_ABORTPAGE),
                        b(5, gf_csf("GUI:ResumeMission", "Resume Mission"), true, A_RESUME)};
        }
    }
    std::string stt_for(int act) const {
        if (mp_net_ && page_ == P_OPTIONS) {   // 0xBBA tooltips
            if (act == A_CONTROLS) return gf_csf("STT:IGMultiOptButtonControls", "");
            if (act == A_ABORTPAGE) return gf_csf("STT:IGMultiOptButtonAbort", "");
            if (act == A_RESUME) return gf_csf("STT:IGMultiOptButtonResume", "");
        }
        switch (act) {
            case A_CONTROLS: return gf_csf("STT:IGSingleOptButtonControls", "");
            case A_LOAD: return gf_csf("STT:IGSingleOptButtonLoad", "");
            case A_SAVE: return gf_csf("STT:IGSingleOptButtonSave", "");
            case A_DELETE: return gf_csf("STT:IGSingleOptButtonDelete", "");
            case A_ABORTPAGE: return gf_csf("STT:IGSingleOptButtonAbort", "");
            case A_RESUME: return gf_csf(page_ == P_ABORT ? "STT:ConfirmExitButtonResume"
                                         : page_ == P_DIPLOMACY ? "STT:DiplomacyButtonReturn" : "STT:IGSingleOptButtonBack", "");
            case A_SOUND: return gf_csf("STT:MainOptButtonSound", "");
            case A_KEYBOARD: return gf_csf("STT:MainOptButtonKeyboard", "");
            case A_BACK: return gf_csf(page_ == P_SOUND ? "STT:SoundOptButtonBack" : "STT:GameOptButtonBack", "");
            case A_ABORT: return gf_csf("STT:ConfirmExitButtonAbort", "");
            case A_RESTART: return gf_csf("STT:ConfirmExitButtonRestart", "");
            case A_LEAVE: return gf_csf("STT:ConfirmExitButtonLeave", "");
            case A_OBSERVE: return gf_csf("STT:ConfirmExitButtonObserve", "");
            default: return "";
        }
    }
    Choice press(int act) {
        switch (act) {
            case A_CONTROLS: page_ = P_CONTROLS; return None;
            case A_LOAD: close(); return Load;
            case A_SAVE: close(); return Save;
            case A_DELETE: close(); return Delete;
            case A_ABORTPAGE: page_ = P_ABORT; return None;
            case A_RESUME: close(); return Resume;
            case A_SOUND: page_ = P_SOUND; return None;
            case A_KEYBOARD: kb_.open(); return None;
            case A_BACK: page_ = (page_ == P_SOUND) ? P_CONTROLS : P_OPTIONS; return Committed;
            case A_ABORT: close(); return Abort;
            case A_RESTART: close(); return Restart;
            case A_LEAVE: close(); return Leave;
            case A_OBSERVE: close(); return Observe;
            case A_GESTURES: close(); return Gestures;
            case A_BRIEFING: close(); return Briefing;
            default: return None;
        }
    }

    static SliderDef slider(int i) {
        switch (i) {
            case S_SPEED: return {{130, 141, 192, 21}, 6, false};
            case S_SCROLL: return {{130, 191, 192, 21}, 6, false};
            case S_MUSIC: return {{157, 45, 262, 21}, 10, true};
            case S_SOUND: return {{157, 81, 262, 21}, 10, true};
            default: return {{157, 117, 262, 21}, 10, true};
        }
    }
    bool slider_on_page(int i) const {
        return (page_ == P_CONTROLS && i <= S_SCROLL) || (page_ == P_SOUND && i >= S_MUSIC);
    }
    static const char* slider_stt(int i) {
        static const char* k[kSliders] = {"STT:GameOptSliderSpeed", "STT:GameOptSliderScroll", "STT:SoundOptSliderMusic",
                                          "STT:SoundOptSliderSound", "STT:SoundOptSliderVoice"};
        return k[i];
    }
    int slider_pos(int i) const {
        switch (i) {
            case S_SPEED: return 6 - std::max(0, std::min(6, set_.game_speed));   // GameSpeed = 6 - pos (RE #4)
            case S_SCROLL: return std::max(0, std::min(6, ra2engine::scroll_rate_to_slider(set_.scroll_rate)));
            case S_MUSIC: return ra2engine::volume_to_slider(set_.score_volume);
            case S_SOUND: return ra2engine::volume_to_slider(set_.sound_volume);
            default: return ra2engine::volume_to_slider(set_.voice_volume);
        }
    }
    void set_slider(int i, int lx) {
        const SliderDef d = slider(i);
        const int ro = d.readout ? ra2engine::kTrackbarReadoutW : 0;
        const int pos = ra2engine::trackbar_pos_from_x(lx - d.r.x, d.range, d.r.w, ro);
        switch (i) {
            case S_SPEED: set_.game_speed = 6 - pos; break;
            case S_SCROLL: set_.scroll_rate = ra2engine::slider_to_scroll_rate(pos); break;
            case S_MUSIC: set_.score_volume = ra2engine::slider_to_volume(pos); break;
            case S_SOUND: set_.sound_volume = ra2engine::slider_to_volume(pos); break;
            default: set_.voice_volume = ra2engine::slider_to_volume(pos); break;
        }
        publish();
    }
    bool check_on_page(int i) const { return (page_ == P_CONTROLS && i <= C_HIDDEN) || (page_ == P_SOUND && i == C_SHUFFLE); }
    static ra2engine::SbRect check_rect(int i) {
        switch (i) {
            case C_TARGET: return {48, 313, 170, 18};
            case C_TOOLTIPS: return {235, 313, 170, 18};
            case C_HIDDEN: return {48, 342, 220, 18};
            default: return {43, 251, 105, 22};
        }
    }
    std::string check_label(int i) const {
        switch (i) {
            case C_TARGET: return gf_csf("GUI:TargetLines", "Target Lines");
            case C_TOOLTIPS: return gf_csf("GUI:Tooltips", "Tooltips");
            case C_HIDDEN: return gf_csf("GUI:ShowHidden", "See Hidden Objects");
            default: return gf_csf("GUI:Shuffle", "Shuffle");
        }
    }
    static const char* check_stt(int i) {
        static const char* k[kChecks] = {"STT:GameOptCBoxTargetLines", "STT:GameOptCBoxTooltips",
                                         "STT:GameOptCBoxHidden", "STT:SoundOptCBoxShuffle"};
        return k[i];
    }
    bool check_val(int i) const {
        switch (i) {
            case C_TARGET: return set_.unit_action_lines;
            case C_TOOLTIPS: return set_.tooltips;
            case C_HIDDEN: return set_.show_hidden;
            default: return set_.is_score_shuffle;
        }
    }
    bool& check_ref(int i) {
        switch (i) {
            case C_TARGET: return set_.unit_action_lines;
            case C_TOOLTIPS: return set_.tooltips;
            case C_HIDDEN: return set_.show_hidden;
            default: return set_.is_score_shuffle;
        }
    }
    // Live apply: the user's settings follow every control change.
    void publish() {
        if (g_user_settings) *g_user_settings = set_;
    }

    Page page_ = P_OPTIONS;
    bool campaign_ = false, soviet_ = false;
    ra2engine::Settings set_{};
    std::vector<DipRow> dip_;   // W2-UI-29
    ShellList tracks_{{157, 157, 262, 160}, {}, {}, {}, 0, -1, 9, 17};
    int mx_ = -1, my_ = -1, drag_ = -1;
    bool down_ = false;
    KeyboardPage kb_;   // W5-E
};
