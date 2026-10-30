// hud_text.inc -- W2-UI-12: the ONE in-game HUD font (local.mix/game.fnt, the
// "fonT" Unicode face game.exe loads at FUN_00432290 -> global 0x84E9E8 and
// prints the sidebar status text, tooltips and messages with, FUN_00497A60).
// Included by main.cpp after the shell-font helpers (blit_shp/shade_rect).
//
// [RE: wave2/04 + w2fix_1..3 (D:\ra2-re\w2fix-uiout)]
//   * text colour = the UI colour pair at 0xAC1028, set by FUN_006F45A0 from
//     0xAC0FE8 = RGB(0xA4,0xD2,0xFF) (Allied / side 0) or 0xAC10F4 =
//     RGB(0xFF,0xFF,0x00) (Soviet / side 1) [CONFIRMED: 0x6F1A80 / 0x6F1AA0].
//   * every sidebar text is preceded by FUN_005FEB60(rect, surf, 0, 0xAF): a
//     black fill at alpha 0xAF over the text rect FUN_00497360(x, y, flags, 2, 1)
//     returns (the text box padded 2 px left/right, 1 px top/bottom) [CONFIRMED].
//   * TextPrintType flags: 0x40 FullShadow, 0x100 centre, 0x200 right.
// [RE-FALLBACK: game.fnt's Latin glyphs leave cell rows 0..2 empty; the clone
// trims them so a text anchored at y has its cap line at ~y+1 and the box is
// 13+2 px tall. The FullShadow pattern is drawn as a right/down/diagonal 1 px
// black shadow.]

std::unique_ptr<ra2assets::UFont> g_hud_font_storage;
const ra2assets::UFont* g_hud_font = nullptr;
// CSF TXT_READY / TXT_HOLD (FUN_0069D770), refreshed from ra2.csf by GameScene.
std::string g_txt_ready = "Ready", g_txt_hold = "On Hold";
constexpr int kHudTrimTop = 3;      // empty rows at the top of every Latin cell
constexpr int kHudTextH = 13;       // rows 3..15 of the 16-row cell

// Load local.mix/game.fnt once per process (guarded; absent -> rect font).
void hud_font_load(const ra2assets::MixFile& ra2) {
    if (g_hud_font) return;
    try {
        ra2assets::MixFile local = ra2.open_nested("local.mix");
        if (const ra2assets::MixEntry* e = local.find(std::string("game.fnt"))) {
            g_hud_font_storage = std::make_unique<ra2assets::UFont>(ra2assets::UFont::load(local.read(*e)));
            g_hud_font = g_hud_font_storage.get();
        }
    } catch (const std::exception& ex) {
        std::cerr << "[hud] game.fnt unavailable: " << ex.what() << "\n";
    }
}

// The UI colour of the player's side (sidec02 = Soviet) [CONFIRMED above].
inline SDL_Color hud_ui_color() {
    const bool soviet = g_sidebar_mix && std::string(g_sidebar_mix) == "sidec02.mix";
    return soviet ? SDL_Color{255, 255, 0, 255} : SDL_Color{164, 210, 255, 255};
}

inline int hud_text_width(const std::string& s) {
    if (!g_hud_font) return static_cast<int>(s.size()) * 6;
    return g_hud_font->text_width(s);
}
inline int hud_line_h() { return g_hud_font ? g_hud_font->line_height() - kHudTrimTop : 9; }
inline int hud_text_height(const std::string& s) {
    int lines = 1;
    for (char c : s) lines += (c == '\n');
    return kHudTextH + (lines - 1) * hud_line_h();
}

// One white glyph texture (alpha mask), tinted per draw with SDL colour mod.
// `mag` = the on-screen magnification (text scale x SDL logical->device
// factor). A fractional one gets the sharp-bilinear texture (see
// fnt_glyph_tex in main.cpp): pre-upscaled by the next integer, drawn linear.
SDL_Texture* hud_glyph_tex(ra2engine::TextureCache& tc, uint32_t cp, double mag = 1.0) {
    const int n = static_cast<int>(std::ceil(mag - 1e-3));
    const int k = (n <= 1 || std::fabs(mag - std::lround(mag)) < 1e-3) ? 1 : std::min(n, 8);
    const std::string key = "ug:" + std::to_string(cp) + (k > 1 ? "@x" + std::to_string(k) : std::string());
    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (tex) return tex;
    const int w = g_hud_font->width(cp), h = g_hud_font->rows() - kHudTrimTop;
    if (w <= 0 || h <= 0) return nullptr;
    const int W = w * k, H = h * k;
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (g_hud_font->pixel(cp, x / k, y / k + kHudTrimTop)) {
                uint8_t* p = &rgba[(static_cast<size_t>(y) * W + x) * 4];
                p[0] = p[1] = p[2] = p[3] = 255;
            }
    tex = static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), W, H));
    if (tex && k > 1) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    return tex;
}

enum HudAlign { kHudLeft = 0, kHudCenter = 1, kHudRight = 2 };

// The padded text rect FUN_00497360(x, y, flags, 2, 1) returns.
SDL_Rect hud_text_rect(const std::string& s, int x, int y, int align) {
    const int w = hud_text_width(s), h = hud_text_height(s);
    const int left = align == kHudCenter ? x - w / 2 : align == kHudRight ? x - w : x;
    return SDL_Rect{left - 2, y - 1, w + 4, h + 2};
}

// Draw `s` (UTF-8, '\n' = new line; each line aligned on its own) at anchor
// (x, y). `backing` = the 0xAF black box; `shadow` = FullShadow.
// The SDL logical->device factor (1 when no logical size is set).
inline double hud_render_scale(SDL_Renderer* ren) {
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(ren, &sx, &sy);
    return std::max(sx, sy);
}

void hud_text(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& s, int x, int y,
              SDL_Color col, int align = kHudLeft, bool backing = false, bool shadow = true,
              double scale = 1.0) {
    if (backing) {
        SDL_Rect r = hud_text_rect(s, x, y, align);
        if (scale != 1.0) {
            r = SDL_Rect{x + static_cast<int>((r.x - x) * scale), y + static_cast<int>((r.y - y) * scale),
                         static_cast<int>(r.w * scale), static_cast<int>(r.h * scale)};
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xAF);
        SDL_RenderFillRect(ren, &r);
    }
    if (!g_hud_font) {   // degraded: the rect font
        draw_text(ren, s, x - (align == kHudCenter ? static_cast<int>(s.size()) * 3 : 0), y, 1, col.r, col.g, col.b);
        return;
    }
    size_t start = 0;
    int line = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        const std::string ln = s.substr(start, nl - start);
        const int w = g_hud_font->text_width(ln);
        double cx = align == kHudCenter ? x - w * scale / 2.0 : align == kHudRight ? x - w * scale : x;
        const double cy = y + line * hud_line_h() * scale;
        for (uint32_t cp : ra2assets::UFont::decode_utf8(ln)) {
            const int gw = g_hud_font->width(cp);
            if (gw > 0) {
                if (SDL_Texture* t = hud_glyph_tex(tc, cp, scale * hud_render_scale(ren))) {
                    const int gh = g_hud_font->rows() - kHudTrimTop;
                    auto put = [&](double dx, double dy, SDL_Color c) {
                        SDL_SetTextureColorMod(t, c.r, c.g, c.b);
                        SDL_SetTextureAlphaMod(t, c.a);
                        SDL_Rect d{static_cast<int>(std::lround(cx + dx * scale)),
                                   static_cast<int>(std::lround(cy + dy * scale)),
                                   static_cast<int>(std::lround(gw * scale)),
                                   static_cast<int>(std::lround(gh * scale))};
                        SDL_RenderCopy(ren, t, nullptr, &d);
                    };
                    if (shadow) {
                        const SDL_Color k{0, 0, 0, 255};
                        put(1, 0, k); put(0, 1, k); put(1, 1, k);
                    }
                    put(0, 0, col);
                    SDL_SetTextureColorMod(t, 255, 255, 255);
                    SDL_SetTextureAlphaMod(t, 255);
                }
                cx += gw * scale;
            } else {
                cx += 4 * scale;
            }
        }
        if (nl >= s.size()) break;
        start = nl + 1;
        ++line;
    }
}

// --- W2-UI-02/03/04/05: per-cameo overlays [RE: wave2/04 #2, FUN_0067F800] ---

// Draw a SHP frame with a constant alpha (the 0x404 TransLucent50 blit).
void blit_shp_alpha(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& key,
                    const ra2assets::ShpFile& shp, int frame, const ra2assets::Palette& pal, int x, int y,
                    Uint8 alpha) {
    SDL_Texture* tex = shp_frame_tex(tc, side_tex_key(key), shp, frame, pal);   // W4-AM-04
    if (!tex) return;
    const ra2assets::ShpFile::Frame f = shp.frame_info(frame);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_Rect dst{x + f.x, y + f.y, f.w, f.h};
    SDL_RenderCopy(ren, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

// DARKEN.SHP with the Darken blitter (0x401: dst >>= 1 under every opaque
// source pixel) -- a modulate blend by 50 % grey through the shape's mask.
void blit_shp_darken(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& key_in,
                     const ra2assets::ShpFile& shp, int frame, int x, int y) {
    if (frame < 0 || frame >= shp.count()) return;
    const std::string key = side_tex_key(key_in);   // W4-AM-04
    const ra2assets::ShpFile::Frame f = shp.frame_info(frame);
    if (f.w <= 0 || f.h <= 0) return;
    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (!tex) {
        const std::vector<uint8_t> idx = shp.decode_indexed(frame);
        std::vector<uint8_t> rgba(static_cast<size_t>(f.w) * f.h * 4, 255);   // opaque white = identity
        for (size_t i = 0; i < idx.size(); ++i)
            if (idx[i]) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 128;
        tex = static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), f.w, f.h));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_MOD);
    }
    SDL_Rect dst{x + f.x, y + f.y, f.w, f.h};
    SDL_RenderCopy(ren, tex, nullptr, &dst);
}

// Status text + queue count for one 60x48 cameo cell (x, y):
//   count "%d" right-aligned at (x+60, y+1) (0x242) when shown;
//   status centred at (x+30, y+1) (0x142), or left at (x+2, y+1) (0x42) when a
//   count is also shown; each over its 0xAF black box, in the UI colour.
void draw_cameo_text(SDL_Renderer* ren, ra2engine::TextureCache& tc, int x, int y, const std::string& status,
                     int count) {
    const SDL_Color col = hud_ui_color();
    const bool show_count = count > 0;
    if (show_count) hud_text(ren, tc, std::to_string(count), x + 60, y + 1, col, kHudRight, true);
    if (!status.empty()) {
        if (show_count) hud_text(ren, tc, status, x + 2, y + 1, col, kHudLeft, true);
        else hud_text(ren, tc, status, x + 30, y + 1, col, kHudCenter, true);
    }
}

// W2-UI-07: the boxed tooltip [RE: FUN_0067F580 text; RE-FALLBACK (Q3): box =
// the same 0xAF black fill with a 1 px UI-colour frame, placed left of the
// pointer over the sidebar and below-right of it elsewhere, clamped on screen].
void draw_tooltip_box(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& text, int px, int py,
                      int screen_w, int screen_h, bool left_of_pointer, double scale = 1.0) {
    if (text.empty()) return;
    const SDL_Color col = hud_ui_color();
    // W6-01-12: `scale` > 1 on a touch canvas (the box grows with the text).
    const int w = static_cast<int>((hud_text_width(text) + 8) * scale);
    const int h = static_cast<int>((hud_text_height(text) + 6) * scale);
    int x = left_of_pointer ? px - w - 4 : px + 12;
    int y = py + (left_of_pointer ? 0 : 18);
    x = std::clamp(x, 0, std::max(0, screen_w - w));
    y = std::clamp(y, 0, std::max(0, screen_h - h));
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xAF);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
    SDL_RenderDrawRect(ren, &r);
    hud_text(ren, tc, text, x + static_cast<int>(4 * scale), y + static_cast<int>(3 * scale), col, kHudLeft,
             false, true, scale);
}

// "UIName $Cost" with every space turned into a newline [RE: FUN_0067F580].
std::string cameo_tooltip_text(const std::string& ui_name, int cost) {
    std::string s = ui_name + " $" + std::to_string(cost);
    for (char& c : s)
        if (c == ' ') c = '\n';
    return s;
}

// W5-PRES: game-font text in one RGB colour (every non-zero glyph pixel white,
// tinted by a texture colour mod) -- the credits tally's side colour.
inline int hud_text_rgb(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2assets::Font& font,
                        const std::string& s, int x, int y, Uint8 r, Uint8 g, Uint8 b) {
    static ra2assets::Palette white{};
    static bool ok = false;
    if (!ok) {
        for (int i = 1; i < 256; ++i) white[i] = ra2assets::Rgba{255, 255, 255, 255};
        white[0] = ra2assets::Rgba{0, 0, 0, 0};
        ok = true;
    }
    int cx = x;
    for (unsigned char ch : s) {
        const int w = font.width(ch);
        if (ch != ' ' && ch >= font.first() && ch <= font.last() && w > 0) {
            const ra2assets::FntGlyph& gl = font.glyph(ch);
            if (gl.w > 0 && gl.h > 0) {
                const std::string key = "fntw:" + std::to_string((int)ch);
                SDL_Texture* tex = fnt_glyph_tex(ren, tc, key, gl, white, 1.0);
                const SDL_Rect dst{cx, y, gl.w, gl.h};
                SDL_SetTextureColorMod(tex, r, g, b);
                SDL_RenderCopy(ren, tex, nullptr, &dst);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            }
        }
        cx += w > 0 ? w : font.height() / 2;
    }
    return cx - x;
}
