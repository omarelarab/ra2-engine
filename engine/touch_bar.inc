// W2-AR-08 / -07 / -16, W4-AM-03/05/11: the touch command column (GameScene
// members, textually included in the class body). Every hotkey-only command
// gets an on-screen affordance once the player uses touch (or always on Android):
//
//   [CMD] open / close the column
//   [+SEL] sticky Shift  (taps ADD to the selection)
//   [FIRE] one-shot Ctrl (force-fire the next order)
//   [MOVE] one-shot Alt  (force-move the next order)
//   [STOP] [GRD] [SCAT] [DPLY]  StopObject / GuardObject / ScatterObject / DeployObject
//   [BOX]  W6-01-11: sticky -- while on, a one-finger drag draws the band box
//          at once (no hold); two fingers still scroll
//   [BRF]  the mission briefing (F1; campaign only)
//   [1]..[5]  tap = TeamSelect_n (double tap centres), hold = TeamCreate_n
//   [MORE] W6-01-09: page 2 -- the hotkey-only commands:
//          [TYPE] TypeSelect (T)   [HOME] CenterBase (H)
//          [EVT]  CenterOnRadarEvent (Space)   [NEXT] / [PREV] Next / PreviousObject
//          [FOLW] Follow (F)   [6]..[0] teams 6-10 (tap select, hold create)
//          [V1]..[V3] View1-3 (tap = jump, hold = SetView)   [BACK] page 1
//
// W4-AM-05: the column sits along the LEFT edge of the tactical view and is
// CLOSED by default (only [CMD] at the left edge), so it never covers
// the bottom of the playfield; it closes itself after a one-shot command and
// after 6 s without use. The armed modifiers stay visible on [CMD] when closed.
// W4-AM-03: an armed cursor mode (sell / repair / placement / superweapon
// target) shows a banner with a 56 px [X] cancel button (touch_ui.inc).
//
// The buttons call the SAME command handlers the KeyboardMD.ini hotkeys use
// (game_scene_controls.inc hotkey()), so behaviour is identical. Buttons are
// 50x56 logical px: >= 42x48 dp on a phone canvas. Desktop with a mouse never
// shows it. [Engine affordance, not original UI: the original is mouse + keyboard only.]
//
// TG (touch gestures): since the gesture set (touch_gestures.hpp) covers every
// column command, the column is OFF by default; RA2.INI [Engine]
// TouchButtons=yes (main menu Options > Touch Buttons) brings it back. Without
// it the left edge carries only TEAM TABS for the teams that exist, plus a "+"
// tab while units are selected (tap = select, tap again = centre, hold =
// reassign; "+" makes the next free team). The gesture side lives below:
// touch_gesture() (the Gesture events), touch_gesture_orders() (hold = force
// fire / guard, ground double tap = force move), the finger label + haptic,
// and the one-time gesture cheat sheet (also Options > Game Controls > Gestures).
struct TouchBtn { const char* label; int id; };
enum : int { TB_TOGGLE = 1, TB_ADD, TB_FIRE, TB_MOVE, TB_STOP, TB_GUARD, TB_SCATTER, TB_DEPLOY, TB_BRIEF,
             TB_BOX, TB_MORE, TB_TYPE, TB_HOME, TB_EVENT, TB_NEXT, TB_PREV, TB_FOLLOW,
             TB_VIEW1 = 90, TB_GROUP1 = 100 };
// W6-01-09: 8 rows (8 x 58 = 464 logical px fits the 548-px phone canvas below
// the EVA lines), two columns per page.
static constexpr int kTbW = 50, kTbH = 56, kTbGap = 2, kTbX = 4, kTbRows = 8;
// The column is vertically centred on the left edge, below the EVA message lines.
int tb_y0() const { return std::max(56, (kWinH - kTbRows * (kTbH + kTbGap)) / 2); }
static constexpr double kTbAutoClose = 6.0;   // seconds without use
const std::vector<TouchBtn>& touch_buttons() const {
    static const std::vector<TouchBtn> all = {
        {"CMD", TB_TOGGLE}, {"+SEL", TB_ADD}, {"FIRE", TB_FIRE}, {"MOVE", TB_MOVE},
        {"STOP", TB_STOP}, {"GRD", TB_GUARD}, {"SCAT", TB_SCATTER}, {"DPLY", TB_DEPLOY},
        {"BOX", TB_BOX}, {"BRF", TB_BRIEF},
        {"1", TB_GROUP1}, {"2", TB_GROUP1 + 1}, {"3", TB_GROUP1 + 2}, {"4", TB_GROUP1 + 3}, {"5", TB_GROUP1 + 4},
        {"MORE", TB_MORE}};
    static const std::vector<TouchBtn> no_brief = [] {
        std::vector<TouchBtn> v;
        for (const TouchBtn& b : all)
            if (b.id != TB_BRIEF) v.push_back(b);
        return v;
    }();
    // W6-01-09: page 2 -- the KeyboardMD.ini commands with no other touch path.
    static const std::vector<TouchBtn> more = {
        {"CMD", TB_TOGGLE}, {"TYPE", TB_TYPE}, {"HOME", TB_HOME}, {"EVT", TB_EVENT},
        {"NEXT", TB_NEXT}, {"PREV", TB_PREV}, {"FOLW", TB_FOLLOW}, {"6", TB_GROUP1 + 5},
        {"7", TB_GROUP1 + 6}, {"8", TB_GROUP1 + 7}, {"9", TB_GROUP1 + 8}, {"0", TB_GROUP1 + 9},
        {"V1", TB_VIEW1}, {"V2", TB_VIEW1 + 1}, {"V3", TB_VIEW1 + 2}, {"BACK", TB_MORE}};
    if (touch_page2_) return more;
    return campaign_ ? all : no_brief;
}
bool touch_bar_visible() const {
    return touch_buttons_ && touch_ui_ && !menu_.open_ && !sld_.is_open() && !gf_abort_.open() && !input_locked_;
}
// ---- TG-15: team tabs (the column's replacement) --------------------------------
bool touch_tabs_visible() const {
    return !touch_buttons_ && touch_ui_ && !mouse_present_ && !menu_.open_ && !sld_.is_open() &&
           !gf_abort_.open() && !input_locked_ && !gesture_help_;
}
struct TeamTab { int n; SDL_Rect r; };   // n = 0: the "+" tab
static constexpr int kTabW = 44, kTabH = 40, kTabGap = 4;
std::vector<TeamTab> team_tabs() const {
    std::vector<TeamTab> v;
    if (!touch_tabs_visible()) return v;
    int y = tb_y0();
    int free_n = 0;
    for (int n = 1; n <= 10; ++n) {
        if (input_.group_size(n) <= 0) {
            if (!free_n) free_n = n;
            continue;
        }
        if (y + kTabH > kWinH) break;
        v.push_back({n, SDL_Rect{kTbX, y, kTabW, kTabH}});
        y += kTabH + kTabGap;
    }
    if (free_n && !input_.selection().empty() && y + kTabH <= kWinH)
        v.push_back({0, SDL_Rect{kTbX, y, kTabW, kTabH}});
    return v;
}
int team_tab_at(int x, int y, int* n_out = nullptr) const {
    const std::vector<TeamTab> v = team_tabs();
    for (std::size_t i = 0; i < v.size(); ++i) {
        const SDL_Rect& r = v[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            if (n_out) *n_out = v[i].n;
            return static_cast<int>(i);
        }
    }
    return -1;
}
int first_free_team() const {
    for (int n = 1; n <= 10; ++n)
        if (input_.group_size(n) <= 0) return n;
    return 0;
}
bool touch_tabs_handle(const ra2engine::InputEvent& ev) {
    if (!ev.touch) return false;
    if (ev.kind == ra2engine::InputEvent::Pan && team_tab_at(ev.x0, ev.y0) >= 0) return true;
    const bool ptr = ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp;
    if (!ptr) return false;
    int n = -1;
    const int ti = team_tab_at(ev.x, ev.y, &n);
    if (ti < 0) {
        if (ev.kind == ra2engine::InputEvent::PointerUp && touch_tab_down_ >= 0) { touch_tab_down_ = -1; return true; }
        return false;
    }
    if (ev.kind == ra2engine::InputEvent::PointerDown) { touch_tab_down_ = n; return true; }
    if (touch_tab_down_ != n) { touch_tab_down_ = -1; return true; }
    touch_tab_down_ = -1;
    const bool hold = ev.button == ra2engine::InputEvent::Right && ev.hold;
    if (n == 0) {   // "+": the selection becomes the next free team
        const int f = first_free_team();
        if (f) {
            input_.group_create(f);
            touch_flash("TEAM " + std::to_string(f % 10), ev.x + kTabW, ev.y);
        }
    } else if (hold) {   // reassign: the current selection replaces team n
        if (!input_.selection().empty()) {
            input_.group_create(n);
            touch_flash("TEAM " + std::to_string(n % 10) + " SET", ev.x + kTabW, ev.y);
        }
    } else {
        input_.group_select(n, ev.t_ms);   // a second tap within 800 ms centres (TeamCenter)
    }
    return true;
}
void draw_team_tabs(SDL_Renderer* ren) {
    const std::vector<TeamTab> v = team_tabs();
    if (v.empty()) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (const TeamTab& t : v) {
        SDL_SetRenderDrawColor(ren, 20, 24, 28, t.n ? 0xB0 : 0x70);
        SDL_RenderFillRect(ren, &t.r);
        SDL_SetRenderDrawColor(ren, 200, 70, 55, t.n ? 0xE0 : 0x90);
        SDL_RenderDrawRect(ren, &t.r);
        const std::string label = t.n ? std::to_string(t.n % 10) : "+";
        const int scale = 3;
        const int lw = static_cast<int>(label.size()) * 6 * scale - scale;
        draw_text(ren, label, t.r.x + (t.r.w - lw) / 2, t.r.y + (t.r.h - 7 * scale) / 2, scale, 235, 225, 200);
    }
}

// ---- TG: gesture commands, finger label, haptic ----------------------------------
void touch_flash(const std::string& text, int x, int y) {
    touch_label_ = text;
    touch_label_x_ = x;
    touch_label_y_ = y;
    touch_label_left_ = 0.7;
    ra2engine::os_haptic();
}
bool touch_playfield(int x, int y) const {
    return !(sidebar_up_ && lay_.contains(x, y)) && x < playfield_w_;
}
// The Gesture events (TouchGestures): the same handlers as the hotkeys.
void touch_gesture(const ra2engine::InputEvent& ev) {
    if (menu_.open_ || sld_.is_open() || gf_abort_.open() || input_locked_ || gesture_help_) return;
    const char* label = nullptr;
    const bool sel = !input_.selection().empty();
    switch (ev.gesture) {
        case ra2engine::kGestStop:    if (sel) { input_.stop_selected(); label = "STOP"; } break;
        case ra2engine::kGestScatter: if (sel) { input_.scatter_selected(); label = "SCATTER"; } break;
        case ra2engine::kGestDeploy:  if (sel) { deploy_selected(); label = "DEPLOY"; } break;
        case ra2engine::kGestNext:    cycle_object(+1); label = "NEXT"; break;
        case ra2engine::kGestPrev:    cycle_object(-1); label = "PREVIOUS"; break;
        case ra2engine::kGestHome:    center_base(); label = "BASE"; break;
        case ra2engine::kGestEvent:   hotkey_radar_event(ev.t_ms); label = "LAST EVENT"; break;
        case ra2engine::kGestFollow:  if (sel) { hotkey_follow(); label = "FOLLOW"; } break;
        default: break;
    }
    if (label) touch_flash(label, ev.x, ev.y);
}
// TG-04 / TG-07 / TG-05: what a hold-release or a ground double tap means
// while units are selected (no armed cursor mode). Rewrites `ev` in place;
// returns true when the event was consumed.
bool touch_gesture_orders(ra2engine::InputEvent& ev) {
    const bool down = ev.kind == ra2engine::InputEvent::PointerDown;
    const bool up = ev.kind == ra2engine::InputEvent::PointerUp;
    if (!down && !up) return false;
    const bool free_play = touch_playfield(ev.x, ev.y) && !input_.placement_active() && touch_mode() == 0 &&
                           !input_.selection().empty();
    // Hold-release = a right click marked hold.
    if (ev.button == ra2engine::InputEvent::Right && ev.hold) {
        if (down) {
            touch_hold_act_ = 0;
            if (!free_play) return false;   // cancels the armed mode / nothing, as before
            const int u = input_.pick_unit_at(ev.x, ev.y);
            touch_hold_act_ = (u >= 0 && input_.selection().has(u)) ? 2 : 1;   // 2 guard, 1 force fire
        }
        if (touch_hold_act_ == 2) {
            if (up) {
                input_.guard_selected();
                touch_flash("GUARD", ev.x, ev.y);
                touch_hold_act_ = 0;
            }
            return true;
        }
        if (touch_hold_act_ == 1) {   // a Ctrl left click on the target = force fire
            ev.button = ra2engine::InputEvent::Left;
            ev.ctrl = true;
            ev.hold = false;
            if (up) {
                touch_flash("FORCE FIRE", ev.x, ev.y);
                touch_hold_act_ = 0;
            }
        }
        return false;
    }
    if (ev.button != ra2engine::InputEvent::Left) return false;
    // TG-05: a second tap on (nearly) the same empty ground = force move.
    if (down) {
        touch_alt_up_ = false;
        if (free_play && !ev.shift && touch_last_tap_t_ && ev.t_ms - touch_last_tap_t_ <= 350 &&
            std::abs(ev.x - touch_last_tap_x_) <= 32 && std::abs(ev.y - touch_last_tap_y_) <= 32 &&
            input_.pick_unit_at(ev.x, ev.y) < 0 && input_.pick_building(ev.x, ev.y, true) < 0) {
            ev.alt = true;
            touch_alt_up_ = true;
            touch_last_tap_t_ = 0;
            touch_flash("FORCE MOVE", ev.x, ev.y);
        }
        return false;
    }
    if (touch_alt_up_) {
        ev.alt = true;
        touch_alt_up_ = false;
        return false;
    }
    if (free_play && !ev.shift && input_.pick_unit_at(ev.x, ev.y) < 0) {
        touch_last_tap_t_ = ev.t_ms ? ev.t_ms : 1;
        touch_last_tap_x_ = ev.x;
        touch_last_tap_y_ = ev.y;
    } else {
        touch_last_tap_t_ = 0;
    }
    return false;
}
void draw_touch_flash(SDL_Renderer* ren) {
    if (touch_label_left_ <= 0.0 || touch_label_.empty()) return;
    const int scale = 2;
    const int lw = static_cast<int>(touch_label_.size()) * 6 * scale - scale;
    int x = touch_label_x_ - lw / 2, y = touch_label_y_ - 56;
    x = std::max(4, std::min(x, playfield_w_ - lw - 4));
    y = std::max(4, y);
    const SDL_Rect r{x - 6, y - 5, lw + 12, 7 * scale + 10};
    const Uint8 a = static_cast<Uint8>(std::min(1.0, touch_label_left_ / 0.25) * 0xC0);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 10, 12, 16, a);
    SDL_RenderFillRect(ren, &r);
    draw_text(ren, touch_label_, x, y, scale, 255, 230, 120);
}

// ---- TG-16: the gesture cheat sheet (first game on a touch device; also
// Options > Game Controls > Gestures). A tap closes it. ---------------------------
static bool gesture_help_first_time() {
#if defined(__ANDROID__) || defined(RA2_IOS)
    return !engine_ini_flag("GestureHelpSeen", false);
#else
    return false;
#endif
}
bool touch_help_handle(const ra2engine::InputEvent& ev) {
    if (!gesture_help_) return false;
    if (ev.kind == ra2engine::InputEvent::PointerDown) { touch_help_closing_ = true; return true; }
    if (ev.kind == ra2engine::InputEvent::PointerUp) {
        if (touch_help_closing_) {
            gesture_help_ = false;
            touch_help_closing_ = false;
            engine_ini_set_flag("GestureHelpSeen", true);
        }
        return true;
    }
    return ev.kind == ra2engine::InputEvent::Pan || ev.kind == ra2engine::InputEvent::Gesture ||
           ev.kind == ra2engine::InputEvent::PointerMove;
}
void draw_gesture_help(SDL_Renderer* ren) {
    if (!gesture_help_) return;
    static const char* kLines[] = {
        "TOUCH CONTROLS",
        "",
        "TAP: SELECT / ORDER          DRAG: SCROLL",
        "DOUBLE TAP A UNIT: ALL OF ITS TYPE",
        "HOLD, THEN DRAG: BOX SELECT",
        "REST ONE FINGER + TAP / DRAG: ADD TO SELECTION",
        "HOLD ON A TARGET: FORCE FIRE",
        "HOLD ON YOUR SELECTED UNIT: GUARD",
        "DOUBLE TAP GROUND: FORCE MOVE",
        "TWO-FINGER TAP: CANCEL / DESELECT",
        "TWO-FINGER HOLD: STOP",
        "PINCH OUT: SCATTER      PINCH IN: DEPLOY",
        "TWO-FINGER DOUBLE TAP: CENTRE ON BASE",
        "THREE-FINGER TAP: LAST EVENT",
        "THREE-FINGER SWIPE: NEXT / PREVIOUS UNIT",
        "THREE-FINGER HOLD: FOLLOW UNIT",
        "TEAMS: '+' TAB MAKES ONE, HOLD A TAB TO REASSIGN",
        "",
        "TAP TO CLOSE  (OPTIONS > GAME CONTROLS > GESTURES)",
    };
    const int n = static_cast<int>(sizeof kLines / sizeof kLines[0]);
    std::size_t longest = 0;
    for (const char* l : kLines) longest = std::max(longest, std::string(l).size());
    const int avail = playfield_w_ - 24;
    const int scale = static_cast<int>(longest) * 12 + 24 <= avail && n * 20 + 24 <= kWinH - 16 ? 2 : 1;
    const int lh = 7 * scale + 6;
    const int w = static_cast<int>(longest) * 6 * scale + 24, h = n * lh + 24;
    const SDL_Rect r{std::max(8, (playfield_w_ - w) / 2), std::max(8, (kWinH - h) / 2), w, h};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 8, 10, 14, 0xE0);
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, 200, 70, 55, 0xE0);
    SDL_RenderDrawRect(ren, &r);
    for (int i = 0; i < n; ++i) {
        const std::string line = kLines[i];
        const int tw = static_cast<int>(line.size()) * 6 * scale - scale;
        const bool head = i == 0;
        draw_text(ren, line, r.x + (r.w - tw) / 2, r.y + 12 + i * lh, scale,
                  head ? 255 : 235, head ? 200 : 225, head ? 90 : 200);
    }
}
// Button i's rectangle: column-major, kTbRows per column, from the top-left.
SDL_Rect touch_btn_rect(int i) const {
    const int col = i / kTbRows, row = i % kTbRows;
    return SDL_Rect{kTbX + col * (kTbW + kTbGap), tb_y0() + row * (kTbH + kTbGap), kTbW, kTbH};
}
int touch_btn_count() const {
    return touch_bar_open_ ? static_cast<int>(touch_buttons().size()) : 1;
}
int touch_btn_at(int x, int y) const {
    if (!touch_bar_visible()) return -1;
    const int n = touch_btn_count();
    for (int i = 0; i < n; ++i) {
        const SDL_Rect r = touch_btn_rect(i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && r.y + r.h <= kWinH) return i;
    }
    return -1;
}
void touch_command(int id, bool long_press, uint32_t t_ms) {
    touch_bar_idle_ = 0.0;
    bool one_shot = true;
    switch (id) {
        case TB_TOGGLE: touch_bar_open_ = !touch_bar_open_; touch_page2_ = false; one_shot = false; break;
        case TB_MORE: touch_page2_ = !touch_page2_; one_shot = false; break;   // W6-01-09
        case TB_BOX:   // W6-01-11: sticky band-box drag
            touch_box_ = !touch_box_;
            ra2engine::TouchGestures::set_box_mode(touch_box_);
            one_shot = false;
            break;
        case TB_TYPE: input_.type_select(); break;          // TypeSelect FUN_00731A20
        case TB_HOME: center_base(); break;                 // CenterBase
        case TB_EVENT: hotkey_radar_event(t_ms); break;     // CenterOnRadarEvent
        case TB_NEXT: cycle_object(+1); one_shot = false; break;   // NextObject (repeatable)
        case TB_PREV: cycle_object(-1); one_shot = false; break;   // PreviousObject
        case TB_FOLLOW: hotkey_follow(); break;              // Follow
        case TB_ADD: touch_add_ = !touch_add_; one_shot = false; break;
        case TB_FIRE: touch_fire_ = !touch_fire_; touch_move_ = false; one_shot = false; break;
        case TB_MOVE: touch_move_ = !touch_move_; touch_fire_ = false; one_shot = false; break;
        case TB_STOP: input_.stop_selected(); break;
        case TB_GUARD: input_.guard_selected(); break;
        case TB_SCATTER: input_.scatter_selected(); break;
        case TB_DEPLOY: deploy_selected(); break;
        case TB_BRIEF: show_briefing_ = !show_briefing_; break;
        default:
            if (id >= TB_VIEW1 && id < TB_VIEW1 + 3) {   // W6-01-09: View / SetView 1-3
                const int v = id - TB_VIEW1;
                if (long_press) {
                    view_cam_[v][0] = cam_.x;
                    view_cam_[v][1] = cam_.y;
                    view_set_[v] = true;
                } else if (view_set_[v]) {
                    cam_.x = view_cam_[v][0];
                    cam_.y = view_cam_[v][1];
                }
            }
            if (id >= TB_GROUP1 && id < TB_GROUP1 + 10) {
                const int n = id - TB_GROUP1 + 1;
                if (long_press) input_.group_create(n);
                else input_.group_select(n, t_ms);
            }
            break;
    }
    if (one_shot) { touch_bar_open_ = false; touch_page2_ = false; }   // W4-AM-05: out of the way again
}
// Consumes taps on the bar; stamps the modifier toggles on other touch
// pointer events (so Input sees Shift / Ctrl / Alt exactly as from a
// keyboard). Returns true when the event was consumed.
bool touch_bar_handle(ra2engine::InputEvent& ev) {
    if (ev.touch) touch_ui_ = true;
    if (touch_help_handle(ev)) return true;   // TG-16 cheat sheet open
    if (ev.kind == ra2engine::InputEvent::Gesture) {   // TG: multi-finger commands
        touch_gesture(ev);
        return true;
    }
    if (touch_tabs_handle(ev)) return true;   // TG-15 team tabs
    // A drag that starts on the column does not scroll the map.
    if (ev.kind == ra2engine::InputEvent::Pan && ev.touch && touch_btn_at(ev.x0, ev.y0) >= 0) return true;
    const bool ptr = ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp;
    if (!ptr) return false;
    const int bi = touch_btn_at(ev.x, ev.y);
    if (bi >= 0) {
        if (ev.kind == ra2engine::InputEvent::PointerDown) {
            touch_down_btn_ = bi;
        } else if (touch_down_btn_ == bi) {
            touch_command(touch_buttons()[static_cast<size_t>(bi)].id,
                          ev.button == ra2engine::InputEvent::Right, ev.t_ms);   // hold = right
            touch_down_btn_ = -1;
        }
        return true;
    }
    if (ev.kind == ra2engine::InputEvent::PointerUp && touch_down_btn_ >= 0) {   // slid off a button
        touch_down_btn_ = -1;
        return true;
    }
    if (!ev.touch) return false;
    if (!touch_buttons_ && touch_gesture_orders(ev)) return true;   // TG-04/05/07
    // W2-AR-16c: placing a building by touch -- the first tap shows the
    // footprint there, a second tap on the same cell places it.
    if (input_.placement_active() && !(sidebar_up_ && lay_.contains(ev.x, ev.y))) {
        ra2engine::InputEvent mv = ev;
        mv.kind = ra2engine::InputEvent::PointerMove;
        input_.handle(mv);
        int cx = 0, cy = 0;
        input_.placement_cell(cx, cy);
        if (ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left) {
            if (cx != touch_place_cx_ || cy != touch_place_cy_) {
                touch_place_cx_ = cx;
                touch_place_cy_ = cy;
                touch_place_swallow_up_ = true;
                return true;   // preview only
            }
        } else if (touch_place_swallow_up_) {
            touch_place_swallow_up_ = false;
            return true;
        }
    } else {
        touch_place_cx_ = touch_place_cy_ = INT_MIN;
    }
    // Modifier toggles (playfield only; the sidebar ignores modifiers).
    if (!(sidebar_up_ && lay_.contains(ev.x, ev.y))) {
        ev.shift = ev.shift || touch_add_;
        ev.ctrl = ev.ctrl || touch_fire_;
        ev.alt = ev.alt || touch_move_;
        // FIRE / MOVE are one-shot: spent by the order (a left PointerUp).
        if (ev.kind == ra2engine::InputEvent::PointerUp && ev.button == ra2engine::InputEvent::Left &&
            (touch_fire_ || touch_move_)) {
            touch_fire_ = touch_move_ = false;
        }
    }
    return false;
}
// Per frame: the open column closes itself after kTbAutoClose seconds unused.
void touch_bar_tick(double dt) {
    if (touch_label_left_ > 0.0) touch_label_left_ -= dt;
    // W6-01-11: BOX drags only on the open playfield (menus / dialogs scroll).
    ra2engine::TouchGestures::set_box_mode(touch_box_ && touch_bar_visible());
    if (!touch_bar_open_) { touch_bar_idle_ = 0.0; return; }
    touch_bar_idle_ += dt;
    if (touch_bar_idle_ >= kTbAutoClose) { touch_bar_open_ = false; touch_page2_ = false; }
}
void draw_touch_bar(SDL_Renderer* ren) {
    draw_team_tabs(ren);
    draw_touch_flash(ren);
    draw_gesture_help(ren);
    if (!touch_bar_visible()) return;
    const auto& bs = touch_buttons();
    const int n = touch_btn_count();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < n; ++i) {
        const SDL_Rect r = touch_btn_rect(i);
        if (r.y + r.h > kWinH) break;
        const int id = bs[static_cast<size_t>(i)].id;
        const bool armed_mod = touch_add_ || touch_fire_ || touch_move_ || touch_box_;
        const bool on = (id == TB_ADD && touch_add_) || (id == TB_FIRE && touch_fire_) ||
                        (id == TB_MOVE && touch_move_) || (id == TB_TOGGLE && (touch_bar_open_ || armed_mod)) ||
                        (id == TB_BRIEF && show_briefing_) || (id == TB_BOX && touch_box_) ||
                        (id == TB_MORE && touch_page2_);
        SDL_SetRenderDrawColor(ren, on ? 150 : 20, on ? 40 : 24, on ? 30 : 28, 0xB0);
        SDL_RenderFillRect(ren, &r);
        SDL_SetRenderDrawColor(ren, 200, 70, 55, 0xE0);
        SDL_RenderDrawRect(ren, &r);
        std::string label = bs[static_cast<size_t>(i)].label;
        if (id == TB_TOGGLE && !touch_bar_open_ && armed_mod)   // closed: show what is armed
            label = touch_add_ ? "+SEL" : touch_fire_ ? "FIRE" : touch_move_ ? "MOVE" : "BOX";
        const int scale = label.size() <= 2 ? 3 : 2;
        const int lw = static_cast<int>(label.size()) * 6 * scale - scale;
        draw_text(ren, label, r.x + (r.w - lw) / 2, r.y + (r.h - 7 * scale) / 2, scale, 235, 225, 200);
    }
}
#if defined(__ANDROID__) || defined(RA2_IOS)
bool touch_ui_ = true;    // a phone: the column is there from the first frame
#else
bool touch_ui_ = false;   // desktop: appears after the first touch event
#endif
bool touch_bar_open_ = false;   // W4-AM-05: closed by default
double touch_bar_idle_ = 0.0;
bool touch_add_ = false, touch_fire_ = false, touch_move_ = false;
bool touch_box_ = false;     // W6-01-11 sticky BOX (TouchGestures::set_box_mode)
bool touch_page2_ = false;   // W6-01-09 the MORE page
int touch_down_btn_ = -1;
int touch_place_cx_ = INT_MIN, touch_place_cy_ = INT_MIN;
bool touch_place_swallow_up_ = false;
// TG: the gesture side.
// The old column instead of tabs. The --play harness keeps it: the touch
// scripts (scripts/play/ctl-touch-*.play) drive it by its labels.
bool touch_buttons_ = g_play_harness || engine_ini_flag("TouchButtons", false);
bool gesture_help_ = gesture_help_first_time();
bool touch_help_closing_ = false;
int touch_tab_down_ = -1;
int touch_hold_act_ = 0;            // 1 force fire, 2 guard (the hold being released)
bool touch_alt_up_ = false;         // TG-05: the force-move tap's PointerUp gets Alt too
uint32_t touch_last_tap_t_ = 0;
int touch_last_tap_x_ = 0, touch_last_tap_y_ = 0;
std::string touch_label_;
int touch_label_x_ = 0, touch_label_y_ = 0;
double touch_label_left_ = 0.0;
