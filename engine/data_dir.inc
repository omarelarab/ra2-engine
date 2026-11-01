// W2-AR-03 / -05 / -22: where the user's game files live, and whether they are
// all there. Textually included by main.cpp near the top (anonymous namespace)
// in place of the old two-line env_install_dir().
//
// Data dir search order (desktop):
//   1. --data <dir> on the command line (g_data_dir_cli),
//   2. the RA2_INSTALL_DIR environment variable,
//   3. RA2.INI [Engine] DataDir in the user's pref dir (written by the
//      missing-files page's BROWSE button),
//   4. the directory holding the executable,
//   5. the retail install's registry key (HKLM\SOFTWARE\Westwood\Red Alert 2
//      InstallPath, which names ra2.exe -- the original's own lookup),
//   6. the historical dev default D:\Red Alert 2.
// The first candidate that holds ra2.mix wins; when none does, the first
// EXPLICIT one (1-3) or else the dev default is returned so the missing-files
// page can name it.
// Android: SDL_AndroidGetExternalStoragePath()/ra2/ (the app's own external
// files dir, filled by the in-app importer, MTP, or adb -- see README).
// iOS: <Documents>/ra2/ (filled by the in-app importer, the Files app or
// Finder file sharing); a personal test build may also carry the files
// read-only inside the app bundle (GameData/, ios/README.md), used only
// while Documents/ra2/ has no ra2.mix.

std::string g_data_dir_cli;        // --data <dir>
std::string g_data_dir_resolved;   // cached result of env_install_dir()

// One file the engine reads from the data dir. `required` files stop the game
// at startup (the missing-files page); optional ones only lose a feature.
struct DataFileSpec {
    const char* name;      // exact retail spelling (Android is case-sensitive)
    bool required;
    const char* purpose;   // shown on the missing-files page
};
constexpr DataFileSpec kDataFiles[] = {
    {"ra2.mix", true, "RULES, ART, MAPS, UNITS, MENUS"},
    {"ra2md.mix", true, "RULESMD, ARTMD, SOUNDMD, VOXELS"},
    {"language.mix", true, "VOICES, EVA, CAMEOS, STRINGS"},
    {"multimd.mix", true, "SKIRMISH MAPS"},
    {"langmd.mix", false, "SKIRMISH MAP NAMES"},
    {"mapsmd03.mix", false, "DEFAULT MAP"},
    {"theme.mix", false, "MUSIC"},
    {"expand01.mix", false, "CAMPAIGN RULES PATCH"},
    {"Maps01.mix", false, "ALLIED CAMPAIGN MAPS"},
    {"MAPS02.MIX", false, "SOVIET CAMPAIGN MAPS"},
    {"ra2.csf", false, "PATCHED STRING TABLE"},
};

struct DataPreflight {
    std::string dir;
    std::vector<const DataFileSpec*> missing_required;
    std::vector<const DataFileSpec*> missing_optional;
    bool ok() const { return missing_required.empty(); }
};

bool data_file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}

DataPreflight preflight_data_files(const std::string& dir) {
    DataPreflight p;
    p.dir = dir;
    for (const DataFileSpec& f : kDataFiles) {
        if (data_file_exists(dir + f.name)) continue;
        (f.required ? p.missing_required : p.missing_optional).push_back(&f);
    }
    return p;
}

std::string with_trailing_sep(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') {
#ifdef _WIN32
        d += '\\';
#else
        d += '/';
#endif
    }
    return d;
}

// RA2.INI in the pref dir (same file the Options page writes; see
// settings_file_path() -- duplicated here because this runs before it exists).
std::string data_dir_ini_path() {
    if (const char* o = std::getenv("RA2_PREF_DIR")) return std::string(o) + "RA2.INI";
    char* p = SDL_GetPrefPath("ra2engine", "RA2");
    if (!p) return {};
    std::string s = std::string(p) + "RA2.INI";
    SDL_free(p);
    return s;
}

std::string data_dir_from_ini() {
    const std::string path = data_dir_ini_path();
    if (path.empty()) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    const ra2engine::IniDoc doc = ra2engine::IniDoc::parse(ss.str());
    const std::optional<std::string> v = doc.get("Engine", "DataDir");
    return v ? *v : std::string();
}

// TG-15 / TG-16: small [Engine] flags in RA2.INI (TouchButtons, GestureHelpSeen).
// Read / written in place, other keys kept (save_user_settings keeps them too).
bool engine_ini_flag(const char* key, bool def) {
    const std::string path = data_dir_ini_path();
    if (path.empty()) return def;
    std::ifstream f(path, std::ios::binary);
    if (!f) return def;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::optional<std::string> v = ra2engine::IniDoc::parse(ss.str()).get("Engine", key);
    if (!v || v->empty()) return def;
    const char c = (*v)[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}
bool engine_ini_set_flag(const char* key, bool on) {
    const std::string path = data_dir_ini_path();
    if (path.empty()) return false;
    std::string text;
    {
        std::ifstream f(path, std::ios::binary);
        if (f) { std::ostringstream ss; ss << f.rdbuf(); text = ss.str(); }
    }
    ra2engine::IniDoc doc = ra2engine::IniDoc::parse(text);
    doc.set("Engine", key, on ? "yes" : "no");
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    const std::string out = doc.serialize();
    o.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(o);
}

// Persist a user-chosen data dir (missing-files page BROWSE).
bool save_data_dir_to_ini(const std::string& dir) {
    const std::string path = data_dir_ini_path();
    if (path.empty()) return false;
    std::string text;
    {
        std::ifstream f(path, std::ios::binary);
        if (f) { std::ostringstream ss; ss << f.rdbuf(); text = ss.str(); }
    }
    ra2engine::IniDoc doc = ra2engine::IniDoc::parse(text);
    doc.set("Engine", "DataDir", dir);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    const std::string out = doc.serialize();
    o.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(o);
}


std::vector<std::string> data_dir_candidates() {
    std::vector<std::string> c;
#if defined(__ANDROID__) && !defined(RA2_ANDROID_CLI)
    const char* ext = SDL_AndroidGetExternalStoragePath();
    std::string dir = ext ? ext : "/sdcard/Android/data/org.ra2.engine/files";
    c.push_back(dir + "/ra2/");
#elif defined(RA2_IOS)
    c.push_back(ra2engine::os_ios_documents_dir() + "/ra2/");
    {
        const std::string b = ra2engine::os_ios_bundle_data_dir();
        if (!b.empty()) c.push_back(with_trailing_sep(b));
    }
#else
    if (!g_data_dir_cli.empty()) c.push_back(with_trailing_sep(g_data_dir_cli));
    if (const char* e = std::getenv("RA2_INSTALL_DIR")) if (*e) c.push_back(with_trailing_sep(e));
    {
        const std::string ini = data_dir_from_ini();
        if (!ini.empty()) c.push_back(with_trailing_sep(ini));
    }
    if (char* base = SDL_GetBasePath()) {
        c.push_back(with_trailing_sep(base));
        SDL_free(base);
    }
#ifdef _WIN32
    {
        const std::string reg = ra2engine::win_registry_install_dir();
        if (!reg.empty()) c.push_back(with_trailing_sep(reg));
    }
#endif
    c.push_back("D:\\Red Alert 2\\");
#endif
    return c;
}

std::string resolve_data_dir() {
    const std::vector<std::string> c = data_dir_candidates();
#if defined(__ANDROID__) && !defined(RA2_ANDROID_CLI)
    return c.front();
#elif defined(RA2_IOS)
    // The imported copy wins; else the bundled test data; else the (empty)
    // Documents folder, so the missing-files page names it.
    for (const std::string& d : c)
        if (data_file_exists(d + "ra2.mix")) return d;
    return c.front();
#else
    // An EXPLICIT choice (CLI / env / INI, the first three candidates) is
    // used as given -- if it lacks the files, the missing-files page names
    // it. Only the implicit locations are searched.
    const char* env = std::getenv("RA2_INSTALL_DIR");
    if (!g_data_dir_cli.empty() || (env && *env) || !data_dir_from_ini().empty()) return c.front();
    for (const std::string& d : c)
        if (data_file_exists(d + "ra2.mix")) return d;
    return c.back();
#endif
}

// Forget the cached dir (after BROWSE / an Android import changes it).
void reset_data_dir() { g_data_dir_resolved.clear(); }

std::string env_install_dir() {
    if (!g_data_dir_resolved.empty()) return g_data_dir_resolved;
    g_data_dir_resolved = resolve_data_dir();
#if defined(__ANDROID__) && !defined(RA2_ANDROID_CLI)
    // std::cerr/std::cout are not captured on Android; SDL_Log routes to
    // logcat (tag "SDL"). Create the folder so it shows up over MTP.
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(g_data_dir_resolved), ec);
    SDL_Log("[assets] android asset dir = %s", g_data_dir_resolved.c_str());
#elif defined(RA2_IOS)
    // Create Documents/ra2/ so it shows up in the Files app (the bundle dir,
    // when picked, is read-only and already exists).
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(ra2engine::os_ios_documents_dir() + "/ra2/"), ec);
    SDL_Log("[assets] ios asset dir = %s", g_data_dir_resolved.c_str());
#endif
    return g_data_dir_resolved;
}
