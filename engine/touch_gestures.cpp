#include "touch_gestures.hpp"

#include <algorithm>
#include <cstdlib>

namespace ra2engine {

void TouchGestures::emit(InputEvent::Kind k, int x, int y, bool right, uint32_t t, bool shift) {
    InputEvent e;
    e.kind = k;
    e.x = x;
    e.y = y;
    e.button = right ? InputEvent::Right : InputEvent::Left;
    e.t_ms = t;
    e.touch = true;
    e.shift = shift;
    out_.push_back(e);
}

void TouchGestures::emit_click(int x, int y, bool right, uint32_t t, bool hold, bool shift) {
    emit(InputEvent::PointerMove, x, y, false, t, shift);
    emit(InputEvent::PointerDown, x, y, right, t, shift);
    emit(InputEvent::PointerUp, x, y, right, t, shift);
    if (hold) {   // W4-AM-07: a hold-release right click never deselects
        out_[out_.size() - 2].hold = true;
        out_.back().hold = true;
    }
}

void TouchGestures::emit_pan(int dx, int dy, int x, int y, uint32_t t) {
    if (!dx && !dy) return;
    InputEvent e;
    e.kind = InputEvent::Pan;
    e.dx = dx;   // the map follows the finger(s)
    e.dy = dy;
    e.x = x;
    e.y = y;
    e.x0 = x0_;   // where the gesture started (sidebar / radar routing)
    e.y0 = y0_;
    e.t_ms = t;
    e.touch = true;
    out_.push_back(e);
}

void TouchGestures::emit_gesture(int g, int x, int y, uint32_t t) {
    InputEvent e;
    e.kind = InputEvent::Gesture;
    e.gesture = g;
    e.x = x;
    e.y = y;
    e.t_ms = t;
    e.touch = true;
    out_.push_back(e);
}

void TouchGestures::emit_key(int key, uint32_t t) {
    InputEvent e;
    e.kind = InputEvent::Key;
    e.key = key;
    e.t_ms = t;
    e.touch = true;
    out_.push_back(e);
}

int TouchGestures::spread() const {
    return std::abs(cx_ - x1_) + std::abs(cy_ - y1_);
}

void TouchGestures::lifted() {
    if (down_ > 0) --down_;
    if (st_ == State::WaitUp && down_ == 0) st_ = State::Idle;
}

void TouchGestures::finger_down(int64_t id, int x, int y, uint32_t t) {
    ++down_;
    switch (st_) {
        case State::Idle:
            st_ = State::Pending;
            down_ = 1;
            id0_ = id;
            x0_ = cx_ = px_ = x;
            y0_ = cy_ = py_ = y;
            t0_ = t;
            return;
        case State::Pending:
        case State::Held:
            // TG-02: the first finger has rested -> it is a Shift modifier and
            // this finger taps / drags. (A quick second finger is a two-finger
            // gesture, below.)
            if (st_ == State::Held || t - t0_ >= cfg_.mod_rest_ms) {
                st_ = State::Modifier;
                id1_ = id;
                mx0_ = x1_ = x;
                my0_ = y1_ = y;
                mod_drag_ = false;
                return;
            }
            [[fallthrough]];
        case State::Panning:
            // A second finger: a two-finger gesture (a one-finger pan simply
            // continues as a two-finger pan).
            st_ = State::Multi;
            id1_ = id;
            x1_ = x;
            y1_ = y;
            mid_x_ = (cx_ + x1_) / 2;
            mid_y_ = (cy_ + y1_) / 2;
            travel_ = sdx_ = sdy_ = 0;
            t2_ = t;
            fingers_ = 2;
            d0_ = spread();
            pinch_ = 0;
            fired_ = false;
            return;
        case State::Multi:
            fingers_ = std::max(fingers_, down_);
            if (fingers_ == 3) { t2_ = t; travel_ = sdx_ = sdy_ = 0; }   // the 3-finger gesture starts now
            return;
        case State::Modifier:
            if (id1_ < 0) {   // another tap / drag while the first finger still rests
                id1_ = id;
                mx0_ = x1_ = x;
                my0_ = y1_ = y;
                mod_drag_ = false;
            }
            return;
        default:
            return;   // extra fingers during a band box / wait are ignored
    }
}

void TouchGestures::finger_move(int64_t id, int x, int y, uint32_t t) {
    if (st_ == State::Multi) {
        if (id == id0_) { cx_ = x; cy_ = y; }
        else if (id == id1_) { x1_ = x; y1_ = y; }
        else return;
        const int mx = (cx_ + x1_) / 2, my = (cy_ + y1_) / 2;
        const int dx = mx - mid_x_, dy = my - mid_y_;
        mid_x_ = mx;
        mid_y_ = my;
        travel_ += std::abs(dx) + std::abs(dy);
        sdx_ += dx;
        sdy_ += dy;
        if (fingers_ >= 3 || fired_) return;   // three-finger swipes / a fired hold do not scroll
        if (pinch_ == 0) {
            // TG-08/09: the spread changed a lot while the centre stayed put.
            const int dd = spread() - d0_;
            const int need = std::max(cfg_.slop_px * 3, d0_ / 4);
            if (std::abs(dd) > need && travel_ < std::abs(dd)) pinch_ = dd > 0 ? 1 : -1;
        }
        if (pinch_ == 0) emit_pan(dx, dy, mx, my, t);
        return;
    }
    if (st_ == State::Modifier) {
        if (id != id1_) return;   // the resting finger may wander a little
        x1_ = x;
        y1_ = y;
        const bool beyond = std::abs(x - mx0_) > cfg_.slop_px || std::abs(y - my0_) > cfg_.slop_px;
        if (!mod_drag_ && beyond) {   // Shift band box from the second finger's start
            mod_drag_ = true;
            emit(InputEvent::PointerMove, mx0_, my0_, false, t, true);
            emit(InputEvent::PointerDown, mx0_, my0_, false, t, true);
        }
        if (mod_drag_) emit(InputEvent::PointerMove, x, y, false, t, true);
        return;
    }
    if (id != id0_) return;
    cx_ = x;
    cy_ = y;
    const bool beyond = std::abs(x - x0_) > cfg_.slop_px || std::abs(y - y0_) > cfg_.slop_px;
    if (st_ == State::Pending && beyond && box_mode_) {
        st_ = State::BandDrag;   // W6-01-11: BOX mode -- the drag is a band box
        emit(InputEvent::PointerMove, x0_, y0_, false, t0_);
        emit(InputEvent::PointerDown, x0_, y0_, false, t0_);
        emit(InputEvent::PointerMove, x, y, false, t);
        return;
    }
    if (st_ == State::Pending && beyond) {
        st_ = State::Panning;   // W4-AM-01: one-finger drag scrolls
        emit_pan(x - px_, y - py_, x, y, t);
        px_ = x;
        py_ = y;
        return;
    }
    if (st_ == State::Panning) {
        emit_pan(x - px_, y - py_, x, y, t);
        px_ = x;
        py_ = y;
        return;
    }
    if (st_ == State::Held && beyond) {
        st_ = State::BandDrag;   // hold-then-drag: band box from the hold point
        emit(InputEvent::PointerMove, x0_, y0_, false, t0_);
        emit(InputEvent::PointerDown, x0_, y0_, false, t0_);
    }
    if (st_ == State::BandDrag) emit(InputEvent::PointerMove, x, y, false, t);
}

void TouchGestures::finger_cancel(int64_t id, int x, int y, uint32_t t) {
    switch (st_) {
        case State::Pending:
        case State::Panning:
        case State::Held:
            if (id == id0_) st_ = State::WaitUp;
            break;
        case State::BandDrag:
            if (id == id0_) {
                emit(InputEvent::PointerUp, x, y, false, t);
                st_ = State::WaitUp;
            }
            break;
        case State::Modifier:
            if (mod_drag_) emit(InputEvent::PointerUp, x1_, y1_, false, t, true);
            st_ = State::WaitUp;
            break;
        case State::Multi:
            st_ = State::WaitUp;   // no click, no gesture
            break;
        default:
            break;
    }
    lifted();
}

void TouchGestures::end_multi(uint32_t t) {
    const int x = mid_x_, y = mid_y_;
    const uint32_t dt = t - t2_;
    if (fingers_ >= 4) {
        // R-170: a four-finger tap toggles the perf overlay.
        if (travel_ <= cfg_.slop_px * 2 && dt <= cfg_.three_finger_tap_ms) emit_key(kKeyPerfToggle, t);
    } else if (fingers_ == 3) {
        if (std::abs(sdx_) >= cfg_.swipe_px && std::abs(sdx_) > 2 * std::abs(sdy_))
            emit_gesture(sdx_ > 0 ? kGestNext : kGestPrev, x, y, t);            // TG-11
        else if (travel_ <= cfg_.slop_px * 2 && dt <= cfg_.three_finger_tap_ms)
            emit_gesture(kGestEvent, x, y, t);                                  // TG-13
        else if (travel_ <= cfg_.slop_px * 2 && dt >= cfg_.long_press_ms)
            emit_gesture(kGestFollow, x, y, t);                                 // TG-14
    } else if (!fired_) {
        if (pinch_ != 0) {
            emit_gesture(pinch_ > 0 ? kGestScatter : kGestDeploy, x, y, t);     // TG-08 / TG-09
        } else if (travel_ <= cfg_.slop_px && dt <= cfg_.two_finger_tap_ms) {
            if (last_two_tap_ && t - last_two_tap_ <= cfg_.double_tap_ms) {
                emit_gesture(kGestHome, x, y, t);                               // TG-12
                last_two_tap_ = 0;
            } else {
                emit_click(x0_, y0_, true, t);   // two-finger TAP = right click at the first finger
                last_two_tap_ = t ? t : 1;
            }
        }
    }
    st_ = State::WaitUp;   // the other fingers' lifts are swallowed
}

void TouchGestures::finger_up(int64_t id, int x, int y, uint32_t t) {
    switch (st_) {
        case State::Pending:
            if (id == id0_) {
                emit_click(x0_, y0_, false, t);   // tap
                st_ = State::WaitUp;
            }
            break;
        case State::Panning:
            if (id == id0_) st_ = State::WaitUp;
            break;
        case State::Held:
            if (id == id0_) {
                emit_click(x0_, y0_, true, t, /*hold=*/true);   // hold-release = right click
                st_ = State::WaitUp;
            }
            break;
        case State::BandDrag:
            if (id == id0_) {
                emit(InputEvent::PointerUp, x, y, false, t);
                st_ = State::WaitUp;
            }
            break;
        case State::Modifier:
            if (id == id1_) {   // TG-02: the second finger's tap / box carries Shift
                if (mod_drag_) emit(InputEvent::PointerUp, x, y, false, t, true);
                else emit_click(mx0_, my0_, false, t, false, /*shift=*/true);
                id1_ = -1;
                mod_drag_ = false;
            } else if (id == id0_) {   // the resting finger lifts: done, no click
                if (id1_ >= 0 && mod_drag_) emit(InputEvent::PointerUp, x1_, y1_, false, t, true);
                st_ = State::WaitUp;
            }
            break;
        case State::Multi:
            end_multi(t);
            break;
        default:
            break;
    }
    lifted();
}

void TouchGestures::tick(uint32_t now) {
    if (st_ == State::Pending && now - t0_ >= cfg_.long_press_ms) st_ = State::Held;
    // TG-06: two fingers resting still -> Stop, at the moment the hold fires.
    if (st_ == State::Multi && fingers_ == 2 && !fired_ && pinch_ == 0 &&
        travel_ <= cfg_.slop_px && std::abs(spread() - d0_) <= cfg_.slop_px * 2 &&
        now - t2_ >= cfg_.long_press_ms) {
        fired_ = true;
        emit_gesture(kGestStop, mid_x_, mid_y_, now);
    }
}

bool TouchGestures::hold_feedback(uint32_t now, int& x, int& y, float& progress) const {
    if (st_ == State::Held) {
        x = x0_;
        y = y0_;
        progress = 1.0f;
        return true;
    }
    if (st_ != State::Pending || now - t0_ < cfg_.feedback_delay_ms) return false;
    x = x0_;
    y = y0_;
    const float span = static_cast<float>(cfg_.long_press_ms - cfg_.feedback_delay_ms);
    progress = span > 0.0f ? static_cast<float>(now - t0_ - cfg_.feedback_delay_ms) / span : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    return true;
}

}  // namespace ra2engine
