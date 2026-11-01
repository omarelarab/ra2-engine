// W4-AM-01/03/06/07/08/11: touch-only helpers of GameScene (members, textually
// included in the class body after touch_bar.inc). Nothing here runs for a
// mouse: every entry point checks ev.touch / touch_ui_ / !mouse_present_.
// [Engine affordance, not original UI: the original is mouse + keyboard only.]

// --- W4-AM-03: an armed cursor mode is visible and has a big cancel button ---
// 0 none, 1 sell, 2 repair, 3 placement, 4 superweapon target.
int touch_mode() const {
    if (input_.placement_active()) return 3;
    if (input_.sw_targeting()) return 4;
    if (input_.base_mode() == ra2engine::Input::BaseMode::Sell) return 1;
    if (input_.base_mode() == ra2engine::Input::BaseMode::Repair) return 2;
    return 0;
}
// --- W6-01-12: HUD message / objective / tooltip text is drawn 1.5x on a
// touch canvas (game.fnt's 13-row cells are ~8 dp tall at 1:1 on a phone);
// a mouse keeps the original 1:1. Cameo captions are baked into the art.
double hud_touch_scale() const { return (touch_ui_ && !mouse_present_) ? 1.5 : 1.0; }

bool touch_banner_visible() const {
    return touch_ui_ && !mouse_present_ && touch_mode() != 0 && !menu_.open_ && !sld_.is_open() &&
           !input_locked_;
}
// The banner runs along the bottom of the tactical view, right of the command
// column (the top carries the EVA messages); the [X] cancel button is its
// right end (56x56 logical px).
SDL_Rect touch_banner_rect() const {
    const int x = kTbX + 2 * (kTbW + kTbGap) + 4;
    return SDL_Rect{x, kWinH - 60, std::max(120, playfield_w_ - x - 8), 56};
}
SDL_Rect touch_cancel_rect() const {
    const SDL_Rect b = touch_banner_rect();
    return SDL_Rect{b.x + b.w - 56, b.y, 56, 56};
}
static bool in_rect(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
void touch_cancel_mode() {
    // The same path as a right click: cancel ONE armed mode (placement also
    // reports place_cancelled to the queue logic), never deselect.
    ra2engine::InputEvent e;
    e.kind = ra2engine::InputEvent::PointerDown;
    e.button = ra2engine::InputEvent::Right;
    e.touch = true;
    e.hold = true;
    e.x = mx_;
    e.y = my_;
    input_.handle(e);
    e.kind = ra2engine::InputEvent::PointerUp;
    input_.handle(e);
}
void draw_touch_mode_banner(SDL_Renderer* ren) {
    if (!touch_banner_visible()) return;
    static const char* kText[5] = {"", "SELL - TAP A BUILDING, TAP AGAIN TO SELL",
                                   "REPAIR - TAP A BUILDING, TAP AGAIN TO REPAIR",
                                   "PLACE - TAP THE GROUND, TAP AGAIN TO BUILD",
                                   "TARGET - TAP THE MAP, TAP AGAIN TO FIRE"};
    const SDL_Rect b = touch_banner_rect();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 20, 24, 28, 0xC8);
    SDL_RenderFillRect(ren, &b);
    SDL_SetRenderDrawColor(ren, 230, 190, 60, 0xF0);
    SDL_RenderDrawRect(ren, &b);
    const std::string aim = touch_aim_text();   // W6-01-02 / -07
    const std::string line = aim.empty() ? std::string(kText[touch_mode()]) : aim;
    const int scale = static_cast<int>(line.size()) * 12 <= b.w - 76 ? 2 : 1;
    draw_text(ren, line, b.x + 10, b.y + (b.h - 7 * scale) / 2, scale, 240, 220, 120);
    const SDL_Rect c = touch_cancel_rect();
    SDL_SetRenderDrawColor(ren, 150, 40, 30, 0xE0);
    SDL_RenderFillRect(ren, &c);
    SDL_SetRenderDrawColor(ren, 240, 230, 210, 0xF0);
    SDL_RenderDrawRect(ren, &c);
    draw_text(ren, "X", c.x + (c.w - 15) / 2, c.y + (c.h - 21) / 2, 3, 245, 235, 215);
}

// --- W6-01-02 / W6-01-07: a confirming second tap for sell, repair and a
// superweapon [engine deviation, touch only]. With a mouse the hovered object
// shows the sell / repair / target cursor BEFORE the click (FUN_00692010 ->
// the mode cursor); a finger has no hover, so the first tap only aims: it
// highlights the building (sell / repair) or draws the target area (SW) and
// the banner names it; a second tap on the SAME building / cell commits it
// through the normal Input path. A tap elsewhere re-aims. The modes stay
// armed after a commit, exactly as with the mouse. SW targeting wins over a
// base mode, as in Input::handle. Called from the touch router after the
// banner's [X] and the cameo popup had their chance.
bool touch_confirm_tap(ra2engine::InputEvent& ev) {
    const bool on_map = !(sidebar_up_ && lay_.contains(ev.x, ev.y));
    const bool ptr = ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp;
    const ra2engine::Input::BaseMode bm = input_.base_mode();
    const bool bmode = bm == ra2engine::Input::BaseMode::Sell || bm == ra2engine::Input::BaseMode::Repair;
    if (!bmode) touch_aim_bld_ = -1;
    if (!input_.sw_targeting()) touch_aim_cx_ = touch_aim_cy_ = INT_MIN;
    if (!ptr || !on_map || ev.button != ra2engine::InputEvent::Left || input_.placement_active()) return false;
    if (ev.kind == ra2engine::InputEvent::PointerUp) {
        if (touch_aim_swallow_up_) {
            touch_aim_swallow_up_ = false;
            return true;
        }
        return false;
    }
    if (input_.sw_targeting()) {
        int cx = 0, cy = 0;
        input_.resolve_ground_cell(ev.x, ev.y, cx, cy);
        if (cx == touch_aim_cx_ && cy == touch_aim_cy_) {   // confirm: Input fires
            touch_aim_cx_ = touch_aim_cy_ = INT_MIN;
            return false;
        }
        touch_aim_cx_ = cx;
        touch_aim_cy_ = cy;
        touch_aim_swallow_up_ = true;
        return true;
    }
    if (bmode) {
        const int bi = input_.pick_building(ev.x, ev.y, true);
        const bool own = bi >= 0 && world_.buildings()[static_cast<std::size_t>(bi)].house == local_house_;
        if (own && bi == touch_aim_bld_) {   // the confirming tap: Input commits it
            touch_aim_bld_ = -1;
            return false;
        }
        touch_aim_bld_ = own ? bi : -1;
        touch_aim_swallow_up_ = true;
        return true;
    }
    return false;
}
// The aimed building / target cell of touch_confirm_tap, drawn over the map.
void draw_touch_aim(SDL_Renderer* ren) {
    if (!touch_ui_ || mouse_present_) return;
    auto diamond = [&](int cx, int cy, Uint8 r, Uint8 g, Uint8 b) {
        const ra2assets::MapCell* mc = a_.map.at(cx, cy);
        const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx, cy, mc ? mc->level : 0, 60, 30, 15);
        const int x = p.x + cam_.x, y = p.y + cam_.y;
        SDL_SetRenderDrawColor(ren, r, g, b, 0xF0);
        for (int d = 0; d < 3; ++d) {   // 3 px thick
            const SDL_Point q[5] = {{x + 30, y + d}, {x + 60 - 2 * d, y + 15}, {x + 30, y + 30 - d},
                                    {x + 2 * d, y + 15}, {x + 30, y + d}};
            SDL_RenderDrawLines(ren, q, 5);
        }
    };
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const auto& bs = world_.buildings();
    if (touch_aim_bld_ >= 0 && touch_aim_bld_ < static_cast<int>(bs.size())) {
        const ra2engine::Building& b = bs[static_cast<std::size_t>(touch_aim_bld_)];
        const bool sell = input_.base_mode() == ra2engine::Input::BaseMode::Sell;
        for (int j = 0; j < b.fh; ++j)
            for (int i = 0; i < b.fw; ++i)
                diamond(b.cx + i, b.cy + j, sell ? 255 : 80, sell ? 60 : 255, sell ? 40 : 80);
    }
    if (input_.sw_targeting() && touch_aim_cx_ != INT_MIN) {
        using T = ra2engine::SuperweaponType;
        const T t = static_cast<T>(input_.sw_armed_type());
        // The area the SW acts on: Iron Curtain / Chronosphere 3x3 [world.hpp
        // fire_superweapon_at], the Nuke its [NUKE] CellSpread, the storm its
        // [LightningStormSpecial] CellSpread; a paradrop marks the cell.
        double rad = 0.0;
        if (t == T::Nuke) rad = world_.sw_cell_spread(0, t);
        else if (t == T::LightningStorm) rad = world_.sw_params().storm_cell_spread;
        const int sq = (t == T::IronCurtain || t == T::ChronoSphere || t == T::ChronoWarp) ? 1 : 0;
        for (int j = -sq; j <= sq; ++j)
            for (int i = -sq; i <= sq; ++i) diamond(touch_aim_cx_ + i, touch_aim_cy_ + j, 255, 220, 60);
        if (rad > 0.0) {   // a cell-space circle is an iso ellipse on screen
            const ra2assets::MapCell* mc = a_.map.at(touch_aim_cx_, touch_aim_cy_);
            const ra2engine::ScreenPt p =
                ra2engine::cell_to_screen(touch_aim_cx_, touch_aim_cy_, mc ? mc->level : 0, 60, 30, 15);
            const double ox = p.x + cam_.x + 30, oy = p.y + cam_.y + 15;
            SDL_SetRenderDrawColor(ren, 255, 90, 40, 0xE0);
            const int steps = 180;
            // cell offset (r cos a, r sin a) -> screen ((dx-dy)*30, (dx+dy)*15)
            auto sp = [&](double a, double& sx, double& sy) {
                const double dx = rad * std::cos(a), dy = rad * std::sin(a);
                sx = ox + (dx - dy) * 30.0;
                sy = oy + (dx + dy) * 15.0;
            };
            for (int k = 0; k < steps; ++k) {
                double x0, y0, x1, y1;
                sp(6.2831853 * k / steps, x0, y0);
                sp(6.2831853 * (k + 1) / steps, x1, y1);
                for (int d = 0; d < 2; ++d)
                    SDL_RenderDrawLine(ren, static_cast<int>(x0), static_cast<int>(y0) + d, static_cast<int>(x1),
                                       static_cast<int>(y1) + d);
            }
        }
    }
}
// The banner line while a touch tap is aimed (W6-01-02 / -07); "" = none.
std::string touch_aim_text() const {
    const auto& bs = world_.buildings();
    if (touch_aim_bld_ >= 0 && touch_aim_bld_ < static_cast<int>(bs.size())) {
        const int t = bs[static_cast<std::size_t>(touch_aim_bld_)].type;
        std::string name =
            (t >= 0 && t < static_cast<int>(ba_.roster.size())) ? tt_ui_name(ba_.roster[(size_t)t].id) : "";
        for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const bool sell = input_.base_mode() == ra2engine::Input::BaseMode::Sell;
        return std::string(sell ? "TAP AGAIN TO SELL " : "TAP AGAIN TO REPAIR ") + name;
    }
    if (input_.sw_targeting() && touch_aim_cx_ != INT_MIN) return "TAP THE SAME SPOT AGAIN TO FIRE";
    return "";
}
int touch_aim_bld_ = -1;
int touch_aim_cx_ = INT_MIN, touch_aim_cy_ = INT_MIN;
bool touch_aim_swallow_up_ = false;

// --- W4-AM-07: the hold ring at the finger (fills up until the hold fires) ---
void draw_touch_hold_ring(SDL_Renderer* ren) {
    if (!touch_ui_ || mouse_present_ || !g_platform) return;
    int hx = 0, hy = 0;
    float pr = 0.0f;
    if (!g_platform->touch_hold(hx, hy, pr)) return;
    // A 5 px band (dark rim, bright core) so it reads on any terrain; it
    // fills clockwise from 12 o'clock and turns green when the hold fires.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // TG-04: red while the hold will FORCE FIRE (units selected, no armed
    // mode, playfield) -- green otherwise (cancel / box select).
    const bool fire = !touch_buttons_ && !input_.selection().empty() && touch_mode() == 0 &&
                      !input_.placement_active() && touch_playfield(hx, hy);
    const int r = 28;
    const int steps = 160;
    const int n = static_cast<int>(steps * pr);
    for (int k = 0; k <= n; ++k) {
        const double a = -1.5707963 + 6.2831853 * k / steps;
        const double ca = std::cos(a), sa = std::sin(a);
        for (int d = -2; d <= 2; ++d) {
            const bool rim = d == -2 || d == 2;
            if (rim) SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xC0);
            else if (pr >= 1.0f && fire) SDL_SetRenderDrawColor(ren, 255, 70, 50, 0xFF);
            else if (pr >= 1.0f) SDL_SetRenderDrawColor(ren, 120, 255, 120, 0xFF);
            else SDL_SetRenderDrawColor(ren, 255, 235, 120, 0xFF);
            SDL_RenderDrawPoint(ren, hx + static_cast<int>(std::lround((r + d) * ca)),
                                hy + static_cast<int>(std::lround((r + d) * sa)));
        }
    }
}

// --- W4-AM-01: the gesture hint, shown on touch for 12 s after control starts --
void touch_hint_tick(double dt) {
    if (!touch_ui_ || input_locked_ || touch_hint_left_ <= 0.0) return;
    touch_hint_left_ -= dt;
}
void draw_touch_hint(SDL_Renderer* ren) {
    if (!touch_ui_ || input_locked_ || touch_hint_left_ <= 0.0 || menu_.open_ || sld_.is_open() ||
        gesture_help_) return;
    static const char* kLines[3] = {"DRAG: SCROLL    TAP: SELECT / ORDER",
                                    "HOLD + DRAG: BOX    HOLD ON TARGET: FORCE FIRE",
                                    "ALL GESTURES: OPTIONS > GAME CONTROLS > GESTURES"};
    const int w = std::min(playfield_w_ - 16, 46 * 12 + 16);
    const int y = touch_banner_visible() ? kWinH - 60 - 72 : kWinH - 72;
    const SDL_Rect r{(playfield_w_ - w) / 2, y, w, 66};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 10, 12, 16, 0xB0);
    SDL_RenderFillRect(ren, &r);
    for (int i = 0; i < 3; ++i) {
        const int lw = static_cast<int>(std::string(kLines[i]).size()) * 12 - 2;
        const int scale = lw <= r.w - 8 ? 2 : 1;
        const int tw = static_cast<int>(std::string(kLines[i]).size()) * 6 * scale - scale;
        draw_text(ren, kLines[i], r.x + (r.w - tw) / 2, r.y + 5 + i * 20, scale, 235, 225, 200);
    }
}
double touch_hint_left_ = 12.0;

// --- W4-AM-08: forgiving sidebar targets for a finger ---------------------------
// A tap on the sidebar that hits NO element snaps to the nearest small button
// (tab, strip arrow, repair, sell) within 20 logical px -- the hit area grows
// to the full gap around the art without ever stealing a cameo tap.
bool touch_snap_sidebar(ra2engine::InputEvent& ev) const {
    if (!ev.touch || !sidebar_up_ || !lay_.contains(ev.x, ev.y)) return false;
    if (lay_.cameo_at(ev.x, ev.y) >= 0 || lay_.tab_at(ev.x, ev.y) >= 0 ||
        lay_.scroll_up.contains(ev.x, ev.y) || lay_.scroll_dn.contains(ev.x, ev.y) ||
        lay_.repair_btn.contains(ev.x, ev.y) || lay_.sell_btn.contains(ev.x, ev.y) ||
        lay_.radar.contains(ev.x, ev.y) || lay_.opt_btn.contains(ev.x, ev.y))
        return false;
    const ra2engine::SbRect* best = nullptr;
    int best_d = 21;
    auto consider = [&](const ra2engine::SbRect& r) {
        if (r.w <= 0 || r.h <= 0) return;
        const int dx = ev.x < r.x ? r.x - ev.x : ev.x >= r.x + r.w ? ev.x - (r.x + r.w - 1) : 0;
        const int dy = ev.y < r.y ? r.y - ev.y : ev.y >= r.y + r.h ? ev.y - (r.y + r.h - 1) : 0;
        const int d = std::max(dx, dy);
        if (d < best_d) { best_d = d; best = &r; }
    };
    for (const ra2engine::SbRect& t : lay_.tabs) consider(t);
    consider(lay_.scroll_up);
    consider(lay_.scroll_dn);
    // W6-01-13: the 72x18 Options button (the visible way into the menu when
    // gesture navigation hides Back) snaps like the other small buttons.
    const ra2engine::SbRect ob{lay_.opt_btn.x, lay_.opt_btn.y, ba_.sb_optbtn.width(), ba_.sb_optbtn.height()};
    if (ba_.has_optbtns) consider(ob);   // (`best` may point at ob: it lives to the end)
    if (ba_.has_repair_sell) {
        consider(lay_.repair_btn);
        consider(lay_.sell_btn);
    }
    if (!best) return false;
    ev.x = best->x + best->w / 2;
    ev.y = best->y + best->h / 2;
    return true;
}

// --- the touch router (called from handle() after the touch column) -----------
// Returns true when the event was consumed.
bool touch_ui_handle(ra2engine::InputEvent& ev) {
    if (!ev.touch) return false;
    if (cameo_menu_handle(ev)) return true;   // W6-01-03 cameo popup
    // The mode banner's [X].
    if (touch_banner_visible() && (ev.kind == ra2engine::InputEvent::PointerDown ||
                                   ev.kind == ra2engine::InputEvent::PointerUp) &&
        in_rect(touch_cancel_rect(), ev.x, ev.y)) {
        if (ev.kind == ra2engine::InputEvent::PointerUp) touch_cancel_mode();
        return true;
    }
    // W4-AM-11: an open briefing closes on the next tap.
    if (show_briefing_ && ev.kind == ra2engine::InputEvent::PointerDown &&
        !(sidebar_up_ && lay_.contains(ev.x, ev.y))) {
        show_briefing_ = false;
        touch_swallow_up_ = true;
        return true;
    }
    if (ev.kind == ra2engine::InputEvent::PointerUp && touch_swallow_up_) {
        touch_swallow_up_ = false;
        return true;
    }
    if (touch_confirm_tap(ev)) return true;   // W6-01-02 / W6-01-07
    if (ev.kind == ra2engine::InputEvent::Pan) {
        if (!(sidebar_up_ && lay_.contains(ev.x0, ev.y0))) return false;   // the map scrolls
        // W4-AM-11: a drag that starts on the radar scrolls the view with it.
        if (lay_.radar.contains(ev.x0, ev.y0)) {
            int cx = 0, cy = 0;
            if (radar_active() && radar_cell_at(ev.x, ev.y, cx, cy)) input_.center_on_cell(cx, cy);
            return true;
        }
        // A vertical swipe on the cameo strip scrolls it one row per 48 px.
        touch_strip_acc_ += ev.dy;
        while (touch_strip_acc_ <= -48) { strip_scroll(+1); touch_strip_acc_ += 48; }
        while (touch_strip_acc_ >= 48) { strip_scroll(-1); touch_strip_acc_ -= 48; }
        return true;
    }
    if (ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp)
        touch_strip_acc_ = 0;
    (void)touch_snap_sidebar(ev);   // W4-AM-08 (rewrites ev.x/y onto a button)
    return false;
}
int touch_strip_acc_ = 0;
bool touch_swallow_up_ = false;

// --- W6-01-03: a cameo HOLD on touch shows its tooltip only ------------------
// [engine deviation, touch] With a mouse, hovering a cameo shows the tooltip
// with no side effect [RE: FUN_0067F580] and a RIGHT click suspends, then
// abandons [RE: FUN_006AA390]. A finger's only tooltip gesture is a hold, so
// a hold must not also be the right click: it shows the name + price (above
// the finger, hud_tooltip.inc) and, when that cameo has something in
// production, opens a small popup beside it with the right-click actions:
//   [PAUSE] / [RESUME]  suspend / resume (right click / left click)
//   [CANCEL]            abandon one (a queued copy first, else the current)
//   [ALL]               abandon every queued copy (Shift + right click)
// A tap anywhere else closes it (and acts normally).
struct CameoMenuBtn { int id; const char* label; SDL_Rect r; };
enum : int { CM_PAUSE = 1, CM_RESUME, CM_CANCEL, CM_ALL };
// Production state of cameo `type` on the active tab: current / held / count.
void cameo_menu_state(int type, bool& current, bool& held, bool& ready, int& count) const {
    current = held = ready = false;
    count = 0;
    if (active_tab_ >= 2) {
        const ra2engine::QueueCat cat = rp_cat_of_unit(type);
        current = lq().producing(cat) == type;
        held = world_.econ_held(local_house_, cat);
        ready = current && lq().ready(cat);
        count = queued_count(type, cat);
    } else {
        const ra2engine::QueueCat bcat =
            (active_tab_ == 1) ? ra2engine::QueueCat::Defense : ra2engine::QueueCat::Structure;
        const ra2engine::ProductionState& bq = lq().state(bcat);
        current = bq.slot.type == type;
        held = current && world_.econ_held(local_house_, bcat);
        ready = current && bq.slot.ready;
        count = current ? 1 : 0;
    }
}
std::vector<CameoMenuBtn> cameo_menu_buttons() const {
    std::vector<CameoMenuBtn> out;
    if (cm_type_ < 0 || cm_tab_ != active_tab_) return out;
    bool current = false, held = false, ready = false;
    int count = 0;
    cameo_menu_state(cm_type_, current, held, ready, count);
    if (count <= 0 && !current) return out;
    if (current && held) out.push_back({CM_RESUME, "RESUME", {}});
    else if (current && !ready) out.push_back({CM_PAUSE, "PAUSE", {}});
    out.push_back({CM_CANCEL, "CANCEL", {}});
    if (active_tab_ >= 2 && count >= 2) out.push_back({CM_ALL, "ALL", {}});
    // Stacked to the LEFT of the cameo (the finger rests on the cameo).
    const int bw = 84, bh = 44, gap = 2;
    const int n = static_cast<int>(out.size());
    int y = cm_y_ + cm_h_ / 2 - (n * bh + (n - 1) * gap) / 2;
    y = std::clamp(y, 0, std::max(0, kWinH - n * (bh + gap)));
    for (int i = 0; i < n; ++i) out[i].r = SDL_Rect{cm_x_ - bw - 6, y + i * (bh + gap), bw, bh};
    return out;
}
void cameo_menu_open(int type, const ra2engine::SbRect& r) {
    cm_type_ = type;
    cm_tab_ = active_tab_;
    cm_x_ = r.x;
    cm_y_ = r.y;
    cm_h_ = r.h;
    if (cameo_menu_buttons().empty()) cm_type_ = -1;   // nothing to pause / cancel
}
void cameo_menu_do(int id) {
    const int type = cm_type_;
    cm_type_ = -1;
    if (type < 0) return;
    switch (id) {
        case CM_PAUSE: cameo_click(type, /*right=*/true, false); return;    // SUSPEND
        case CM_RESUME: cameo_click(type, /*right=*/false, false); return;  // PRODUCE = resume
        case CM_ALL: cameo_click(type, /*right=*/true, /*shift=*/true); return;
        case CM_CANCEL:
            // W9-05-01 [RE: every production change is an EVENT -- ABANDON 0x10
            // through the DoList, EventClass::Execute FUN_004C6BD0]: a queued
            // copy first, else the current one, applied by cmd_abandon on the
            // command's tick on every peer (was a direct World call on this
            // peer only -> the match desynced within 15 frames).
            issue_type_event(ra2engine::EventType::Abandon, /*building=*/active_tab_ < 2, type);
            pres_.note_eva("EVA_Canceled");   // W7-12-01 [RE: FUN_006AA390 @6AA4C4]
            return;
        default: return;
    }
}
// Router hook: true when the event was consumed by the open popup.
bool cameo_menu_handle(const ra2engine::InputEvent& ev) {
    if (cm_type_ < 0) return false;
    const bool ptr = ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp;
    if (!ptr) return false;
    for (const CameoMenuBtn& b : cameo_menu_buttons())
        if (ev.x >= b.r.x && ev.x < b.r.x + b.r.w && ev.y >= b.r.y && ev.y < b.r.y + b.r.h) {
            if (ev.kind == ra2engine::InputEvent::PointerUp) cameo_menu_do(b.id);
            return true;
        }
    if (ev.kind == ra2engine::InputEvent::PointerDown) cm_type_ = -1;   // a tap elsewhere closes it
    return false;
}
void draw_cameo_menu(SDL_Renderer* ren) {
    const std::vector<CameoMenuBtn> bs = cameo_menu_buttons();
    if (bs.empty()) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (const CameoMenuBtn& b : bs) {
        SDL_SetRenderDrawColor(ren, b.id == CM_PAUSE || b.id == CM_RESUME ? 30 : 120, 30, 28, 0xE0);
        SDL_RenderFillRect(ren, &b.r);
        SDL_SetRenderDrawColor(ren, 230, 190, 60, 0xF0);
        SDL_RenderDrawRect(ren, &b.r);
        const std::string l = b.label;
        const int lw = static_cast<int>(l.size()) * 12 - 2;
        draw_text(ren, l, b.r.x + (b.r.w - lw) / 2, b.r.y + (b.r.h - 14) / 2, 2, 240, 230, 210);
    }
}
int cm_type_ = -1, cm_tab_ = -1, cm_x_ = 0, cm_y_ = 0, cm_h_ = 0;
