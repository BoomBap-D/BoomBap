#pragma once
// ============================================================================
// GENERATED - factory kit data table (bank build, Nov 14, 2026).
// Source of truth: ~/workspace/boombap/preset-kits - Kit 1 "Boom Bap
// Core" (item 82) and Wave 1 (items 140/159): Golden Dust, Lowrider
// Funk, Modern Trap. FactoryKitData.bin is those 64 WAVs concatenated
// in this table's order and bundled into the binaries via
// juce_add_binary_data (CMakeLists: BoomBapBinaryData). Regenerate the
// .bin and this table together or the offsets lie.
// ============================================================================
#include <cstdint>

namespace boombap
{
namespace factorydata
{
struct PadEntry { const char* name; std::uint32_t offset; std::uint32_t size; };
struct KitEntry { const char* name; PadEntry pads[16]; };

inline constexpr int numKits = 4;
inline constexpr std::uint32_t blobSize = 4778171;

inline const KitEntry kits[numKits] =
{
    { "Boom Bap Core",
      {
        { "Deep Kick", 0u, 72809u },
        { "Punch Kick", 72809u, 45026u },
        { "Fat Snare", 117835u, 29150u },
        { "Rimshot", 146985u, 9305u },
        { "Closed Hat", 156290u, 11951u },
        { "Open Hat", 168241u, 72809u },
        { "Clap", 241050u, 37088u },
        { "Shaker", 278138u, 34442u },
        { "Low Tom", 312580u, 52964u },
        { "High Tom", 365544u, 39734u },
        { "Crash", 405278u, 198494u },
        { "Ride Bell", 603772u, 132344u },
        { "808 Bass C", 736116u, 125729u },
        { "808 Bass G", 861845u, 112499u },
        { "Chord Stab", 974344u, 50318u },
        { "Vinyl Dust Hit", 1024662u, 79424u },
      } },
    { "Golden Dust",
      {
        { "Dust Kick", 1104086u, 82070u },
        { "Sub Thump Kick", 1186156u, 95300u },
        { "Crack Snare", 1281456u, 31796u },
        { "Rimshot", 1313252u, 10628u },
        { "Closed Hat", 1323880u, 13274u },
        { "Open Hat", 1337154u, 79424u },
        { "Clap", 1416578u, 45026u },
        { "Shaker", 1461604u, 34442u },
        { "Low Tom", 1496046u, 55610u },
        { "Floor Tom", 1551656u, 47672u },
        { "Crash", 1599328u, 211724u },
        { "Agogo Bell", 1811052u, 72809u },
        { "Sub Bass C", 1883861u, 132344u },
        { "Sub Bass G", 2016205u, 119114u },
        { "Soul Stab", 2135319u, 52964u },
        { "Crackle Bed Hit", 2188283u, 119114u },
      } },
    { "Lowrider Funk",
      {
        { "Round Kick", 2307397u, 68840u },
        { "Funk Kick", 2376237u, 39734u },
        { "West Snare", 2415971u, 39734u },
        { "Rimshot", 2455705u, 9305u },
        { "Closed Hat", 2465010u, 17243u },
        { "Open Hat", 2482253u, 72809u },
        { "Clap", 2555062u, 47672u },
        { "Tambourine", 2602734u, 39734u },
        { "Low Tom", 2642468u, 52964u },
        { "High Tom", 2695432u, 39734u },
        { "Crash", 2735166u, 198494u },
        { "Cowbell", 2933660u, 42380u },
        { "Sub Bass C", 2976040u, 152186u },
        { "Sub Bass G", 3128226u, 138959u },
        { "Worm Hit", 3267185u, 66194u },
        { "Talkbox Stab", 3333379u, 50318u },
      } },
    { "Modern Trap",
      {
        { "Trap Kick", 3383697u, 42380u },
        { "808 Kick", 3426077u, 99269u },
        { "Trap Snare", 3525346u, 39734u },
        { "Clap Layer", 3565080u, 29150u },
        { "Closed Hat", 3594230u, 7319u },
        { "Open Hat", 3601549u, 52964u },
        { "Trap Clap", 3654513u, 42380u },
        { "Hat Tick", 3696893u, 4013u },
        { "Low Tom", 3700906u, 59579u },
        { "Riser Build", 3760485u, 92651u },
        { "Crash", 3853136u, 185261u },
        { "Siren Hit", 4038397u, 71486u },
        { "Trap 808 C", 4109883u, 165419u },
        { "Trap 808 G", 4275302u, 145574u },
        { "Dark Bells", 4420876u, 92651u },
        { "Riser Fx", 4513527u, 264644u },
      } },
};
} // namespace factorydata
} // namespace boombap
