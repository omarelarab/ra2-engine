// G-70 / G-71: the touch gesture layer (engine/touch_gestures.{hpp,cpp}).
#include "doctest.h"
#include "touch_gestures.hpp"

#include <vector>

using namespace ra2engine;

namespace {
std::vector<InputEvent> drain(TouchGestures& g) {
    std::vector<InputEvent> v;
    InputEvent e;
    while (g.pop(e)) v.push_back(e);
    return v;
}
}  // namespace

TEST_CASE("touch: a tap is ONE left click (no synthetic duplicate)") {
    TouchGestures g;
    g.finger_down(1, 100, 200, 1000);
    g.tick(1100);
    g.finger_up(1, 102, 201, 1150);
    const auto v = drain(g);
    REQUIRE(v.size() == 3);
    CHECK(v[1].kind == InputEvent::PointerDown);
    CHECK(v[1].button == InputEvent::Left);
    CHECK(v[2].kind == InputEvent::PointerUp);
    CHECK(v[1].x == 100);
    CHECK(v[1].touch);
}

TEST_CASE("touch: a hold fires on the lift as a right click marked hold (W4-AM-07)") {
    TouchGestures g;
    g.finger_down(1, 50, 60, 1000);
    g.tick(1200);                        // 200 ms: still a tap
    CHECK_FALSE(g.pending());
    int hx = 0, hy = 0;
    float pr = 0.0f;
    CHECK(g.hold_feedback(1200, hx, hy, pr));   // ring shown after 100 ms
    CHECK(hx == 50);
    CHECK(pr > 0.0f);
    CHECK(pr < 1.0f);
    g.tick(1300);                        // 300 ms (TG-01): held, nothing emitted yet
    CHECK_FALSE(g.pending());
    CHECK(g.hold_feedback(1300, hx, hy, pr));
    CHECK(pr == doctest::Approx(1.0f));
    g.finger_up(1, 50, 60, 1700);
    auto v = drain(g);
    REQUIRE(v.size() == 3);
    CHECK(v[1].button == InputEvent::Right);
    CHECK(v[1].hold);
    CHECK(v[2].button == InputEvent::Right);
    CHECK(v[2].hold);

    TouchGestures s;                     // a 250 ms slow tap is a TAP
    s.finger_down(1, 50, 60, 1000);
    s.tick(1250);
    s.finger_up(1, 50, 60, 1250);
    v = drain(s);
    REQUIRE(v.size() == 3);
    CHECK(v[1].button == InputEvent::Left);
    CHECK_FALSE(v[1].hold);
}

TEST_CASE("touch: one-finger drag PANS the map (W4-AM-01)") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_move(1, 105, 105, 1010);   // within slop: nothing yet
    CHECK_FALSE(g.pending());
    g.finger_move(1, 160, 140, 1020);
    g.finger_move(1, 200, 180, 1030);
    g.tick(5000);                        // no hold once panning
    g.finger_up(1, 200, 180, 1040);
    const auto v = drain(g);
    REQUIRE(v.size() == 2);
    int dx = 0, dy = 0;
    for (const auto& e : v) {
        CHECK(e.kind == InputEvent::Pan);
        CHECK(e.x0 == 100);
        CHECK(e.y0 == 100);
        dx += e.dx;
        dy += e.dy;
    }
    CHECK(dx == 100);   // the map follows the finger from the touch-down point
    CHECK(dy == 80);
}

TEST_CASE("touch: BOX mode -- a one-finger drag is a band box at once (W6-01-11)") {
    TouchGestures::set_box_mode(true);
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_move(1, 105, 105, 1010);   // within slop: nothing yet
    CHECK_FALSE(g.pending());
    g.finger_move(1, 160, 140, 1020);
    g.finger_move(1, 200, 180, 1030);
    g.finger_up(1, 200, 180, 1040);
    TouchGestures::set_box_mode(false);
    const auto v = drain(g);
    REQUIRE(v.size() == 5);
    CHECK(v[1].kind == InputEvent::PointerDown);
    CHECK(v[1].button == InputEvent::Left);
    CHECK(v[1].x == 100);
    CHECK(v[1].y == 100);
    CHECK(v[3].kind == InputEvent::PointerMove);
    CHECK(v[3].x == 200);
    CHECK(v[4].kind == InputEvent::PointerUp);
    for (const auto& e : v) CHECK(e.kind != InputEvent::Pan);
    // two fingers still scroll in BOX mode
    TouchGestures::set_box_mode(true);
    TouchGestures t;
    t.finger_down(1, 100, 100, 1000);
    t.finger_down(2, 200, 100, 1010);
    t.finger_move(1, 130, 120, 1020);
    TouchGestures::set_box_mode(false);
    const auto w = drain(t);
    REQUIRE_FALSE(w.empty());
    CHECK(w[0].kind == InputEvent::Pan);
}

TEST_CASE("touch: hold then drag is a left band box from the hold point") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.tick(1700);
    CHECK_FALSE(g.pending());
    g.finger_move(1, 160, 140, 1750);
    g.finger_move(1, 200, 180, 1800);
    g.finger_up(1, 200, 180, 1850);
    const auto v = drain(g);
    REQUIRE(v.size() == 5);
    CHECK(v[1].kind == InputEvent::PointerDown);
    CHECK(v[1].button == InputEvent::Left);
    CHECK(v[1].x == 100);
    CHECK(v[3].kind == InputEvent::PointerMove);
    CHECK(v[4].kind == InputEvent::PointerUp);
    CHECK(v[4].x == 200);
}

TEST_CASE("touch: two-finger drag pans; a two-finger tap is a right click") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_down(2, 200, 100, 1010);
    g.finger_move(1, 130, 120, 1020);
    g.finger_move(2, 230, 120, 1030);
    auto v = drain(g);
    REQUIRE(v.size() == 2);
    CHECK(v[0].kind == InputEvent::Pan);
    int dx = 0, dy = 0;
    for (const auto& e : v) { dx += e.dx; dy += e.dy; }
    CHECK(dx == 30);
    CHECK(dy == 20);
    g.finger_up(1, 130, 120, 1100);
    g.finger_up(2, 230, 120, 1110);
    CHECK_FALSE(g.pending());   // it panned: no right click
    g.tick(9000);
    CHECK_FALSE(g.pending());

    TouchGestures h;
    h.finger_down(1, 100, 100, 1000);
    h.finger_down(2, 150, 100, 1020);
    h.finger_up(2, 150, 100, 1100);
    h.finger_up(1, 100, 100, 1120);
    v = drain(h);
    REQUIRE(v.size() == 3);
    CHECK(v[1].button == InputEvent::Right);
}

TEST_CASE("touch: a system-cancelled gesture never clicks (W2-AR-18)") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_cancel(1, 100, 100, 1050);   // back-swipe took the touch
    g.tick(9000);
    CHECK_FALSE(g.pending());

    TouchGestures h;   // two-finger tap cancelled: no right click
    h.finger_down(1, 100, 100, 1000);
    h.finger_down(2, 150, 100, 1020);
    h.finger_cancel(2, 150, 100, 1100);
    h.finger_cancel(1, 100, 100, 1110);
    CHECK_FALSE(h.pending());

    TouchGestures d;   // a band box in progress is released, not left pressed
    d.finger_down(1, 100, 100, 1000);
    d.tick(1700);
    d.finger_move(1, 160, 140, 1750);
    (void)drain(d);
    d.finger_cancel(1, 160, 140, 1100);
    auto v = drain(d);
    REQUIRE(v.size() == 1);
    CHECK(v[0].kind == InputEvent::PointerUp);
}

TEST_CASE("touch: three fingers -- tap = event, rest = follow; four-finger tap = perf (TG-13/14, R-170)") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_down(2, 200, 100, 1020);
    g.finger_down(3, 300, 100, 1040);
    g.finger_up(3, 300, 100, 1200);
    g.finger_up(2, 200, 100, 1210);
    g.finger_up(1, 100, 100, 1220);
    auto v = drain(g);
    REQUIRE(v.size() == 1);   // never a click
    CHECK(v[0].kind == InputEvent::Gesture);
    CHECK(v[0].gesture == kGestEvent);
    CHECK(v[0].touch);

    TouchGestures s;   // a slow three-finger rest = Follow
    s.finger_down(1, 100, 100, 1000);
    s.finger_down(2, 200, 100, 1020);
    s.finger_down(3, 300, 100, 1040);
    s.finger_up(3, 300, 100, 2000);
    s.finger_up(2, 200, 100, 2010);
    s.finger_up(1, 100, 100, 2020);
    v = drain(s);
    REQUIRE(v.size() == 1);
    CHECK(v[0].gesture == kGestFollow);

    TouchGestures f;   // four fingers tapped = the perf overlay key
    f.finger_down(1, 100, 100, 1000);
    f.finger_down(2, 200, 100, 1010);
    f.finger_down(3, 300, 100, 1020);
    f.finger_down(4, 400, 100, 1030);
    f.finger_up(4, 400, 100, 1150);
    f.finger_up(3, 300, 100, 1160);
    f.finger_up(2, 200, 100, 1170);
    f.finger_up(1, 100, 100, 1180);
    v = drain(f);
    REQUIRE(v.size() == 1);
    CHECK(v[0].kind == InputEvent::Key);
    CHECK(v[0].key == kKeyPerfToggle);

    TouchGestures t;   // after it, a normal tap still works
    t.finger_down(1, 100, 100, 1000);
    t.finger_down(2, 200, 100, 1020);
    t.finger_down(3, 300, 100, 1040);
    t.finger_up(1, 100, 100, 1200);
    t.finger_up(2, 200, 100, 1210);
    t.finger_up(3, 300, 100, 1220);
    drain(t);
    t.finger_down(7, 50, 60, 3000);
    t.finger_up(7, 50, 60, 3100);
    const auto w = drain(t);
    REQUIRE(w.size() == 3);
    CHECK(w[1].kind == InputEvent::PointerDown);
    CHECK(w[1].x == 50);
}

TEST_CASE("touch: three-finger swipe right / left = next / previous unit (TG-11)") {
    for (int dir : {+1, -1}) {
        TouchGestures g;
        g.finger_down(1, 300, 100, 1000);
        g.finger_down(2, 400, 100, 1010);
        g.finger_down(3, 500, 100, 1020);
        for (int k = 1; k <= 10; ++k) {
            g.finger_move(1, 300 + dir * 12 * k, 102, 1020 + 10 * k);
            g.finger_move(2, 400 + dir * 12 * k, 101, 1020 + 10 * k);
        }
        CHECK_FALSE(g.pending());   // a three-finger swipe never scrolls
        g.finger_up(1, 300 + dir * 120, 102, 1200);
        g.finger_up(2, 400 + dir * 120, 101, 1210);
        g.finger_up(3, 500 + dir * 120, 100, 1220);
        const auto v = drain(g);
        REQUIRE(v.size() == 1);
        CHECK(v[0].gesture == (dir > 0 ? kGestNext : kGestPrev));
    }
}

TEST_CASE("touch: a resting finger makes the second finger's tap / box Shift (TG-02)") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.tick(1300);                        // rested (held)
    g.finger_down(2, 400, 300, 1350);
    g.finger_up(2, 401, 300, 1450);      // a tap with the second finger
    auto v = drain(g);
    REQUIRE(v.size() == 3);
    CHECK(v[1].kind == InputEvent::PointerDown);
    CHECK(v[1].button == InputEvent::Left);
    CHECK(v[1].x == 400);
    CHECK(v[1].shift);
    CHECK(v[2].shift);
    g.finger_down(3, 500, 300, 1600);    // again, now a drag = Shift band box
    g.finger_move(3, 560, 360, 1650);
    g.finger_up(3, 560, 360, 1700);
    v = drain(g);
    REQUIRE(v.size() >= 4);
    CHECK(v[1].kind == InputEvent::PointerDown);
    CHECK(v[1].x == 500);
    CHECK(v.back().kind == InputEvent::PointerUp);
    for (const InputEvent& e : v) CHECK(e.shift);
    g.finger_up(1, 100, 100, 1800);      // the resting finger: no click of its own
    CHECK(drain(g).empty());

    TouchGestures q;                     // a QUICK second finger is still a two-finger tap
    q.finger_down(1, 100, 100, 1000);
    q.finger_down(2, 140, 100, 1050);
    q.finger_up(2, 140, 100, 1150);
    q.finger_up(1, 100, 100, 1160);
    v = drain(q);
    REQUIRE(v.size() == 3);
    CHECK(v[1].button == InputEvent::Right);
    CHECK_FALSE(v[1].shift);
}

TEST_CASE("touch: pinch out = scatter, pinch in = deploy, no scrolling (TG-08/09)") {
    for (int dir : {+1, -1}) {
        TouchGestures g;
        const int a0 = dir > 0 ? 280 : 200, b0 = dir > 0 ? 320 : 400;
        g.finger_down(1, a0, 200, 1000);
        g.finger_down(2, b0, 200, 1010);
        for (int k = 1; k <= 10; ++k) {
            g.finger_move(1, a0 - dir * 8 * k, 200, 1010 + 10 * k);
            g.finger_move(2, b0 + dir * 8 * k, 200, 1010 + 10 * k);
        }
        g.finger_up(1, a0 - dir * 80, 200, 1200);
        g.finger_up(2, b0 + dir * 80, 200, 1210);
        const auto v = drain(g);
        REQUIRE_FALSE(v.empty());
        CHECK(v.back().kind == InputEvent::Gesture);
        CHECK(v.back().gesture == (dir > 0 ? kGestScatter : kGestDeploy));
        for (const InputEvent& e : v) CHECK(e.kind != InputEvent::PointerDown);
    }
}

TEST_CASE("touch: two-finger hold = stop; two-finger double tap = home (TG-06/12)") {
    TouchGestures g;
    g.finger_down(1, 100, 100, 1000);
    g.finger_down(2, 160, 100, 1020);
    g.tick(1200);
    CHECK_FALSE(g.pending());
    g.tick(1330);                        // 310 ms with two fingers resting
    auto v = drain(g);
    REQUIRE(v.size() == 1);
    CHECK(v[0].gesture == kGestStop);
    g.finger_up(1, 100, 100, 1500);
    g.finger_up(2, 160, 100, 1510);
    CHECK(drain(g).empty());             // no right click after the stop

    TouchGestures d;
    d.finger_down(1, 100, 100, 1000);
    d.finger_down(2, 160, 100, 1010);
    d.finger_up(1, 100, 100, 1100);
    d.finger_up(2, 160, 100, 1110);
    v = drain(d);
    REQUIRE(v.size() == 3);              // the first two-finger tap: a right click
    CHECK(v[1].button == InputEvent::Right);
    d.finger_down(1, 102, 100, 1250);
    d.finger_down(2, 162, 100, 1260);
    d.finger_up(1, 102, 100, 1330);
    d.finger_up(2, 162, 100, 1340);
    v = drain(d);
    REQUIRE(v.size() == 1);              // the second: home, no second right click
    CHECK(v[0].gesture == kGestHome);
}
