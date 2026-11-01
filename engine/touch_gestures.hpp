#pragma once
// G-70 / G-71 / W4-AM-01: touch gesture layer (Android / iOS). Pure C++17, no
// SDL -- the Platform feeds it finger events and drains the InputEvents it
// produces.
//
// The original is a mouse game; its scheme (left = select AND order, right =
// cancel/deselect, right-drag = scroll; see input_original.cpp) maps onto
// fingers like this [RE-FALLBACK: no original touch mapping exists; the slop
// is Android's touch slop (8 dp ~ 12 logical px); the hold is 300 ms (TG-01:
// was 450 ms; a hold no longer competes with a slow tap since W4-AM-07)]:
//   tap                          -> left click (select / order / sidebar / menu)
//   one-finger drag              -> Pan (the map follows the finger)
//   hold, then drag              -> left band box from the hold point
//   hold, then lift              -> right click marked `hold`. GameScene turns
//                                   it into FORCE FIRE (on a target) or GUARD
//                                   (on a selected unit) while units are
//                                   selected, else it cancels the armed mode;
//                                   it never deselects (W4-AM-07).
//   TG-02 rest one finger (>= mod_rest_ms, still), then tap / drag with a
//         second one             -> Shift click / Shift band box at the second
//                                   finger (add to the selection); repeatable
//                                   while the first finger rests.
//   two-finger drag              -> Pan
//   two-finger tap               -> right click (cancel a mode, else deselect)
//   TG-12 two-finger double tap  -> Gesture kGestHome (the second tap only)
//   TG-06 two-finger long-press  -> Gesture kGestStop (fires at the hold)
//   TG-08 pinch out              -> Gesture kGestScatter
//   TG-09 pinch in               -> Gesture kGestDeploy
//   TG-11 three-finger swipe     -> Gesture kGestNext (right) / kGestPrev (left)
//   TG-13 three-finger tap       -> Gesture kGestEvent (CenterOnRadarEvent)
//   TG-14 three-finger long-press-> Gesture kGestFollow
//   four-finger tap              -> Key kKeyPerfToggle (R-170: the FPS / perf
//                                   overlay; was the three-finger tap)
//   W6-01-11: while BOX mode is on (the sticky [BOX] button of the optional
//   command column, set_box_mode), a one-finger drag draws the band box at
//   once (no hold); two fingers still scroll.
// Every emitted event has touch = true (GameScene then never edge-scrolls).
// hold_feedback() reports the hold progress so the scene can draw a ring at
// the finger before the hold fires.
#include <cstdint>
#include <deque>

#include "platform.hpp"

namespace ra2engine {

class TouchGestures {
public:
    struct Config {
        uint32_t long_press_ms = 300;
        int slop_px = 12;
        uint32_t two_finger_tap_ms = 300;
        uint32_t feedback_delay_ms = 100;   // the hold ring appears after this
        uint32_t three_finger_tap_ms = 500;
        uint32_t mod_rest_ms = 250;         // TG-02: a resting first finger is a modifier
        uint32_t double_tap_ms = 350;       // TG-12: two two-finger taps
        int swipe_px = 72;                  // TG-11: three-finger swipe distance
    };
    TouchGestures() = default;
    explicit TouchGestures(Config c) : cfg_(c) {}
    // W9-04-06: the Platform scales the slop to 8 dp on a phone.
    void set_slop(int px) { if (px > 0) cfg_.slop_px = px; }
    int slop() const { return cfg_.slop_px; }

    void finger_down(int64_t id, int x, int y, uint32_t t_ms);
    void finger_move(int64_t id, int x, int y, uint32_t t_ms);
    void finger_up(int64_t id, int x, int y, uint32_t t_ms);
    // W2-AR-18: the system took the gesture (back-swipe, notification pull):
    // a pending tap / hold / two-finger tap produces NO click; a band box
    // already pressed is released where it is.
    void finger_cancel(int64_t id, int x, int y, uint32_t t_ms);
    // Time passing with no finger event (hold detection).
    void tick(uint32_t now_ms);

    // W4-AM-07: the hold ring. True while one finger rests (within the slop)
    // past feedback_delay_ms; progress 0..1 reaches 1 when the hold fires.
    bool hold_feedback(uint32_t now_ms, int& x, int& y, float& progress) const;

    // W6-01-11: the sticky BOX toggle, shared by every gesture layer (the
    // Platform's and the --play harness's); GameScene's [BOX] button sets it.
    static void set_box_mode(bool on) { box_mode_ = on; }
    static bool box_mode() { return box_mode_; }

    bool pending() const { return !out_.empty(); }
    bool pop(InputEvent& ev) {
        if (out_.empty()) return false;
        ev = out_.front();
        out_.pop_front();
        return true;
    }

private:
    enum class State { Idle, Pending, Panning, Held, BandDrag, WaitUp, Multi, Modifier };
    void emit(InputEvent::Kind k, int x, int y, bool right, uint32_t t, bool shift = false);
    void emit_click(int x, int y, bool right, uint32_t t, bool hold = false, bool shift = false);
    void emit_pan(int dx, int dy, int x, int y, uint32_t t);
    void emit_gesture(int g, int x, int y, uint32_t t);
    void emit_key(int key, uint32_t t);
    void end_multi(uint32_t t);   // the first lift of a 2+ finger gesture decides it
    void lifted();                // one finger less; WaitUp -> Idle at zero
    int spread() const;           // two-finger distance (city block, px)

    Config cfg_;
    State st_ = State::Idle;
    int down_ = 0;                            // fingers currently down
    int64_t id0_ = -1, id1_ = -1;
    int x0_ = 0, y0_ = 0, cx_ = 0, cy_ = 0;   // first finger: start / current
    int px_ = 0, py_ = 0;                     // one-finger pan: last emitted point
    int x1_ = 0, y1_ = 0;                     // second finger current
    uint32_t t0_ = 0;
    // Multi (2+ fingers)
    int mid_x_ = 0, mid_y_ = 0;               // two-finger centroid (last seen)
    int travel_ = 0;                          // centroid travel (px)
    int sdx_ = 0, sdy_ = 0;                   // signed centroid travel (three-finger swipe)
    uint32_t t2_ = 0;                         // multi start time
    int fingers_ = 0;                         // most fingers seen in this gesture
    int d0_ = 0;                              // two-finger start spread
    int pinch_ = 0;                           // +1 out, -1 in, 0 none
    bool fired_ = false;                      // the gesture already emitted (two-finger hold)
    uint32_t last_two_tap_ = 0;               // TG-12: time of the last two-finger tap (0 none)
    // Modifier (TG-02)
    int mx0_ = 0, my0_ = 0;                   // second finger start
    bool mod_drag_ = false;
    std::deque<InputEvent> out_;
    inline static bool box_mode_ = false;
};

}  // namespace ra2engine
