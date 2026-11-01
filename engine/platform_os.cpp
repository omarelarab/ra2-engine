#include "platform_os.hpp"
#include <atomic>
#include <cstdint>

#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#endif

namespace ra2engine {

#ifdef _WIN32
namespace {
std::string utf8_of(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
}  // namespace

std::string win_registry_install_dir() {
    for (REGSAM view : {static_cast<REGSAM>(KEY_WOW64_32KEY), static_cast<REGSAM>(KEY_WOW64_64KEY)}) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Westwood\\Red Alert 2", 0, KEY_READ | view, &key) !=
            ERROR_SUCCESS)
            continue;
        wchar_t buf[1024] = {0};
        DWORD len = sizeof buf - sizeof(wchar_t), type = 0;
        const LONG rc = RegQueryValueExW(key, L"InstallPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &len);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_SZ) continue;
        std::string p = utf8_of(buf);
        // InstallPath names the executable (...\Ra2.exe); keep its folder.
        const std::size_t sep = p.find_last_of("\\/");
        const std::size_t dot = p.rfind('.');
        if (dot != std::string::npos && sep != std::string::npos && dot > sep) p.resize(sep);
        if (!p.empty()) return p;
    }
    return {};
}

std::string os_browse_folder(const char* title) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    wchar_t wtitle[256];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wtitle, 256);
    BROWSEINFOW bi{};
    bi.lpszTitle = wtitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    std::string out;
    if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi)) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) out = utf8_of(path);
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return out;
}

void os_attach_parent_console() {
    if (GetConsoleWindow()) return;   // already a console process
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
bool os_log_to_file_if_no_console(const std::string& path) {
    if (GetConsoleWindow()) return false;   // a console process (ra2engine.exe)
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
        return false;   // stderr already goes somewhere (a pipe / file)
    FILE* f = nullptr;
    if (freopen_s(&f, path.c_str(), "w", stderr) != 0) return false;
    freopen_s(&f, path.c_str(), "a", stdout);
    setvbuf(stderr, nullptr, _IONBF, 0);
    return true;
}
bool os_android_open_importer() { return false; }
void os_android_multicast_lock(bool) {}
void os_android_ime_field(const std::string&, int, int) {}
int os_android_bind_lan(bool) { return 1; }
#else
bool os_log_to_file_if_no_console(const std::string&) { return false; }
std::string win_registry_install_dir() { return {}; }
std::string os_browse_folder(const char*) { return {}; }
void os_attach_parent_console() {}
#ifdef __ANDROID__
bool os_android_open_importer() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return false;
    jclass cls = env->GetObjectClass(act);
    jmethodID m = cls ? env->GetMethodID(cls, "openImporter", "()V") : nullptr;
    bool ok = false;
    if (m) {
        env->CallVoidMethod(act, m);
        ok = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return ok;
}
// Multiplayer LAN discovery: RA2Activity.netMulticastLock(boolean).
void os_android_multicast_lock(bool on) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return;
    jclass cls = env->GetObjectClass(act);
    jmethodID m = cls ? env->GetMethodID(cls, "netMulticastLock", "(Z)V") : nullptr;
    if (m) env->CallVoidMethod(act, m, static_cast<jboolean>(on ? JNI_TRUE : JNI_FALSE));
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
}
int os_android_bind_lan(bool on) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return 1;
    jclass cls = env->GetObjectClass(act);
    jmethodID m = cls ? env->GetMethodID(cls, "netBindLan", "(Z)I") : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    int r = 1;
    if (m) r = env->CallIntMethod(act, m, static_cast<jboolean>(on ? JNI_TRUE : JNI_FALSE));
    if (env->ExceptionCheck()) { env->ExceptionClear(); r = 1; }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return r;
}
// W9-04-01/-14: SDLActivity.ra2SetImeField(String, int, int) (RA2 patch of
// the vendored SDL Java). The UTF-8 text goes over as UTF-16 (NewString: no
// modified-UTF-8 pitfalls); a malformed byte becomes '?' so the IME's copy
// keeps one character per engine code point.
void os_android_ime_field(const std::string& text, int kind, int max) {
    std::u16string w;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = '?';
        std::size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6) { n = 2; cp = c & 0x1F; }
        else if ((c >> 4) == 14) { n = 3; cp = c & 0x0F; }
        else if ((c >> 3) == 30) { n = 4; cp = c & 0x07; }
        else n = 0;
        bool ok = n > 0 && i + n <= text.size();
        for (std::size_t k = 1; ok && k < n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { cp = '?'; n = 1; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w += static_cast<char16_t>(0xD800 + (cp >> 10));
            w += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            w += static_cast<char16_t>(cp);
        }
        i += n;
    }
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject act = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !act) return;
    jclass cls = env->GetObjectClass(act);   // RA2Activity: inherits SDLActivity's statics
    jmethodID m = cls ? env->GetStaticMethodID(cls, "ra2SetImeField", "(Ljava/lang/String;II)V") : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (m) {
        jstring js = env->NewString(reinterpret_cast<const jchar*>(w.data()), static_cast<jsize>(w.size()));
        if (js) {
            env->CallStaticVoidMethod(cls, m, js, static_cast<jint>(kind), static_cast<jint>(max));
            env->DeleteLocalRef(js);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
}
#else
bool os_android_open_importer() { return false; }
void os_android_multicast_lock(bool) {}
void os_android_ime_field(const std::string&, int, int) {}
int os_android_bind_lan(bool) { return 1; }
#endif
#endif

// W6-01-10: see platform_os.hpp.
static std::atomic<bool> g_keep_autosave{false};
void os_set_keep_autosave(bool keep) { g_keep_autosave = keep; }
#ifndef RA2_IOS
void os_haptic() {}   // iOS: platform_ios.mm
#endif
bool os_keep_autosave() { return g_keep_autosave; }

}  // namespace ra2engine

#ifdef __ANDROID__
// W6-01-10: RA2Activity.onDestroy() while !isFinishing() (called on the UI
// thread before SDL joins the engine thread, so the scene teardown sees it).
extern "C" JNIEXPORT void JNICALL Java_org_ra2_engine_RA2Activity_nativeKeepAutosave(JNIEnv*, jclass) {
    ra2engine::os_set_keep_autosave(true);
    SDL_Log("[lifecycle] activity destroyed without finishing: the autosave is kept");
}
#endif
