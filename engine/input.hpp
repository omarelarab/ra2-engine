#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "command.hpp"       // NET L1: Command / OrderRules
#include "iso_renderer.hpp"  // Camera
#include "platform.hpp"      // InputEvent
#include "selection.hpp"     // NET L1: the local selection (UI state)
#include "world.hpp"         // World / Unit

namespace ra2engine {

// Reports what handle() did since the last take_actions() call, for audio
// voice-ack hooks (Task 5 of the audio-playback phase) -- pure observation,
// no behavior change to selection/orders. ACCUMULATE-THEN-CONSUME: handle()
// never resets this; each commit/order site ORs its flag in (actor = lowest
// selected unit index, captured before the order mutates state), and
// Input::take_actions() is the single consume point -- it returns the
// accumulated struct and clears it. WHY: the game loop feeds a whole frame's
// event batch (a click's Down+Up typically arrive together) and then reads
// the actions ONCE per frame -- accumulation means the Down-recorded action
// survives the batch's trailing events without special-casing any of them,
// and consuming means an idle frame (or a frame of plain mouse moves) reads
// back all-false instead of re-triggering the previous frame's ack forever.
// Within one consume window the flags are independent bits: a select-click
// and a right-click order in the same frame both report (Task 6's ack
// precedence -- selected, else attack, else move -- picks one voice line).
struct InputActions {
    bool selected = false;       // a commit produced a non-empty selection
    bool move_ordered = false;   // right-click group move issued
    bool attack_ordered = false; // right-click attack issued
    int actor = -1;              // representative unit index (lowest selected)

    // --- Buildings phase: placement mode (Task 7a) ---
    bool place_confirmed = false;   // left-click confirmed a placement this batch
    bool place_cancelled = false;   // right-click/Esc cancelled placement this batch
    int place_cx = 0, place_cy = 0; // confirmed foundation ORIGIN cell

    // CB-4 Superweapons [spec §4]: a left-click committed a SW target cell while
    // SW-targeting was armed (mirrors place_confirmed/place_cx/cy). main.cpp
    // routes this to World::fire_superweapon. Default inert (no existing flow
    // arms SW-targeting, so no existing demo/frame reports it).
    bool sw_fire_confirmed = false;
    int sw_cx = 0, sw_cy = 0;

    // Repair & Sell [RE: repair-sell.md §C1]: a left-click in Repair/Sell mode
    // that landed on an OWN (house 0) alive building. main.cpp routes these to
    // World::toggle_repair / World::begin_sell. Default inert -- no existing
    // flow arms either mode, so no existing demo/frame reports them.
    bool repair_clicked = false;
    int repair_building = -1;
    bool sell_clicked = false;
    int sell_building = -1;

    // G-10: a left click selected an own building (any type).
    bool building_selected = false;
    // ACTION_SELF: a left click on the (already) selected own deployable unit
    // -- main.cpp runs the DeployObject command (unload / deploy).
    bool deploy_clicked = false;
    // W5-E2: a click in PowerToggleMode on an own building.
    bool power_clicked = false;
    int power_building = -1;

    // W5-E [RE: TechnoClass::ClickedMission FUN_006FF0A0]: the mission the
    // order click gave picks the acknowledgement voice -- Harvest(10) ->
    // VoiceHarvest, Enter(7) -> VoiceEnter, Capture(8) -> VoiceCapture,
    // Unload(16) -> VoiceDeploy, any other non-Attack/Move mission (Sabotage)
    // -> VoiceSpecialAttack. A type without the key falls back (Capture ->
    // Enter -> Move, Harvest/Enter -> Move). Presentation-only.
    enum class Order { Move, Enter, Capture, Harvest, Special, Deploy };
    Order order = Order::Move;
    // W5-E [RE: presentation.md §1.3 @448171 / FUN_004437D0 @4439D4]: a player
    // click made a factory primary / set its rally point.
    bool primary_set = false;
    bool rally_set = false;
    int factory = -1;
    int target_building = -1;   // attack order on a building
};

// Translates pointer InputEvents (Task 0 Platform) into actions on a World +
// Camera. PURE engine logic -- no SDL (only the SDL-free InputEvent struct,
// screen_to_cell, and World) -- so it is unit-testable without a window.
//
// RA2-classic gesture model (multi-unit selection, Task 5; combat, Task 6):
//   * Left-click on a unit selects only it; on empty ground clears selection.
//     A hostile (house != 0) is NEVER selectable -- a left-click landing on
//     one is treated like a click on empty ground.
//   * Left-drag (past a small threshold) draws a selection box; releasing it
//     box-selects every house-0, alive unit whose ground point falls inside
//     (hostiles and corpses are skipped even if boxed).
//   * Shift+left-click toggles a unit in/out of the selection; Shift+left-drag
//     adds the boxed units to the existing selection.
//   * A double-click (two quick left-clicks near the same spot) selects every
//     on-screen house-0 unit of the same image/type as the one clicked; a
//     double-click landing on a hostile does nothing.
//   * Right-click on a hostile (house != 0) issues an attack order
//     (World::order_attack) to the selection; on anything else (empty ground,
//     or a friendly unit) it group-moves the selection (order_move_selected)
//     to the clicked cell, resolved LEVEL-AWARE (see handle()'s two-pass
//     inversion) so raised terrain is targeted at the tile actually drawn
//     under the cursor.
//   * Dead/dying units are unpickable everywhere (unit_under skips them), so
//     neither selection nor the right-click attack path can ever target a
//     corpse.
//   * Camera panning is no longer driven by a left-drag -- main.cpp calls
//     Input::pan() directly (arrow keys / screen-edge scroll).
class Input {
public:
    // Level-aware window pixel -> ground cell (the click resolver; public for
    // the --play harness's `place auto` round-trip check).
    void resolve_ground_cell(int px, int py, int& cx, int& cy) const;
    Input(World& world, Camera& camera, int view_w, int view_h);

    // --- NET L1: selection + command output ----------------------------------
    // The local player's unit selection (UI state, not the World's).
    Selection& selection() { return sel_; }
    const Selection& selection() const { return sel_; }
    // Every order Input gives leaves as a Command. With a sink installed (the
    // game's scheduler) it is handed over; without one it is applied to the
    // World immediately (unit tests, tools) -- same result, no tick boundary.
    void set_command_sink(std::function<void(Command)> sink) { sink_ = std::move(sink); }
    // The issuing house of this Input's commands: the local player, i.e. the
    // World's order issuer (L2 set_order_issuer).
    int local_house() const { return world_.order_issuer(); }
    // The gesture tables an apply needs too (refineries, factory groups, spy).
    const OrderRules& order_rules() const { return rules_; }

    // Feed one polled event (RA2-classic model, see input.cpp).
    void handle(const InputEvent& ev);

    // --- G-05 mouse scheme [RE: gamemd fixB hunt] -------------------------
    // Original (the DEFAULT, what gamemd.exe does): the tactical WndProc
    // FUN_00692AB0 routes WM_LBUTTONUP to DisplayClass Left_Up FUN_004AB8C0,
    // which with a selection runs Mouse_Left_Release FUN_004AE660 -- every
    // selected object gets What_Action(target) + Active_Click_With, i.e. a
    // LEFT click issues the order (move/attack/enter/capture/...); a left
    // click on an own object whose action is ACTION_SELECT re-selects (Shift
    // = toggle); with nothing selected a ground click does nothing. A
    // WM_RBUTTONDOWN starts a right-drag scroll; WM_RBUTTONUP without a drag
    // runs FUN_004AAC40 = cancel ONE armed mode, else FUN_0048DBA0 (deselect
    // everything). Legacy = the clone's pre-G-05 model (left selects, right
    // orders), kept only for the old unit tests.
    enum class Scheme { Original, Legacy };
    void set_scheme(Scheme s) { scheme_ = s; }
    Scheme scheme() const { return scheme_; }

    // What a left click at window pixel (px, py) would do RIGHT NOW (the
    // What_Action probe), using the modifier keys of the latest event. This
    // is the cursor STATE for the renderer (agent E draws the art): one value
    // per frame for the pointer position. Mode actions (placement, SW
    // targeting, repair/sell) take precedence exactly as in handle().
    enum class Action {
        None,          // arrow cursor, a click does nothing
        Select,        // own unit: (re)select it
        ToggleSelect,  // Shift + own unit
        SelectBuilding,// own building: select it (HP bar, rally/primary if a factory)
        Move, NoMove,  // ground move / unreachable (impassable or off-map) cell
        Attack,        // enemy unit/building, or Ctrl force-fire
        Enter,         // own transport with eligible passengers selected
        Deploy,        // the selected own unit itself (ACTION_SELF): unload / deploy
        Harvest,       // harvester(s) selected, ore cell
        Dock,          // harvester(s) selected, own refinery
        Capture,       // lone engineer, enemy/neutral building
        EngineerRepair,// lone engineer, own damaged building
        Infiltrate,    // lone spy, enemy/neutral building
        Rally,         // own factory selected, ground click
        SetPrimary,    // re-click the selected own factory
        Place,         // building placement (foundation legality is main.cpp's)
        SuperWeapon,   // SW targeting armed
        Repair, NoRepair, Sell, NoSell,   // repair / sell modes
    };
    Action action_at(int px, int py) const;
    static const char* action_name(Action a);

    // --- G-04 hotkey commands [RE: gamemd CommandClass Execute fns] --------
    // Each mirrors the original command's effect on the current selection.
    // Stop (StopObject FUN_0072FF70: per selected own object queue EVENT_IDLE)
    // -> drop the target/enter/unload and halt at the next cell.
    void stop_selected();
    // Guard (GuardObject FUN_0072FE30: MegaMission(Area_Guard 0xB, self))
    // -> stop, then anchor the guard point at the unit's current cell.
    void guard_selected();
    // Scatter (ScatterObject FUN_007300B0: per selected own object queue
    // EVENT_SCATTER) -> each unit steps to a free adjacent cell.
    void scatter_selected();
    // TypeSelect (FUN_00731A20): select every own unit sharing an image with
    // the selection, on screen first; when all on-screen ones are already
    // selected, across the whole map. Returns true if anything is selected.
    bool type_select();
    // Control groups [RE: TeamCreate FUN_00730130, TeamSelect FUN_00730290,
    // TeamAddSelect FUN_00730590, TeamCenter FUN_00730470]. n = 1..10 (10 is
    // the '0' key). Each object belongs to at most ONE group: create clears
    // group n from every unit then assigns it to the selected ones. The three
    // select variants first clear every cursor mode. TeamSelect pressed again
    // within 800 ms on the same group centres the view (TeamCenter).
    void group_create(int n);
    void group_select(int n, uint32_t t_ms);
    void group_add_select(int n);
    void group_center(int n);
    // TG-15 (touch team tabs): live, controllable units in group n.
    int group_size(int n) const;
    int group_of(int unit_idx) const {
        return (unit_idx >= 0 && unit_idx < static_cast<int>(unit_group_.size()))
                   ? unit_group_[unit_idx] : 0;
    }
    // Centre the view on the selection (FUN_004AE1A0: mean position; with
    // >2 objects the one farthest from the mean is excluded).
    void center_on_selection();
    // Centre the camera on (fractional) map cell (cx, cy); `level` lifts the
    // point by the cell height (W4-MC-03: the home waypoint's ground point).
    void center_on_cell(double cx, double cy, int level = 0);
    // W4-MC-04 [RE: FUN_00577e90 IsWithinUsableArea, [Map] LocalSize]: keep
    // the view inside a WORLD-pixel rectangle (screen = world + cam). A view
    // larger than the rectangle is centred on it (TacticalClass clamp
    // FUN_006d7bb0 bounds the view centre the same way).
    void set_view_limits(int min_x, int min_y, int max_x, int max_y);
    // R-162: the tactical view changed size (window resize, foldable, split
    // screen): recompute the default camera clamp for the new viewport.
    // set_view_limits (a campaign's LocalSize) must be re-applied after it.
    void set_viewport(int view_w, int view_h);
    // W4-MC-02: drop a half-done gesture (band box / right-drag) without
    // acting on it (LockInput's FUN_0054f3b0).
    void cancel_gesture() { l_down_ = dragging_ = false; r_down_ = r_drag_ = false; }
    // Clear every cursor mode (repair / sell / SW targeting; a pending
    // placement survives) -- the group-hotkey prologue.
    void clear_cursor_modes();
    // Right-click-up semantics (FUN_004AAC40): cancel one armed mode, else
    // deselect everything (units and the selected building).
    void right_click_cancel();
    void deselect_all();
    // ACTION_SELF predicate: can this selected own unit deploy / unload? The
    // default covers transports with cargo and bound Deployers; GameScene may
    // widen it (e.g. agent F's MCV -> ConYard).
    void set_deploy_check(std::function<bool(const Unit&)> f) { deploy_check_ = std::move(f); }

    // --- Buildings phase: placement mode (Task 7a) -------------------------
    // While active, a left Down+Up (no drag) confirms placement at the hovered
    // foundation ORIGIN cell (reported via InputActions::place_confirmed +
    // place_cx/cy); a right click OR cancel_placement() (main.cpp routes the
    // Escape key here) cancels back to READY. Selection/attack/move gestures
    // are suppressed while placing. main.cpp routes sidebar-rect clicks BEFORE
    // Input::handle (the sidebar consumes them), so Input never needs to know
    // the sidebar exists.
    void begin_placement(int roster_type, int fw, int fh);
    void cancel_placement();
    bool placement_active() const { return place_type_ >= 0; }

    // CB-4: arm/disarm SW-targeting -- a lightweight parallel of placement mode.
    // While armed, a left-click on the map reports sw_fire_confirmed + sw_cx/cy
    // (via the SAME level-aware screen->cell pick placement uses); a right-click/
    // Esc cancels. UI state only -- the sim is untouched until main.cpp calls
    // World::fire_superweapon. No existing flow arms this, so this branch is
    // never entered in any existing demo.
    void arm_superweapon() { sw_targeting_ = true; }
    // W2-AN-04: which SW type the armed targeting is for (SuperweaponType as
    // int; the cursor shape) and the Chronosphere's second (destination) click.
    void arm_superweapon(int type, int phase) { sw_targeting_ = true; sw_type_ = type; sw_phase_ = phase; }
    int sw_armed_type() const { return sw_type_; }
    int sw_armed_phase() const { return sw_phase_; }
    void cancel_superweapon() { sw_targeting_ = false; }
    bool sw_targeting() const { return sw_targeting_; }
    int placement_type() const { return place_type_; }

    // --- Repair & Sell cursor modes [RE: repair-sell.md §C1] --------------
    // The original's DisplayClass carries FIVE mutually-exclusive one-byte mode
    // flags (RepairMode +0x11B0, SellMode +0x11B1, PowerToggleMode +0x11B2,
    // PlanningMode +0x11B3, PlaceBeaconMode +0x11B4) and EVERY setter clears
    // the other four. The last three have no feature behind them in this engine
    // (no per-building power toggle, no waypoint planning, no beacons), so they
    // are recorded but not modeled: the exclusion set here is
    // {pending placement, sw_targeting, Repair, Sell}.
    enum class BaseMode { None, Repair, Sell, PowerToggle };   // W5-E2 PowerToggleMode (+0x11B2)

    // SetRepairMode / SetSellMode (FUN_004AC7D0 / FUN_004AC570), transcribed:
    //   1. control -1 toggle / 0 off / 1 on; no-op if the value is unchanged;
    //   2. REFUSED outright while a placement is pending (placement WINS);
    //   3. clears every other mode (here: the sibling mode + sw_targeting);
    //   4. arming requires the player to own at least one building
    //      (PlayerPtr->[0x2F0] > 0, §D5 CONFIRMED), and calls Reset_Cursor
    //      (FUN_0048DBA0) -- which DESELECTS EVERYTHING.
    // The mode is NOT one-shot: it stays armed until cancelled, so several
    // buildings can be repaired/sold in a row.
    void set_repair_mode(int control);
    void set_sell_mode(int control);
    // W5-E2 [RE: DisplayClass::SetPowerToggleMode FUN_004AC730, same shape as
    // the two above]. In gamemd.exe 1.001 nothing ARMS it (every caller passes
    // 0: the group hotkeys, Next/PrevObject, the cancel chain) -- the TS-era
    // sidebar power button is gone (SidebarClass::Action FUN_006A6E10 only
    // knows 0x8065 repair / 0x8066 sell). The clone keeps the mode reachable
    // through the touch command column / a cheat [deviation].
    void set_power_mode(int control);
    BaseMode base_mode() const { return mode_; }

    // The ESC / right-click chain (FUN_004AAC40): cancels EXACTLY ONE thing per
    // press, in the recovered priority order
    //   pending placement -> RepairMode -> SellMode -> PowerToggleMode
    //   -> pending superweapon -> PlanningMode -> PlaceBeaconMode -> (nothing).
    // The three unmodeled modes are simply absent from the chain. Returns true
    // iff something was cancelled.
    bool cancel_one_mode();

    // HouseClass+0x2F0 = the player's owned-building count [RE: repair-sell.md
    // §D5, CONFIRMED]: arming either mode is refused when it is zero. main.cpp
    // refreshes it once per frame (alive, non-Dead house-0 buildings -- a
    // building mid-sell still counts, §D5 residual).
    void set_owned_buildings(int n) { owned_buildings_ = n; }
    int owned_buildings() const { return owned_buildings_; }

    // The alive, non-Dead building under window pixel (px, py), or -1. Used
    // by main.cpp's mode cursor; = pick_building(px, py, latest event touch).
    int building_under(int px, int py) const { return pick_building(px, py, ev_touch_); }
    // W6-01-01: the building a click / tap at window pixel (px, py) picks.
    // [RE: FUN_00691d00 -> FUN_006d98f0] The original first scans the
    // tactical's list of drawn objects (DAT_00b0be30 {obj, x, y}, filled each
    // redraw by FUN_006d9250 for every alive on-map building at the screen
    // point of its Location = the origin cell's centre, and by FUN_006d9460
    // for infantry / vehicles / aircraft at their draw point) and keeps the
    // nearest entry with dx*dx + 0.5*dy*dy < 200 (a ~14 x 20 px ellipse);
    // only then does it take the first object of the level-aware cell under
    // the point (FUN_006d5b00 -> cell+0xE4). So a mouse picks a building by
    // its foundation (plus that small hotspot), never by its upper art.
    // Touch (engine deviation, W6-01-01): a finger has no cursor preview, so
    // a tap first tests every building's DRAWN ART (the opaque pixels of the
    // frame the renderer draws; set_building_art_test) front-most first, then
    // falls back to the original rule.
    int pick_building(int px, int py, bool touch) const;
    // W6-01-01: the renderer's art test -- true when building index `b`'s
    // drawn frame has an opaque pixel at window pixel (px, py).
    void set_building_art_test(std::function<bool(int, int, int)> f) { art_hit_ = std::move(f); }
    // The building whose drawn art (touch test) holds (px, py), front first; -1.
    int building_art_at(int px, int py) const;
    // P3 (play harness): the unit a click at (px, py) would hit, or -1.
    int pick_unit_at(int px, int py) const { return unit_under(px, py); }
    // Hovered foundation ORIGIN cell for the CURRENT pointer position:
    // resolve_ground_cell(cur) minus half the foundation, so the cursor sits
    // near the foundation's center. Meaningful only while placement_active().
    void placement_cell(int& cx, int& cy) const;

    // Pan the camera by (dx, dy) window pixels, clamped to the map bounds.
    // Called by main.cpp for arrow-key + screen-edge scrolling (left-drag no
    // longer pans -- it draws the selection box).
    void pan(int dx, int dy);

    // If a left-drag selection box is active, fill its normalized rectangle
    // (x0<=x1, y0<=y1) in window pixels and return true; else return false.
    bool selection_box(int& x0, int& y0, int& x1, int& y1) const;

    // Economy phase: the roster type indices that are refineries
    // (GAREFN/NAREFN) -- the SAME list main.cpp's wire_economy hands to
    // World::set_economy. Gates the right-click "return to refinery" order:
    // with a harvester selected, a right-click on an own alive building
    // issues order_return ONLY when that building's type is in this list; a
    // click on any other own building stays a plain ground move. Empty
    // (never called) = no building ever triggers a return order.
    void set_refinery_types(std::vector<int> types) {
        refinery_types_ = std::move(types);
        rules_.refinery_types = refinery_types_;
    }

    // Production phase [RE: production.md §1.2/§1.4]: the roster-index lists of
    // the player's UNIT factories, used by the factory-select / rally / primary
    // gestures. `unit_factory_types_` is the UNION (any left-click on an own
    // alive building of one of these types selects it for rally/primary); the
    // per-category lists (infantry = {GAPILE}, vehicle = {GAWEAP}) let the
    // primary-set trigger resolve the SELECTED factory's category so
    // set_primary clears the right sibling group. All own-house 0 only.
    void set_unit_factory_types(std::vector<int> t) { unit_factory_types_ = std::move(t); }
    void set_infantry_factory_types(std::vector<int> t) {
        inf_factory_types_ = std::move(t);
        rules_.inf_factory_types = inf_factory_types_;
    }
    void set_vehicle_factory_types(std::vector<int> t) {
        veh_factory_types_ = std::move(t);
        rules_.veh_factory_types = veh_factory_types_;
    }

    // Engineer/Spy Capture phase [RE: capture.md §B]: the per-roster-index
    // infiltration purpose map (main.cpp resolves it from building ids:
    // GAREFN/NAREFN->Refinery, GAPOWR/NAPOWR->PowerPlant, a SW building->
    // Superweapon, GAPILE/NAHAND/GAWEAP/NAWEAP->Factory, radar/lab->RadarLab,
    // else None) plus the two rules constants the spy gesture passes through
    // (SpyMoneyStealPercent=0.5, SpyPowerBlackout=1000). Empty/unset (default)
    // -> every building resolves to None and the spy gesture no-ops. Interactive-
    // only: no demo selects a spy, so no gate exercises any of this.
    void set_infiltrate_params(std::vector<World::InfiltratePurpose> purposes,
                               double steal_pct, int blackout_frames) {
        infiltrate_purposes_ = std::move(purposes);
        spy_steal_pct_ = steal_pct;
        spy_blackout_frames_ = blackout_frames;
        rules_.infiltrate_purposes = infiltrate_purposes_;
        rules_.spy_steal_pct = steal_pct;
        rules_.spy_blackout_frames = blackout_frames;
    }

    // The own (house 0) factory currently selected for rally/primary, or -1.
    // Consumed by main.cpp's rally-marker chrome.
    int selected_building() const { return selected_building_; }

    bool wants_quit() const { return quit_; }

    // W6-01-05 [engine deviation, touch]: a finger tap on the lone selected
    // deployable unit (ACTION_SELF) waits out the double-tap window before it
    // deploys, so the first tap of a double tap never deploys. GameScene calls
    // this once per frame (before take_actions) with the frame's dt; when the
    // window has passed with no second tap it reports deploy_clicked.
    void touch_tick(double dt) {
        if (touch_deploy_wait_ < 0.0) return;
        touch_deploy_wait_ -= dt;
        if (touch_deploy_wait_ <= 0.0) {
            touch_deploy_wait_ = -1.0;
            actions_.deploy_clicked = true;
        }
    }

    // Consume the actions accumulated by every handle() call since the
    // previous take_actions(): returns them and resets the accumulator (see
    // InputActions above for the accumulate-then-consume rationale). Call
    // ONCE per frame, after the frame's event-poll loop.
    InputActions take_actions() {
        InputActions out = actions_;
        actions_ = InputActions{};
        return out;
    }

private:
    // G-05: the order half of a click (the legacy right-click body): rally,
    // attack (is_enemy), harvest/dock, enter, spy, engineer, move.
    void issue_order(int px, int py);
    void handle_original(const InputEvent& ev);   // Scheme::Original dispatch
    void left_click_original(int px, int py, bool shift, uint32_t t_ms);
    // Shared classifier behind action_at (selection/order decision).
    Action classify(int px, int py, bool shift, bool ctrl, bool alt) const;
    bool any_own_selected() const;
    bool selected_building_valid() const;
    bool cell_walkable(int cx, int cy) const;
    void sync_groups();
    void clamp_camera();
    // Lowest index i with units()[i].selected == true, or -1 if none.
    int lowest_selected() const;
    // Screen pixel -> unit index or -1. Skips dead/dying units (corpses are
    // unpickable everywhere this is used, including the right-click attack
    // path) but still returns hostiles -- attack targeting needs them; it is
    // the CALLERS (commit_click/commit_box/commit_double) that additionally
    // restrict selection to house == 0.
    int unit_under(int px, int py) const;
    void unit_ground_px(const Unit& u, int& gx, int& gy) const;  // ground point in window px
    // Level-aware two-pass window-pixel -> ground cell (the exact resolution
    // the right-click move path uses); shared with placement_cell.
    void commit_click(int px, int py, bool shift);  // house-0-only selection (see .cpp)
    void commit_box(bool shift);                    // house-0-only, corpse-skipping box select
    void commit_double(int px, int py);             // house-0-only type-select
    // True iff roster type `t` is in refinery_types_ (set_refinery_types).
    bool is_refinery_type(int t) const {
        for (int r : refinery_types_)
            if (r == t) return true;
        return false;
    }
    // True iff roster type `t` is one of the player's unit-factory types.
    bool is_unit_factory_type(int t) const {
        for (int r : unit_factory_types_)
            if (r == t) return true;
        return false;
    }
    // Engineer/Spy Capture [RE: capture.md §B]: the infiltration purpose for a
    // building of roster type `t` (None if unset or out of range).
    World::InfiltratePurpose infiltrate_purpose_of(int t) const {
        return (t >= 0 && t < static_cast<int>(infiltrate_purposes_.size()))
                   ? infiltrate_purposes_[t]
                   : World::InfiltratePurpose::None;
    }
    // Factory-select / primary-set on a non-drag left click [RE: production.md
    // §1.2]. If the click resolves to an own (house 0) alive unit-factory:
    // re-clicking the ALREADY-selected factory sets it primary; otherwise it
    // becomes the selection (clearing any unit selection). Returns true when a
    // factory was handled (so the caller skips unit selection / double-click);
    // returns false for a click NOT on an own factory, after clearing the
    // factory selection so the caller's normal selection logic runs.
    bool commit_factory_click(int px, int py);

    World& world_;
    Camera& cam_;
    bool quit_ = false;
    // NET L1.
    Selection sel_;
    OrderRules rules_;
    std::function<void(Command)> sink_;
    void emit(Command c);

    InputActions actions_;  // accumulator; cleared only by take_actions()

    // Left-button gesture state.
    bool l_down_ = false;
    bool dragging_ = false;
    bool down_shift_ = false;
    int down_x_ = 0, down_y_ = 0, cur_x_ = 0, cur_y_ = 0;

    // Double-click detection (uses event t_ms; no clock read here).
    uint32_t last_click_t_ = 0;
    int last_click_x_ = 0, last_click_y_ = 0;
    int touch_last_kind_ = 0;   // W6-01-05: 1 = the last touch tap selected / re-tapped an own unit
    double touch_deploy_wait_ = -1.0;   // W6-01-05: a deferred touch deploy (seconds left; < 0 none)

    // Placement mode (Task 7a): place_type_ >= 0 while placing a building of
    // that roster type; place_fw_/fh_ is its foundation size.
    int place_type_ = -1;
    int place_fw_ = 1, place_fh_ = 1;

    // CB-4 Superweapons: true while SW-targeting is armed (a click on the map
    // then reports sw_fire_confirmed). Default false -- inert in every demo.
    bool sw_targeting_ = false;
    int sw_type_ = 0;    // W2-AN-04 armed SW type (cursor)
    int sw_phase_ = 0;   // 1 = Chronosphere destination pick

    // Repair & Sell cursor modes [RE: repair-sell.md §C1]. Default None +
    // 0 owned -> both modes are unreachable until main.cpp arms one.
    BaseMode mode_ = BaseMode::None;
    int owned_buildings_ = 0;
    void set_mode_(BaseMode want, int control);

    // Economy phase: refinery roster types (set_refinery_types).
    std::vector<int> refinery_types_;

    // Production phase: unit-factory roster types + the currently selected own
    // factory (set_unit_factory_types / commit_factory_click). -1 = none.
    std::vector<int> unit_factory_types_, inf_factory_types_, veh_factory_types_;
    int selected_building_ = -1;

    // Engineer/Spy Capture phase [RE: capture.md §B]: per-roster-index spy
    // infiltration purpose + the two rules constants (set_infiltrate_params).
    // Empty/defaults -> the spy gesture no-ops (interactive-only).
    std::vector<World::InfiltratePurpose> infiltrate_purposes_;
    double spy_steal_pct_ = 0.5;       // SpyMoneyStealPercent
    int spy_blackout_frames_ = 1000;   // SpyPowerBlackout

    // G-05/G-04 state.
    Scheme scheme_ = Scheme::Original;
    bool mod_shift_ = false, mod_ctrl_ = false, mod_alt_ = false;  // latest event's modifiers
    bool ev_touch_ = false;                    // W4-AM-09: latest pointer event was a finger
    bool box_has_own_unit() const;             // W4-AM-01: the band box holds a controllable unit
    bool r_down_ = false, r_drag_ = false;     // right-button gesture (drag = scroll)
    int r_last_x_ = 0, r_last_y_ = 0, r_down_x_ = 0, r_down_y_ = 0;
    std::vector<int> unit_group_;              // per unit index: group 1..10, 0 = none
    int last_group_ = 0;                       // TeamSelect double-press detection
    uint32_t last_group_t_ = 0;
    std::function<bool(const Unit&)> deploy_check_;
    std::function<bool(int, int, int)> art_hit_;   // W6-01-01 renderer art test
    // W6-01-01/06: a finger tap hits a unit's BODY (the original's hotspot
    // ellipse, see pick_building), not just the generous selection box.
    bool unit_body_hit(int idx, int px, int py) const;

    // Camera clamp bounds (computed once from the map's screen bbox + viewport;
    // same rule as the previous drag clamp -- see input.cpp).
    int view_w_ = 0, view_h_ = 0;
    int min_cam_x_ = 0, max_cam_x_ = 0, min_cam_y_ = 0, max_cam_y_ = 0;
};

}  // namespace ra2engine
