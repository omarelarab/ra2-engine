// G-05 / G-04 / G-10: the ORIGINAL mouse scheme, cursor-action probe, hotkey
// commands and control groups of Input. Split from input.cpp so the legacy
// model stays readable. Pure engine logic (no SDL). Interactive-only: no gate
// constructs an Input, so nothing here can move a gate hash.
//
// Sources [RE: gamemd.exe, hunt "fixB" 2026-10, scripts D:\ra2-re\scripts\fixB_*.py]:
//   FUN_00692AB0  tactical WndProc: WM_LBUTTONDOWN -> Left_Down FUN_004AADA0 +
//                 band start FUN_004AC220; WM_LBUTTONUP -> Left_Up FUN_004AB8C0;
//                 WM_RBUTTONDOWN -> right-drag scroll start; WM_RBUTTONUP ->
//                 (no drag) FUN_004AAC40 cancel chain.
//   FUN_00692010  action of the click: with CurrentObjects.Count != 0 it is the
//                 first selected object's What_Action(cell|object).
//   FUN_004AB8C0  Left_Up: ACTION_SELECT(7) -> Unselect_All FUN_0048DBA0 + Select;
//                 ACTION_TOGGLE_SELECT(8); any other action -> Mouse_Left_Release
//                 FUN_004AE660 = Active_Click_With on EVERY selected object.
//   FUN_004AAC40  cancel placement -> Repair -> Sell -> PowerToggle -> SW ->
//                 Planning -> Beacon -> else FUN_0048DBA0 (Unselect_All).
#include "input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "cargo.hpp"   // can_load (W7-01-30)
#include "ra2assets/map.hpp"

namespace ra2engine {

namespace {
constexpr int kDragThresholdO = 6;      // same band threshold as the legacy model
constexpr uint32_t kDblMsO = 400;
constexpr int kDblSlopO = 6;
// W4-AM-09: a finger's double tap lands ~5 dp apart at best; touch uses a
// 32 logical px slop (~24 dp on a phone canvas) and a 500 ms window.
constexpr int kDblSlopTouch = 32;
constexpr uint32_t kDblMsTouch = 500;
constexpr int kLevelPxO = 15;           // one height level in screen px (input.cpp kLevelPx)
// [RE: FUN_00730290] TeamSelect re-press window: timeGetTime() delta < 800 ms.
constexpr uint32_t kGroupRepeatMs = 800;

// W7-01-30 / W7-04-31: may any selected own unit board transport `ti`?
bool can_board(const World& w, int ti, const Selection& sel) {
    for (int si : sel.ids()) {
        if (si >= static_cast<int>(w.units().size())) break;
        const Unit& su = w.units()[si];
        if (si == ti || !w.controllable(su.house) || !su.alive || su.die_time >= 0.0) continue;
        if (can_load(w.units(), ti, si, [&w](int a, int b) { return w.are_allied(a, b); })) return true;
    }
    return false;
}

// W7-01-05: does any selected own unit take a ground move (a deployed
// Deployer infantry of a human house does not)?
bool selection_can_move(const World& w, const Selection& sel) {
    bool any = false;
    for (int i : sel.ids()) {
        if (i >= static_cast<int>(w.units().size())) break;
        const Unit& u = w.units()[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || !w.controllable(u.house)) continue;
        any = true;
        if (!(u.deployed && !u.is_vehicle && (u.deploy_fire || u.abil.deployer))) return true;
    }
    return !any;
}

// W7-01-04 [RE: Mouse_Left_Release FUN_004AE660 -> each selected object's
// What_Action]: the first selected own spy / engineer (any selection size),
// -1 if none. W7-01-33: by the Agent / Infiltrate / Engineer type flags.
int first_selected_role(const World& w, const Selection& sel, bool spy) {
    for (int i : sel.ids()) {
        if (i >= static_cast<int>(w.units().size())) break;
        const Unit& u = w.units()[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || !w.controllable(u.house)) continue;
        if (spy ? w.unit_is_spy(u) : w.unit_is_engineer(u)) return i;
    }
    return -1;
}
}  // namespace

bool Input::any_own_selected() const {
    for (int i : sel_.ids()) {
        if (i >= static_cast<int>(world_.units().size())) break;
        const Unit& u = world_.units()[i];
        if (world_.controllable(u.house) && u.alive && u.die_time < 0.0) return true;
    }
    return false;
}

bool Input::selected_building_valid() const {
    const auto& b = world_.buildings();
    return selected_building_ >= 0 && selected_building_ < static_cast<int>(b.size()) &&
           b[selected_building_].alive && b[selected_building_].state != Building::State::Dead;
}

bool Input::cell_walkable(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= world_.grid_w() || cy >= world_.grid_h()) return false;
    // W2-AN-09: walkable for the SELECTION's movement domains (a ship moves on
    // the water grid, a landing craft on either, an aircraft anywhere).
    return world_.group_can_enter(sel_.ids(), world_.order_issuer(), cx, cy);
}

const char* Input::action_name(Action a) {
    switch (a) {
        case Action::None: return "none";
        case Action::Select: return "select";
        case Action::ToggleSelect: return "toggle_select";
        case Action::SelectBuilding: return "select_building";
        case Action::Move: return "move";
        case Action::NoMove: return "nomove";
        case Action::Attack: return "attack";
        case Action::Enter: return "enter";
        case Action::Deploy: return "deploy";
        case Action::Harvest: return "harvest";
        case Action::Dock: return "dock";
        case Action::Capture: return "capture";
        case Action::EngineerRepair: return "engineer_repair";
        case Action::Infiltrate: return "infiltrate";
        case Action::Rally: return "rally";
        case Action::SetPrimary: return "set_primary";
        case Action::Place: return "place";
        case Action::SuperWeapon: return "superweapon";
        case Action::Repair: return "repair";
        case Action::NoRepair: return "norepair";
        case Action::Sell: return "sell";
        case Action::NoSell: return "nosell";
    }
    return "none";
}

Input::Action Input::action_at(int px, int py) const {
    return classify(px, py, mod_shift_, mod_ctrl_, mod_alt_);
}

Input::Action Input::classify(int px, int py, bool shift, bool ctrl, bool alt) const {
    // Mode actions win, in handle()'s precedence order.
    if (sw_targeting_) return Action::SuperWeapon;
    if (placement_active()) return Action::Place;
    if (mode_ != BaseMode::None) {
        const int b = building_under(px, py);
        const bool own = b >= 0 && world_.buildings()[b].house == world_.order_issuer();
        if (mode_ == BaseMode::Repair) return own ? Action::Repair : Action::NoRepair;
        if (mode_ == BaseMode::PowerToggle) return own ? Action::Repair : Action::NoRepair;   // [RE-FALLBACK: power cursor]
        return own ? Action::Sell : Action::NoSell;
    }
    const auto& us = world_.units();
    int idx = unit_under(px, py);
    int cx = 0, cy = 0;
    resolve_ground_cell(px, py, cx, cy);
    // W6-01-01: the object pick (touch: a building's drawn art first).
    const int b_art = ev_touch_ ? building_art_at(px, py) : -1;
    const int b = b_art >= 0 ? b_art : pick_building(px, py, false);
    const bool units_sel = any_own_selected();
    // Touch [engine deviation, W6-01-01 / W6-01-06]: a finger has no cursor
    // preview, so the generous selection box (kHitHalfW x kHitUp) only claims
    // a tap that lands on nothing better. Outside a unit's BODY (the
    // original's hotspot ellipse, unit_body_hit):
    //   * a tap on a building's drawn art picks the building;
    //   * with own units selected, a tap beside an own unit is a ground tap
    //     (the order), not a re-select -- crowds have no gaps otherwise.
    // Enemy units keep the generous box (attack orders stay easy to give).
    if (ev_touch_ && idx >= 0 && !unit_body_hit(idx, px, py)) {
        if (b_art >= 0) idx = -1;
        else if (units_sel && !shift && world_.controllable(us[idx].house)) idx = -1;
    }

    if (!units_sel) {
        if (idx >= 0 && world_.controllable(us[idx].house)) return shift ? Action::ToggleSelect : Action::Select;
        if (b >= 0 && world_.buildings()[b].house == world_.order_issuer()) {
            if (selected_building_valid() && b == selected_building_ &&
                is_unit_factory_type(world_.buildings()[b].type))
                return Action::SetPrimary;
            return Action::SelectBuilding;
        }
        if (selected_building_valid() &&
            is_unit_factory_type(world_.buildings()[selected_building_].type))
            return Action::Rally;
        // W7-02-10: a selected own firing garrison attacks like a defence.
        if (selected_building_valid() && world_.garrison_firing(selected_building_)) {
            const int sb_house = world_.buildings()[selected_building_].house;
            if (idx >= 0 && !world_.are_allied(sb_house, us[idx].house)) return Action::Attack;
            if (b >= 0 && b != selected_building_ && !world_.are_allied(sb_house, world_.buildings()[b].house))
                return Action::Attack;
        }
        return Action::None;   // [RE: FUN_004AB8C0] nothing selected + ground = no action
    }
    // w7fix-combat: the selection's friendly-attack abilities.
    auto sel_any = [&](auto pred) {
        for (int si : sel_.ids()) {
            if (si < 0 || si >= static_cast<int>(us.size())) continue;
            const Unit& su = us[si];
            if (world_.controllable(su.house) && su.alive && su.die_time < 0.0 && su.cargo_of < 0 && pred(su))
                return true;
        }
        return false;
    };
    auto ivan_sel = [&] {
        return sel_any([](const Unit& su) { return su.abil.fire_kind == AbilityKind::IvanBomb; });
    };
    // W7-02-11 [RE: InfantryClass::What_Action 0x51E89E]: an Ivan over an
    // object that already carries a bomb gets NoIvanBomb (no order).
    if (idx >= 0 && !sel_.has(idx) && us[idx].bomb_frame >= 0 && ivan_sel()) return Action::NoMove;
    // W7-07-12 [RE: YR mission 29 Attack Move, AttackMoveNav cursor; the
    // modifier is SUSPECTED Ctrl+Shift]: Ctrl+Shift on the ground is an
    // attack-move (the move cursor).
    if (ctrl && shift && idx < 0 && b < 0) return cell_walkable(cx, cy) ? Action::Move : Action::NoMove;
    // Force-fire (Ctrl) on any object that is not part of the selection.
    // W7-07-13 / W7-02-01 [RE: What_Action FUN_006FF380 -- with the force-fire
    // key down any object other than the unit itself is ACTION_ATTACK,
    // whatever its owner]: own and allied objects too.
    if (ctrl) {
        if (idx >= 0 && !sel_.has(idx)) return Action::Attack;
        if (b >= 0) return Action::Attack;
        if (idx < 0 && b < 0) return Action::Attack;   // G16: force-fire the ground cell
    }
    // W7-02-01 [RE: What_Action 0x6FF865, TechnoType +0x6C1
    // AttackCursorOnFriendlies]: the Ivan's bomb cursor on own / allied
    // objects without the force-fire key.
    if (idx >= 0 && !sel_.has(idx) && !world_.is_enemy(world_.order_issuer(), us[idx].house) &&
        sel_any([](const Unit& su) { return su.tg.t.attack_cursor_on_friendlies; }))
        return Action::Attack;
    if (idx < 0 && b >= 0 && !world_.is_enemy(world_.order_issuer(), world_.buildings()[b].house)) {
        const Building& FB = world_.buildings()[b];
        if (FB.bomb_frame < 0 && sel_any([](const Unit& su) { return su.tg.t.attack_cursor_on_friendlies; }))
            return Action::Attack;
        // W7-02-02 [RE: SelectWeapon FUN_006F2890 @0x6F2B7F -- an allied
        // Overpowerable building and an ElectricAssault secondary]: Tesla
        // Troopers ordered onto an own / allied Tesla Coil charge it.
        const auto& bt = world_.btypes();
        if (FB.type >= 0 && FB.type < static_cast<int>(bt.size()) && bt[FB.type].w5.overpowerable &&
            world_.allied_with(world_.order_issuer(), FB.house) &&
            sel_any([](const Unit& su) { return su.w5.p.assault; }))
            return Action::Attack;
    }
    if (alt) return cell_walkable(cx, cy) ? Action::Move : Action::NoMove;   // force-move
    if (idx >= 0) {
        const Unit& u = us[idx];
        // Wave-5 (R-046): a lone engineer on a bombed unit -> disarm.
        if (u.bomb_frame >= 0 && !sel_.has(idx) && first_selected_role(world_, sel_, false) >= 0 &&
            !world_.is_enemy(world_.order_issuer(), u.house))
            return Action::Capture;
        // Wave-5 (R-052): an ALLIED transport takes the selected infantry.
        // W7-01-30 / W7-04-31 [RE: 0x51E17C -> ReceiveMessage(0xF) refuses ->
        // NoEnter 0x1F]: only when one of them may actually board.
        if (!world_.controllable(u.house) && world_.are_allied(world_.order_issuer(), u.house) && u.passengers_max > 0) {
            for (int si : sel_.ids()) {
                if (si >= static_cast<int>(us.size())) break;
                const Unit& su = us[si];
                if (world_.controllable(su.house) && su.alive && su.die_time < 0.0 && su.passengers_max <= 0)
                    return can_board(world_, idx, sel_) ? Action::Enter : Action::NoMove;
            }
        }
        if (world_.controllable(u.house)) {
            if (shift) return Action::ToggleSelect;
            // W6-01-06 [engine deviation, touch]: with two or more units
            // selected, a tap on a unit OF THE SELECTION is an order to its
            // cell (the army moves there), not a re-select that drops the
            // army. A deliberate select is a tap on an own unit that is NOT
            // selected; a deploy needs a lone selection (or DPLY).
            if (ev_touch_ && sel_.has(idx)) {
                int nsel = 0;
                for (int si : sel_.ids()) {
                    if (si >= static_cast<int>(us.size())) break;
                    const Unit& su = us[si];
                    if (world_.controllable(su.house) && su.alive && su.die_time < 0.0) ++nsel;
                }
                if (nsel >= 2) return cell_walkable(cx, cy) ? Action::Move : Action::NoMove;
            }
            // ACTION_SELF: clicking the selected unit itself deploys / unloads it.
            if (sel_.has(idx)) {
                const bool can = (!u.cargo.empty()) || u.abil.deployer || u.deployed ||
                                 (deploy_check_ && deploy_check_(u));
                if (can) return Action::Deploy;
            }
            if (u.passengers_max > 0) {
                for (int si : sel_.ids()) {
                    if (si >= static_cast<int>(us.size())) break;
                    const Unit& su = us[si];
                    if (!world_.controllable(su.house) || !su.alive || su.die_time >= 0.0 ||
                        si == idx || su.passengers_max > 0)
                        continue;
                    return can_board(world_, idx, sel_) ? Action::Enter : Action::NoMove;   // W7-01-30
                }
            }
            return Action::Select;
        }
        if (world_.is_enemy(world_.order_issuer(), u.house)) {
            // W7-02-11 [RE: InfantryClass::What_Action 0x51E89E]: an Ivan over
            // an already-bombed object gets NoIvanBomb (0x36), not Attack.
            if (u.bomb_frame >= 0) {
                bool all_ivan = false;
                for (int si : sel_.ids()) {
                    if (si >= static_cast<int>(us.size())) break;
                    const Unit& su = us[si];
                    if (!world_.controllable(su.house) || !su.alive || su.die_time >= 0.0) continue;
                    if (su.abil.fire_kind != AbilityKind::IvanBomb) { all_ivan = false; break; }
                    all_ivan = true;
                }
                if (all_ivan) return Action::NoMove;
            }
            return Action::Attack;
        }
        // An ally / neutral unit: the click resolves to its ground cell.
    }
    bool harv = false;
    for (int si : sel_.ids()) {
        if (si >= static_cast<int>(us.size())) break;
        const Unit& su = us[si];
        if (su.is_harvester && world_.controllable(su.house) && su.alive && su.die_time < 0.0) {
            harv = true;
            break;
        }
    }
    if (harv && world_.ore().has_ore(cx, cy)) return Action::Harvest;
    if (b >= 0) {
        const Building& B = world_.buildings()[b];
        if (harv && B.house == world_.order_issuer() && is_refinery_type(B.type)) return Action::Dock;
        // G16 (agent G) [RE: world-depth.md §A2]: selected Occupier infantry on
        // a building they may garrison (CanBeOccupiedBy) -> the enter cursor.
        for (int si : sel_.ids())
            if (si < static_cast<int>(us.size()) && world_.controllable(us[si].house) && world_.can_occupy(si, b))
                return Action::Enter;
        // W7-01-14: only a non-allied Spyable building takes a spy.
        const int spy = first_selected_role(world_, sel_, true);
        if (spy >= 0 && world_.spy_can_infiltrate(spy, b)) return Action::Infiltrate;
        // W7-01-16 / W7-08-19 / W7-08-20: the engineer's own rule.
        const int eng = first_selected_role(world_, sel_, false);
        if (eng >= 0) {
            switch (world_.engineer_action(eng, b)) {
                case World::EngineerAct::Defuse:
                case World::EngineerAct::Capture: return Action::Capture;
                case World::EngineerAct::Repair:
                case World::EngineerAct::Hut: return Action::EngineerRepair;
                default: break;
            }
        }
        // W7-08-01 / W7-04-06: damaged own vehicles over an own UnitRepair
        // building -> the enter (dock) cursor.
        if (world_.depot_wants(world_.order_issuer(), sel_.ids(), b)) return Action::Enter;
        if (B.house == world_.order_issuer()) return shift ? Action::None : Action::SelectBuilding;
        if (world_.is_enemy(world_.order_issuer(), B.house)) return Action::Attack;
    }
    // W7-01-05 [RE: InfantryClass::What_Action 0x51F510]: a human's deployed
    // Deployer infantry answers a ground click with NoMove -- per unit, so a
    // mixed selection still moves.
    if (!selection_can_move(world_, sel_)) return Action::NoMove;
    return cell_walkable(cx, cy) ? Action::Move : Action::NoMove;
}

void Input::left_click_original(int px, int py, bool shift, uint32_t t_ms) {
    const Action a = classify(px, py, shift, mod_ctrl_, mod_alt_);
    const int slop = ev_touch_ ? kDblSlopTouch : kDblSlopO;
    const bool is_double = last_click_t_ != 0 &&
                           (t_ms - last_click_t_) <= (ev_touch_ ? kDblMsTouch : kDblMsO) &&
                           std::abs(px - last_click_x_) <= slop &&
                           std::abs(py - last_click_y_) <= slop;
    // W6-01-05 [engine deviation, touch]: a finger's double tap on an own
    // unit selects every on-screen unit of its type and never deploys. The
    // second tap would otherwise land on the now-selected unit (ACTION_SELF
    // = deploy / unload, or a crowd order, W6-01-06). A single tap on the
    // lone selected unit, slower than the double-tap window, still deploys.
    const bool own_tap = [&] {
        if (!ev_touch_) return false;
        const int i = unit_under(px, py);
        return i >= 0 && world_.controllable(world_.units()[i].house) &&
               (a == Action::Select || a == Action::Deploy ||
                ((a == Action::Move || a == Action::NoMove) && unit_body_hit(i, px, py)));
    }();
    if (own_tap && !shift && is_double && touch_last_kind_ == 1) {
        touch_deploy_wait_ = -1.0;   // the first tap's deferred deploy is dropped
        selected_building_ = -1;
        commit_double(px, py);
        last_click_t_ = 0;
        touch_last_kind_ = 0;
        return;
    }
    touch_deploy_wait_ = -1.0;   // any other tap drops a pending touch deploy
    touch_last_kind_ = own_tap ? 1 : 0;
    if (own_tap && a != Action::Select) {   // remember the tap for the double
        last_click_t_ = t_ms ? t_ms : 1;
        last_click_x_ = px;
        last_click_y_ = py;
    }
    switch (a) {
        case Action::None:
            return;
        case Action::Select: {
            selected_building_ = -1;
            if (is_double) {
                commit_double(px, py);
                last_click_t_ = 0;
                touch_last_kind_ = 0;
            } else {
                commit_click(px, py, false);
                last_click_t_ = t_ms ? t_ms : 1;
                last_click_x_ = px;
                last_click_y_ = py;
            }
            return;
        }
        case Action::ToggleSelect:
            selected_building_ = -1;
            commit_click(px, py, true);
            return;
        case Action::SelectBuilding:
        case Action::SetPrimary: {
            const int b = pick_building(px, py, ev_touch_);   // W6-01-01
            if (b < 0) return;
            if (a == Action::SetPrimary || !is_unit_factory_type(world_.buildings()[b].type)) {
                if (a == Action::SetPrimary) {
                    commit_factory_click(px, py);   // re-click the selected factory -> primary
                } else {
                    selected_building_ = b;
                    sel_.clear();
                }
            } else {
                selected_building_ = -1;            // fresh select (commit_factory_click
                commit_factory_click(px, py);       // would prime on a re-click)
            }
            actions_.building_selected = true;
            last_click_t_ = 0;
            return;
        }
        case Action::Attack:
            // w7fix-combat: a friendly target (AttackCursorOnFriendlies, the
            // Tesla charge) takes the force-fire emission too.
            if (mod_ctrl_ || [&] {
                    const int fi = unit_under(px, py);
                    if (fi >= 0) return !sel_.has(fi) && !world_.is_enemy(world_.order_issuer(), world_.units()[fi].house);
                    const int fb = pick_building(px, py, ev_touch_);
                    return fb >= 0 && !world_.is_enemy(world_.order_issuer(), world_.buildings()[fb].house);
                }()) {   // force-fire: target regardless of the is_enemy gate
                const int idx = unit_under(px, py);
                const int actor = lowest_selected();
                const std::vector<int> sel = sel_.ids();
                if (idx >= 0 && !sel_.has(idx)) {
                    emit(make_mega(world_, sel, Mission::Attack, Target::unit(world_, idx), Target{}));
                } else {
                    int cx = 0, cy = 0;
                    resolve_ground_cell(px, py, cx, cy);
                    const int b = pick_building(px, py, ev_touch_);   // W6-01-01
                    if (b >= 0) emit(make_mega(world_, sel, Mission::Attack, Target::building(b), Target{}));
                    else emit(make_mega(world_, sel, Mission::Attack, Target::cell(cx, cy), Target{}));   // G16: ground
                }
                actions_.attack_ordered = true;
                actions_.actor = actor;
                return;
            }
            issue_order(px, py);
            return;
        case Action::Deploy:
            if (ev_touch_) touch_deploy_wait_ = kDblMsTouch / 1000.0;   // W6-01-05 (touch_tick)
            else actions_.deploy_clicked = true;
            return;
        case Action::Move:
        case Action::NoMove:
            if (mod_ctrl_ && shift && a == Action::Move) {   // W7-07-12 attack-move
                int cx = 0, cy = 0;
                resolve_ground_cell(px, py, cx, cy);
                const int actor = lowest_selected();
                emit(make_mega(world_, sel_.ids(), Mission::AttackMove, Target{}, Target::cell(cx, cy)));
                actions_.move_ordered = true;
                actions_.actor = actor;
                return;
            }
            if (mod_alt_) {   // force-move: always a ground move
                int cx = 0, cy = 0;
                resolve_ground_cell(px, py, cx, cy);
                const int actor = lowest_selected();
                emit(make_mega(world_, sel_.ids(), Mission::Move, Target{}, Target::cell(cx, cy)));
                actions_.move_ordered = true;
                actions_.actor = actor;
                return;
            }
            issue_order(px, py);
            return;
        default:
            issue_order(px, py);   // enter / harvest / dock / capture / spy / rally
            return;
    }
}

void Input::deselect_all() {
    sel_.clear();
    selected_building_ = -1;
}

void Input::right_click_cancel() {
    if (!cancel_one_mode()) deselect_all();   // FUN_004AAC40 tail = FUN_0048DBA0
}

void Input::clear_cursor_modes() {
    mode_ = BaseMode::None;
    sw_targeting_ = false;
}

void Input::handle_original(const InputEvent& ev) {
    if (ev.kind == InputEvent::PointerDown || ev.kind == InputEvent::PointerUp ||
        ev.kind == InputEvent::PointerMove || ev.kind == InputEvent::Key) {
        mod_shift_ = ev.shift;
        mod_ctrl_ = ev.ctrl;
        mod_alt_ = ev.alt;
        ev_touch_ = ev.touch;
    }
    const bool any_mode = sw_targeting_ || placement_active() || mode_ != BaseMode::None;
    switch (ev.kind) {
        case InputEvent::Quit:
            quit_ = true;
            return;
        case InputEvent::Pan:
            pan(ev.dx, ev.dy);
            return;
        case InputEvent::PointerMove:
            cur_x_ = ev.x;
            cur_y_ = ev.y;
            // [RE: FUN_004AC220 refuses the band while a mode is armed]
            if (l_down_ && !dragging_ && !any_mode &&
                (std::abs(ev.x - down_x_) > kDragThresholdO ||
                 std::abs(ev.y - down_y_) > kDragThresholdO))
                dragging_ = true;
            if (r_down_) {
                if (!r_drag_ && (std::abs(ev.x - r_down_x_) > kDragThresholdO ||
                                 std::abs(ev.y - r_down_y_) > kDragThresholdO))
                    r_drag_ = true;
                // Right-drag scroll (WM_RBUTTONDOWN path). [RE-FALLBACK: the
                // ScrollMethod-0 pan law FUN_0066A7A0 is not transcribed; the
                // view follows the drag 1:1 (grab-and-drag).]
                if (r_drag_) pan(ev.x - r_last_x_, ev.y - r_last_y_);
                r_last_x_ = ev.x;
                r_last_y_ = ev.y;
            }
            return;
        case InputEvent::PointerDown:
            cur_x_ = ev.x;
            cur_y_ = ev.y;
            if (ev.button == InputEvent::Right) {
                r_down_ = true;
                r_drag_ = false;
                r_down_x_ = r_last_x_ = ev.x;
                r_down_y_ = r_last_y_ = ev.y;
            } else {
                l_down_ = true;
                dragging_ = false;
                down_shift_ = ev.shift;
                down_x_ = ev.x;
                down_y_ = ev.y;
            }
            return;
        case InputEvent::PointerUp:
            if (ev.button == InputEvent::Right) {
                if (ev.touch) { last_click_t_ = 0; touch_last_kind_ = 0; }   // W6-01-05: no double across it
                if (r_down_ && !r_drag_) {
                    if (placement_active()) actions_.place_cancelled = true;
                    // W4-AM-07: a finger HOLD cancels an armed mode but never
                    // deselects (slow taps used to drop the selection).
                    if (ev.hold) (void)cancel_one_mode();
                    else right_click_cancel();
                }
                r_down_ = r_drag_ = false;
                return;
            }
            if (!l_down_) return;
            if (sw_targeting_) {
                if (!dragging_) {
                    int cx = 0, cy = 0;
                    resolve_ground_cell(ev.x, ev.y, cx, cy);
                    actions_.sw_fire_confirmed = true;
                    actions_.sw_cx = cx;
                    actions_.sw_cy = cy;
                    sw_targeting_ = false;
                }
            } else if (placement_active()) {
                if (!dragging_) {
                    int cx = 0, cy = 0;
                    resolve_ground_cell(ev.x, ev.y, cx, cy);
                    actions_.place_confirmed = true;
                    actions_.place_cx = cx - place_fw_ / 2;
                    actions_.place_cy = cy - place_fh_ / 2;
                }
            } else if (mode_ != BaseMode::None) {
                const int bi = building_under(ev.x, ev.y);
                if (!dragging_ && bi >= 0 && world_.buildings()[bi].house == world_.order_issuer()) {
                    if (mode_ == BaseMode::Repair) {
                        actions_.repair_clicked = true;
                        actions_.repair_building = bi;
                    } else if (mode_ == BaseMode::PowerToggle) {   // W5-E2
                        actions_.power_clicked = true;
                        actions_.power_building = bi;
                    } else {
                        actions_.sell_clicked = true;
                        actions_.sell_building = bi;
                    }
                }
            } else if (dragging_) {
                // W4-AM-01: an empty touch band box keeps the selection.
                if (!ev.touch || box_has_own_unit()) commit_box(down_shift_);
                if (any_own_selected()) selected_building_ = -1;
            } else {
                left_click_original(ev.x, ev.y, down_shift_, ev.t_ms);
            }
            l_down_ = false;
            dragging_ = false;
            return;
        default:
            return;
    }
}

// --- G-04 hotkey commands ----------------------------------------------------

void Input::stop_selected() {
    // Gap fix C: the World owns Stop/Guard/Scatter (engine/unit_orders.cpp).
    // NET L1 [RE: StopObject FUN_0072FF70 queues EVENT_IDLE per object]: one
    // IDLE command with the selection as Whom (its apply marks W4-SQ).
    sel_.prune(world_);
    const std::vector<int> sel = sel_.ids();
    if (!sel.empty()) emit(make_whom(world_, EventType::Idle, sel));
}

void Input::guard_selected() {
    // Area_Guard anchored on the unit itself [RE: FUN_0072FE30 =
    // MegaMission(Area_Guard 0xB, self)]; World side.
    sel_.prune(world_);
    const std::vector<int> sel = sel_.ids();
    if (!sel.empty()) emit(make_mega(world_, sel, Mission::AreaGuard, Target{}, Target{}));
}

void Input::scatter_selected() {
    // World side (unit_orders.cpp): a free 8-ring neighbour drawn from the
    // sync RNG [the original randomises TechnoClass::Scatter's cell;
    // ScatterObject FUN_007300B0 queues EVENT_SCATTER per object].
    sel_.prune(world_);
    const std::vector<int> sel = sel_.ids();
    if (!sel.empty()) emit(make_whom(world_, EventType::Scatter, sel));
}

bool Input::type_select() {
    sel_.prune(world_);
    const auto& us = world_.units();
    std::vector<std::string> types;
    for (int i : sel_.ids()) {
        if (i >= static_cast<int>(us.size())) break;
        const Unit& u = us[i];
        if (world_.controllable(u.house) && u.alive && u.die_time < 0.0 &&
            std::find(types.begin(), types.end(), u.image) == types.end())
            types.push_back(u.image);
    }
    if (types.empty()) return false;
    auto matches = [&](const Unit& u) {
        return world_.controllable(u.house) && u.alive && u.die_time < 0.0 && u.cargo_of < 0 &&
               std::find(types.begin(), types.end(), u.image) != types.end();
    };
    auto on_screen = [&](const Unit& u) {
        int gx = 0, gy = 0;
        unit_ground_px(u, gx, gy);
        return gx >= 0 && gx < view_w_ && gy >= 0 && gy < view_h_;
    };
    // [RE: FUN_00731A20] on screen first; if every on-screen match is already
    // selected, the press extends to the whole map.
    bool all_on_screen_selected = true;
    for (std::size_t i = 0; i < us.size(); ++i)
        if (matches(us[i]) && on_screen(us[i]) && !sel_.has(static_cast<int>(i))) { all_on_screen_selected = false; break; }
    for (std::size_t i = 0; i < us.size(); ++i)
        if (matches(us[i]) && (all_on_screen_selected || on_screen(us[i]))) sel_.set(static_cast<int>(i));
    selected_building_ = -1;
    const int lo = lowest_selected();
    // W7-12-26 [RE: 0x7318A0 saves AllowVoice and forces it to 0]: the
    // select-same-type sweep is SILENT -- no `selected` voice request.
    if (lo >= 0) actions_.actor = lo;
    return lo >= 0;
}

void Input::sync_groups() {
    if (unit_group_.size() < world_.units().size()) unit_group_.resize(world_.units().size(), 0);
}

void Input::group_create(int n) {
    sync_groups();
    sel_.prune(world_);
    const auto& us = world_.units();
    for (std::size_t i = 0; i < us.size(); ++i) {
        if (!world_.controllable(us[i].house)) continue;
        if (unit_group_[i] == n) unit_group_[i] = 0;
        if (sel_.has(static_cast<int>(i))) unit_group_[i] = n;
    }
}

void Input::group_add_select(int n) {
    clear_cursor_modes();
    sync_groups();
    const auto& us = world_.units();
    for (std::size_t i = 0; i < us.size(); ++i)
        if (unit_group_[i] == n && world_.controllable(us[i].house) && us[i].alive && us[i].die_time < 0.0 &&
            us[i].cargo_of < 0)
            sel_.set(static_cast<int>(i));
    const int lo = lowest_selected();
    if (lo >= 0) { selected_building_ = -1; actions_.selected = true; actions_.actor = lo; }
}

void Input::group_select(int n, uint32_t t_ms) {
    clear_cursor_modes();
    sync_groups();
    // [RE: FUN_00730290] the same group again within 800 ms -> TeamCenter.
    if (n == last_group_ && last_group_t_ != 0 && t_ms - last_group_t_ < kGroupRepeatMs) {
        last_group_t_ = 0;
        group_center(n);
        return;
    }
    last_group_ = n;
    last_group_t_ = t_ms ? t_ms : 1;
    bool any = false;
    for (std::size_t i = 0; i < world_.units().size(); ++i)
        if (unit_group_[i] == n && world_.units()[i].alive && world_.units()[i].die_time < 0.0)
            any = true;
    if (!any) return;   // an empty group leaves the selection alone
    sel_.clear();
    selected_building_ = -1;
    group_add_select(n);
}

int Input::group_size(int n) const {
    const auto& us = world_.units();
    int c = 0;
    for (std::size_t i = 0; i < us.size() && i < unit_group_.size(); ++i)
        if (unit_group_[i] == n && world_.controllable(us[i].house) && us[i].alive && us[i].die_time < 0.0)
            ++c;
    return c;
}

void Input::group_center(int n) {
    clear_cursor_modes();
    sync_groups();
    sel_.clear();
    selected_building_ = -1;
    group_add_select(n);
    center_on_selection();
}

void Input::center_on_cell(double cx, double cy, int level) {
    // Screen point of the cell centre is ((cx-cy)*30 + 30 + cam.x, (cx+cy)*15 +
    // 15 - level*kLevelPx + cam.y); put it at the middle of the playfield.
    cam_.x = static_cast<int>(std::lround(view_w_ / 2.0 - (cx - cy) * 30.0 - 30.0));
    cam_.y = static_cast<int>(std::lround(view_h_ / 2.0 - (cx + cy) * 15.0 - 15.0 +
                                          level * static_cast<double>(kLevelPxO)));
    clamp_camera();
}

void Input::set_view_limits(int min_x, int min_y, int max_x, int max_y) {
    // The visible world rect is [-cam.x, -cam.x + view_w) x [-cam.y, -cam.y + view_h).
    if (max_x - min_x >= view_w_) {
        min_cam_x_ = view_w_ - max_x;
        max_cam_x_ = -min_x;
    } else {
        min_cam_x_ = max_cam_x_ = (view_w_ - max_x - min_x) / 2;
    }
    if (max_y - min_y >= view_h_) {
        min_cam_y_ = view_h_ - max_y;
        max_cam_y_ = -min_y;
    } else {
        min_cam_y_ = max_cam_y_ = (view_h_ - max_y - min_y) / 2;
    }
    clamp_camera();
}

void Input::set_viewport(int view_w, int view_h) {
    view_w_ = view_w;
    view_h_ = view_h;
    int mnx, mny, mxx, mxy;   // the same rule as the constructor
    if (terrain_screen_bounds(world_.map(), mnx, mny, mxx, mxy)) {
        const int margin_x = view_w_ / 2, margin_y = view_h_ / 2;
        min_cam_x_ = -margin_x - mxx;
        max_cam_x_ = view_w_ + margin_x - mnx;
        min_cam_y_ = -margin_y - mxy;
        max_cam_y_ = view_h_ + margin_y - mny;
    }
    clamp_camera();
}

void Input::center_on_selection() {
    std::vector<const Unit*> sel;
    for (int i : sel_.ids()) {
        if (i >= static_cast<int>(world_.units().size())) break;
        const Unit& u = world_.units()[i];
        if (u.alive && u.die_time < 0.0) sel.push_back(&u);
    }

    if (sel.empty()) return;
    double mx = 0, my = 0;
    for (const Unit* u : sel) { mx += u->fx; my += u->fy; }
    mx /= sel.size();
    my /= sel.size();
    if (sel.size() > 2) {   // [RE: FUN_004AE1A0] drop the farthest from the mean
        std::size_t far = 0;
        double best = -1;
        for (std::size_t k = 0; k < sel.size(); ++k) {
            const double d = (sel[k]->fx - mx) * (sel[k]->fx - mx) + (sel[k]->fy - my) * (sel[k]->fy - my);
            if (d > best) { best = d; far = k; }
        }
        mx = (mx * sel.size() - sel[far]->fx) / (sel.size() - 1);
        my = (my * sel.size() - sel[far]->fy) / (sel.size() - 1);
    }
    center_on_cell(mx, my);
}

}  // namespace ra2engine
