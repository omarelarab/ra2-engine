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
    return touch_ui_ && !menu_.open_ && !sld_.is_open() && !gf_abort_.open() && !input_locked_;
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
    // W6-01-11: BOX drags only on the open playfield (menus / dialogs scroll).
    ra2engine::TouchGestures::set_box_mode(touch_box_ && touch_bar_visible());
    if (!touch_bar_open_) { touch_bar_idle_ = 0.0; return; }
    touch_bar_idle_ += dt;
    if (touch_bar_idle_ >= kTbAutoClose) { touch_bar_open_ = false; touch_page2_ = false; }
}
void draw_touch_bar(SDL_Renderer* ren) {
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
