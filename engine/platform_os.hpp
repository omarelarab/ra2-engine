#pragma once
// W2-AR-22: small OS hooks kept out of main.cpp (which must not see
// <windows.h>). Every function is a harmless no-op / empty result on
// platforms that lack the facility.
#include <string>
namespace ra2engine {
// The retail install's registry InstallPath folder (HKLM SOFTWARE/Westwood/
// Red Alert 2, 32- then 64-bit view), "" when absent or not on Windows.
std::string win_registry_install_dir();
// A native folder picker; "" when cancelled or unsupported (non-Windows).
std::string os_browse_folder(const char* title);
// Desktop "attach to a console when there is one" for a GUI-subsystem build,
// so --help/--version/--play still print when run from a terminal.
void os_attach_parent_console();
// R-182: a GUI-subsystem launch (ra2game.exe double-clicked: no console and
// no redirected stderr) sends stdout/stderr to `path` (truncated per run).
// Returns true when it redirected. No-op elsewhere.
bool os_log_to_file_if_no_console(const std::string& path);
// W2-AR-04 (Android): open the in-app game-file importer (SetupActivity,
// via RA2Activity.openImporter()); false off Android or on a JNI failure.
bool os_android_open_importer();
// Multiplayer LAN discovery: hold / release the Wi-Fi MulticastLock
// (RA2Activity.netMulticastLock). No-op off Android.
void os_android_multicast_lock(bool on);
// W9-04-01/-14 (Android): seed the soft keyboard's session with an engine
// edit's text (UTF-8), its kind (kOsIme*: the keyboard layout) and its length
// limit (0 none) -- SDLActivity.ra2SetImeField. Call BEFORE SDL_StartTextInput
// (and again when the engine changed the text itself). No-op off Android.
enum : int { kOsImeText = 0, kOsImeUri = 1, kOsImeCode = 2, kOsImeChat = 3 };
void os_android_ime_field(const std::string& text, int kind, int max);
// W9-04-10 (Android): bind the process to the Wi-Fi / Ethernet network for
// LAN play (on) or clear the binding (off) -- RA2Activity.netBindLan.
// Returns 0 no Wi-Fi, 1 Wi-Fi is the default network (or off / not Android),
// 2 bound to Wi-Fi while mobile data is the default.
int os_android_bind_lan(bool on);
// W6-01-10: Android destroyed the game activity WITHOUT the player quitting
// (a configuration change it cannot hand to the running activity, e.g. an
// overlay / navigation-mode / theme change; RA2Activity.onDestroy with
// !isFinishing()). The background autosave must then survive the scene
// teardown so the next launch offers RESUME. Set from Java (JNI) or by the
// --play harness's `lifecycle relaunch`.
void os_set_keep_autosave(bool keep);
bool os_keep_autosave();
#ifdef RA2_IOS
// iOS (engine/platform_ios.mm, compiled only in the iOS build):
// the app's Documents folder (no trailing '/'; shared with the Files app and
// Finder file sharing through Info.plist).
std::string os_ios_documents_dir();
// <app bundle>/GameData when a personal test build bundled the game files
// there (ios/scripts/link_game_data.sh), else "".
std::string os_ios_bundle_data_dir();
// The in-app importer: a modal Files folder picker; the engine-read files
// in the picked folder (one sub-folder level, like Android's SetupActivity)
// are copied into Documents/ra2/ under their canonical names. Blocks (the
// UIKit run loop keeps running) and returns the number of files copied,
// 0 when cancelled, -1 on an error (already shown to the player).
// restart_note: on success, tell the player the files load on the next start.
int os_ios_import_game_files(bool restart_note);
#endif
}  // namespace ra2engine
