#pragma once
#include <cstdint>
namespace ra2engine {
struct InputEvent {
    // P7: Key (a key the UI consumes; `key` = one of the kKey* codes below, or 0
    // for a key that only carries a `vk`) and Text (typed UTF-8 in `text`, for
    // the save description edit).
    // Pan (a touch pan gesture; dx/dy in window pixels) is produced only by the
    // touch gesture layer.
    // W2-AR-09/-26 (appended): AppBackground (Android WILLENTERBACKGROUND /
    // TERMINATING -- save now, the process may be killed), AppForeground,
    // AppLowMemory (drop caches), RenderReset (GL context lost: textures gone).
    enum Kind { None, Quit, PointerDown, PointerUp, PointerMove, Key, Text, Pan,
                AppBackground, AppForeground, AppLowMemory, RenderReset,
                Resize /* R-162: the window / panel changed size */,
                Gesture /* touch: a multi-finger command gesture, `gesture` = kGest* */ } kind = None;
    int x = 0, y = 0;                       // pointer position (window pixels)
    enum Button { Left, Right } button = Left;  // valid on PointerDown/PointerUp
    bool shift = false;                     // Shift held at event time
    bool ctrl = false;                      // Ctrl held at event time
    bool alt = false;                       // Alt held at event time
    bool touch = false;                     // produced by a finger, not a mouse
    bool system_back = false;               // W2-AR-14: Key from Android BACK (also Esc)
    uint32_t t_ms = 0;                      // event timestamp (ms) for double-click
    int key = 0;                            // Key: kKey* code (0 = vk-only key)
    // Key: the Windows virtual-key code of the pressed key -- the code space
    // KeyboardMD.ini [Hotkey] uses (A..Z = 65..90, 0..9 = 48..57, F1 = 112,
    // Esc = 27, Space = 32, Home = 36, ...). 0 = unmapped key.
    int vk = 0;
    int dx = 0, dy = 0;                     // Pan: camera delta (window pixels)
    char text[32] = {0};                    // Text: NUL-terminated UTF-8
    // W4-AM-01/07 (touch only): `hold` marks the right click a finger hold
    // produces (it cancels a mode but never deselects); x0/y0 is where a Pan
    // gesture started (a drag that starts on the sidebar / radar is routed there).
    bool hold = false;
    int x0 = 0, y0 = 0;
    int gesture = 0;                        // Gesture: one of kGest* below
};
// Touch command gestures (TouchGestures -> GameScene, which runs the same
// handlers as the KeyboardMD.ini hotkeys). [Engine affordance: the original is
// mouse + keyboard only.]
enum : int { kGestStop = 1 /* two-finger long-press */, kGestScatter /* pinch out */,
             kGestDeploy /* pinch in */, kGestNext /* three-finger swipe right */,
             kGestPrev /* three-finger swipe left */, kGestHome /* two-finger double tap */,
             kGestEvent /* three-finger tap */, kGestFollow /* three-finger long-press */ };
enum : int { kKeyBackspace = 1, kKeyEnter = 2, kKeyUp = 3, kKeyDown = 4, kKeyDelete = 5,
             kKeyF10 = 6, kKeyF1 = 7 /* P10 briefing */, kKeyEscape = 8 /* G-01 */,
             kKeyPerfToggle = 9 /* R-170: three-finger tap / Ctrl+Shift+F, eaten by the driver */ };

// G0: anything that yields InputEvents -- the real Platform, or the --play
// harness's scripted source. poll() returns false when no event is pending.
struct EventSource {
    virtual bool poll(InputEvent& ev) = 0;
    virtual ~EventSource() = default;
};

// Translate a key press (SDL keycode + SDL modifier mask, passed as plain ints
// so this header stays SDL-free) into the InputEvent the Platform emits. Shared
// by Platform::poll and the --play harness's `key` command, so scripted keys go
// through the exact production mapping.
void key_event_from_sdl(int sdl_keycode, int sdl_mod, InputEvent& ev);

class Platform : public EventSource {
public:
    // hidden = true creates an invisible window with the software renderer (the
    // --play harness: deterministic pixels, no visible desktop needed).
    static Platform create(const char* title, int w, int h, bool hidden = false);
    Platform(Platform&&) noexcept; Platform& operator=(Platform&&) noexcept;
    ~Platform() override;
    bool poll(InputEvent& ev) override;
    void begin_frame();
    void present();
    void* renderer() const;   // SDL_Renderer*
    int width() const; int height() const;
    // G-89: physical window size (RA2.INI [Video]) and desktop fullscreen;
    // rendering stays at the logical size given to create().
    // W6-06-12: false = the size did not fit and 1280x720 was used instead.
    bool set_window_size(int w, int h);
    void set_fullscreen(bool on);
    // W2-AR-06: change the LOGICAL canvas (e.g. a wider game view on a 20:9
    // phone); input is remapped to it. No-op when unchanged.
    void set_logical_size(int w, int h);
    // Physical output size in pixels (the window / phone panel).
    void output_size(int& w, int& h) const;
    // W4-AM-07: a finger resting toward a hold (the scene draws a ring).
    bool touch_hold(int& x, int& y, float& progress) const;
private:
    Platform() = default;
    struct Impl; Impl* p_ = nullptr;
};
}
