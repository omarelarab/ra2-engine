#pragma once
// engine/mp/mp_model -- the multiplayer lobby's state (8 slots, map, game
// options, seed) and its serialisation as the ORIGINAL game's options string
// (docs/gap/net/01-original-protocol.md §7.1, built by FUN_005DB540, parsed
// by FUN_005E35A0):
//
//   Seed,GameSpeed,GameSpeed,Credits,UnitCount,ShortGame,SuperWeapons,
//   BuildOffAlly,MCVRedeploy,Crates,MPModeIndex,0,0,MapSize,IsOfficial,
//   MapCRC,Scenario:Digest,
//   per human  Name,Side,Colour,Start,Team,%x,%x,%x,
//   per AI     Difficulty,Country,Colour,Start,Ally,
//
// The three per-human "address words" (the IPX address in the original) carry
// the clone's slot index, flags (bit0 ready, bit1 host, bit2 has-map) and the
// low 32 bits of the player id. Clone extensions follow as key=value tokens
// (tl = tech level, max = map max players, slots = the 8-slot layout string
// H/A/O/C) so a decoder can place humans / AIs back into their slots. Names
// never contain ',', ':' or '=' (replaced by '_').
//
// Also: the session config handed to the lockstep layer when the match
// starts, and its conversion to the launcher's SkirmishConfig. SDL-free.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "skirmish.hpp"

namespace ra2mp {

constexpr int kSlots = 8;
constexpr int kRandom = -2;        // country / colour "Random"
constexpr int kNoTeam = -1;
constexpr int kRandomStart = -1;
constexpr std::uint16_t kNetProto = 2;   // bump on any lobby/session wire change (2: NET-PLAY platform tag, GO peer table)
// W9-05-02: the sim version two machines must share to play together. It was
// a hand-bumped constant (1 since it was introduced, never bumped across 36
// world.cpp commits and save formats 34-45); now it is derived from the BUILD:
// FNV-1a of the commit the binary was built from (RA2_SIM_BUILD, set by
// cmake/ra2_sources.cmake; "-dirty" is ignored so a phone APK and a PC build
// of one commit still match) and save::kFormatVersion. Test hook:
// RA2_NET_SIM_SALT=<text> mixes <text> in (simulates another build).
std::uint32_t sim_version();
const char* sim_build_id();   // the commit / version text behind sim_version()

enum class SlotKind : std::uint8_t { Open = 0, Closed = 1, Human = 2, AI = 3 };

struct MpSlot {
    SlotKind kind = SlotKind::Open;
    std::string name;              // human nickname
    int ai = 1;                    // AI level (skirmish_lobby kLobbyAi*: 0 Brutal, 1 Medium, 2 Easy)
    int country = kRandom;         // index into the lobby country list, or -2
    int color = kRandom;           // lobby colour 0..7, or -2
    int team = kNoTeam;            // -1 none, 0..3 = A..D
    int start = kRandomStart;      // -1 random, 0..7 = map start waypoint
    bool ready = false;
    bool host = false;
    bool has_map = true;
    std::uint32_t pid_lo = 0;      // low 32 bits of the player id
    bool occupied() const { return kind == SlotKind::Human || kind == SlotKind::AI; }
};

struct MpOptions {
    int game_speed = 1;            // 0 fastest .. 6 slowest
    int credits = 10000;
    int unit_count = 10;
    bool short_game = true;
    bool superweapons = true;
    bool build_off_ally = true;
    bool mcv_repacks = true;
    bool crates = true;
    int tech_level = 10;
    // NET-PLAY (clone extensions, 05-transport-infra.md §6.3 / §7):
    bool ai_takeover = false;      // a player who leaves / drops: an AI takes the house over (else defeated)
    bool cross_platform = false;   // "Cross-platform (experimental)": admit Windows + Android + iOS peers
};

// NET-PLAY: this build's platform tag for the lobby handshake
// (1 Windows / desktop, 2 Android arm64, 3 Android x86_64, 4 iOS arm64).
inline std::uint8_t platform_code() {
#if defined(RA2_IOS)
    return 4;
#elif defined(__ANDROID__) && defined(__aarch64__)
    return 2;
#elif defined(__ANDROID__)
    return 3;
#else
    return 1;
#endif
}

// W9-04-13: a platform tag for the lists and the refusal notice ("" unknown).
inline const char* platform_name(std::uint8_t code) {
    switch (code) {
        case 1: return "PC";
        case 2: return "Android";
        case 3: return "Android x86";
        case 4: return "iOS";
        default: return "";
    }
}

struct MpMap {
    std::string file;              // multimd.mix entry, e.g. "xmp01t4.map"
    std::string digest;            // the map's [Digest] 1= value (the original compares it, §7.1)
    int max_players = 8;
    std::uint32_t size = 0;        // map file size
    bool official = true;
};

struct MpLobbyModel {
    MpSlot slots[kSlots];
    MpOptions opt;
    MpMap map;
    std::uint32_t seed = 0;
    int mode = 1;                  // MPModeIndex (1 = Battle)
};

// Replace the separator characters a name may not contain.
std::string sanitize_name(const std::string& s, std::size_t max_len = 16);

std::string encode_options(const MpLobbyModel& m);
bool decode_options(const std::string& s, MpLobbyModel& out);

// [Digest] 1= value of a map's INI text ("" when absent).
std::string map_digest_of(const std::string& map_text);
// The digest as it travels in the options string (base64 '=' padding is a
// separator there, so it is carried as '_'); compare digests through this.
inline std::string normalize_digest(const std::string& d) { return sanitize_name(d, 64); }

// Counts and checks.
int human_count(const MpLobbyModel& m);
int player_count(const MpLobbyModel& m);    // humans + AIs
int first_open_slot(const MpLobbyModel& m);
// Owner slot of a colour / start (-1 = free), ignoring `except`.
int color_owner(const MpLobbyModel& m, int color, int except = -1);
int start_owner(const MpLobbyModel& m, int start, int except = -1);

enum class StartCheck { Ok, NeedTwoPlayers, TooManyPlayers, NotReady, MissingMap, NoMap };
StartCheck can_start(const MpLobbyModel& m);
const char* start_check_text(StartCheck c);

// --- the session handed to the lockstep layer --------------------------------

enum class MpTransportKind : std::uint8_t { Lan = 0, Tunnel = 1 };

struct MpPlayer {
    int slot = 0;                  // lobby slot 0..7
    int house = 0;                 // house index in the World (slot order: identical on every machine)
    bool is_ai = false;
    bool local = false;            // this machine's player
    std::string name;
    int ai = 1;
    int country = 0;               // resolved (no Random left)
    int color = 0;                 // resolved lobby colour
    int team = kNoTeam;
    int start = 0;                 // resolved start waypoint
    std::uint32_t pid_lo = 0;
    std::uint64_t peer = 0;        // transport peer id (0 = local / AI)
};

struct MpSessionConfig {
    MpLobbyModel lobby;            // the final lobby (seed set)
    std::string options;           // the final options string (what GO confirmed)
    std::uint32_t seed = 0;
    bool is_host = false;
    int local_slot = -1;
    int local_house = -1;
    MpTransportKind transport = MpTransportKind::Lan;
    std::string host_addr;         // LAN: host ip:port; tunnel: server ip:v3port
    std::uint32_t local_v3 = 0, host_v3 = 0;
    // NET-PLAY: transport peer of every remote human slot (GO's peer table;
    // the host fills it from its own view). resolve_session copies it into
    // MpPlayer::peer.
    std::vector<std::pair<int, std::uint64_t>> slot_peers;
    std::vector<MpPlayer> players; // occupied slots in slot order
};

// The deterministic Random resolution every machine performs identically
// from the seed (country, colour, start). countries = list size.
void resolve_session(MpSessionConfig& s, int country_count);

struct MpConvertCtx {
    std::vector<std::string> countries;
    std::function<ra2engine::World::Side(const std::string&)> side_of;
};

// Lockstep view: houses in slot order (house k = players[k]); human remote
// players are humans (is_ai false) -- the lockstep layer drives them.
ra2engine::SkirmishConfig session_to_skirmish(const MpSessionConfig& s, const MpConvertCtx& ctx);
// Local preview (the placeholder until the lockstep layer lands): the local
// player becomes house 0 and remote humans are driven by a Medium AI so the
// map, houses and options can be checked in a normal single-player game.
ra2engine::SkirmishConfig session_to_local_preview(const MpSessionConfig& s, const MpConvertCtx& ctx);

std::string describe_session(const MpSessionConfig& s);

}  // namespace ra2mp
