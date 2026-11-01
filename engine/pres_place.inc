// pres_place.inc -- W5-PRES R-102 / R-144: the building placement grid and the
// primary-factory label of the real game. Included by main.cpp before
// draw_placement_cursor; inert without g_fx (every gate keeps the old
// outlines / markers).

namespace camp { std::string csf_text(const std::string& dir, const std::string& label); }

// [RE: CellClass::DrawPlacement 0x47EBA0, docs/re/w5-presentation.md §2]:
// PLACE.SHP centred at (cell.x + 30, cell.y - 15*height - 1) -- the cell's
// diamond centre -- opaque; a valid foundation cell (adjacency AND clear to
// build) draws frame 0, a blocked one frame 2 + its ramp type. No building
// ghost in vanilla. Returns false (caller keeps its outlines) without art.
bool pres_draw_place_grid(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::PlacementCheck& chk,
                          const std::vector<ra2engine::Cellxy>& cells, const Assets& a,
                          const ra2engine::Camera& cam, int W, int H) {
    if (!g_fx) return false;
    static std::shared_ptr<ra2assets::ShpFile> place;
    static bool tried = false;
    if (!tried) {
        tried = true;
        place = g_fx->projectile_shp("place");
        std::cerr << "[place] PLACE.SHP " << (place ? std::to_string(place->count()) + " frames" : "missing") << "\n";
    }
    if (!place || place->count() <= 0) return false;
    for (std::size_t k = 0; k < cells.size(); ++k) {
        const ra2assets::MapCell* mc = a.map.at(cells[k].x, cells[k].y);
        const int level = mc ? mc->level : 0;
        const bool ok = chk.adjacency && (k < chk.cell_ok.size() ? chk.cell_ok[k] : false);
        int fr = ok ? 0 : 2 + pres_cell_ramp(a, cells[k].x, cells[k].y);
        if (fr >= place->count()) fr = ok ? 0 : 2;
        const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cells[k].x, cells[k].y, level, 60, 30, 15);
        // The convert (0x87E644) is the PALETTE.PAL one the pips use: frame 0
        // green, frame 2+ red (verified against the other palettes by eye).
        const ra2assets::Palette* pp = (g_p5_art && g_p5_art->has_pips_pal) ? &g_p5_art->pips_pal : nullptr;
        g_fx->blit(ren, tc, *place, pp ? "place:p" : "place:a", fr, p.x + cam.x + 30, p.y + cam.y + 15 - 1,
                   255, W, H, pp);
    }
    return true;
}

// [RE: TechnoClass::DrawExtraInfo 0x709EE0, §3]: a primary factory shows CSF
// TXT_PRIMARY in a box -- black fill, house-colour outline, house-colour text
// (style 0x149, padding 4,2) -- over the building; the clone draws it for the
// selected building [Medium-Low: the selected-only gate was not traced].
bool pres_draw_primary_label(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& world,
                             const ra2engine::Building& b, int sx, int sy, int playfield_w) {
    if (!g_fx || !g_game_font) return false;
    static std::string text;
    if (text.empty()) {
        text = camp::csf_text(env_install_dir(), "TXT_PRIMARY");
        if (text.empty() || text == "TXT_PRIMARY") text = "Primary";
    }
    static ra2assets::Palette white{};
    static bool white_ok = false;
    if (!white_ok) {
        for (int i = 1; i < 256; ++i) white[i] = ra2assets::Rgba{255, 255, 255, 255};
        white[0] = ra2assets::Rgba{0, 0, 0, 0};
        white_ok = true;
    }
    const ra2assets::Font& font = *g_game_font;
    int tw = 0;
    for (unsigned char ch : text) tw += font.width(ch) > 0 ? font.width(ch) : font.height() / 2;
    const int th = font.height();
    SDL_Rect box{sx - tw / 2 - 4, sy - th / 2 - 2, tw + 8, th + 4};
    if (box.x + box.w <= 0 || box.x >= playfield_w) return true;
    const ra2assets::Rgba hc = world.house_palette(*g_map_pals[2], b.house)[16];   // ramp top = house colour
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawColor(ren, hc.r, hc.g, hc.b, 255);
    SDL_RenderDrawRect(ren, &box);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int cx = box.x + 4;
    for (unsigned char ch : text) {
        const int w = font.width(ch);
        if (ch != ' ' && ch >= font.first() && ch <= font.last() && w > 0) {
            const ra2assets::FntGlyph& g = font.glyph(ch);
            if (g.w > 0 && g.h > 0) {
                const std::string key = "fntw:" + std::to_string((int)ch);
                SDL_Texture* tex = fnt_glyph_tex(ren, tc, key, g, white, 1.0);
                SDL_Rect dst{cx, box.y + 2, g.w, g.h};
                SDL_SetTextureColorMod(tex, hc.r, hc.g, hc.b);
                SDL_RenderCopy(ren, tex, nullptr, &dst);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            }
        }
        cx += w > 0 ? w : font.height() / 2;
    }
    return true;
}

// [RE: TacticalClass::DrawFactoryRallyLines 0x6D9F40, docs/re/w5-presentation.md §3]:
// for the selected own factory with a rally target, a dashed line (pattern
// 0x8418D0: 5 px on, 3 off, offset (INT_MAX - Frame) % 15 -- marching ants)
// from the building centre to the target cell centre: black at y+2, then the
// house colour at y+1 and y+0.
void pres_draw_rally_line(SDL_Renderer* ren, const ra2engine::World& world, const ra2engine::Building& b, int ax,
                          int ay, int bx, int by) {
    const ra2assets::Rgba hc = g_map_pals[2] ? world.house_palette(*g_map_pals[2], b.house)[16]
                                             : ra2assets::Rgba{255, 255, 255, 255};
    const int phase = static_cast<int>((0x7fffffffL - g_pres_tick) % 15);
    auto dashed = [&](int dy, Uint8 r, Uint8 g, Uint8 bl) {
        SDL_SetRenderDrawColor(ren, r, g, bl, 255);
        int x0 = ax, y0 = ay + dy;
        const int x1 = bx, y1 = by + dy;
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int ddy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + ddy, i = 0;
        for (;;) {
            if (((i + phase) & 7) < 5) SDL_RenderDrawPoint(ren, x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= ddy) { err += ddy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            ++i;
        }
    };
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    dashed(2, 0, 0, 0);
    dashed(1, hc.r, hc.g, hc.b);
    dashed(0, hc.r, hc.g, hc.b);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
}
