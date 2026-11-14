#pragma once
// ============================================================================
// GENERATED - preset pattern data table (patterns build, Nov 14, 2026).
// Source of truth: ~/workspace/boombap/preset-patterns - D's DopeVST packs
// converted to JSON (see INVENTORY.md there for the verified note map and
// every anomaly). PatternData.bin is the 100 JSONs concatenated in this
// table's order (drums 01-50, then basslines 001-050) and bundled into the
// binaries via juce_add_binary_data (CMakeLists: BoomBapBinaryData), the
// same way FactoryKitData.bin ships the kits. Regenerate the .bin and
// this table together (preset-patterns/make_bundle.py) or the offsets lie.
// ============================================================================
#include <cstdint>

namespace boombap
{
namespace patterndata
{
struct PatternEntry { const char* file; std::uint32_t offset; std::uint32_t size; };

inline constexpr std::uint32_t blobSize = 1216065;

inline const PatternEntry drums[50] = 
{
    { "drum-01-92bpm.json", 0u, 24496u },
    { "drum-02-91bpm.json", 24496u, 24516u },
    { "drum-03-92bpm.json", 49012u, 25624u },
    { "drum-04-93bpm.json", 74636u, 23634u },
    { "drum-05-92bpm.json", 98270u, 19640u },
    { "drum-06-92bpm.json", 117910u, 25614u },
    { "drum-07-92bpm.json", 143524u, 24452u },
    { "drum-08-92bpm.json", 167976u, 581u },
    { "drum-09-92bpm.json", 168557u, 25258u },
    { "drum-10-90bpm.json", 193815u, 26075u },
    { "drum-11-93bpm.json", 219890u, 11003u },
    { "drum-12-97bpm.json", 230893u, 12418u },
    { "drum-13-95bpm.json", 243311u, 11083u },
    { "drum-14-95bpm.json", 254394u, 6328u },
    { "drum-15-89bpm.json", 260722u, 5762u },
    { "drum-16-93bpm.json", 266484u, 11754u },
    { "drum-17-93bpm.json", 278238u, 8533u },
    { "drum-18-91bpm.json", 286771u, 6691u },
    { "drum-19-91bpm.json", 293462u, 11115u },
    { "drum-20-90bpm.json", 304577u, 12065u },
    { "drum-21-93bpm.json", 316642u, 12457u },
    { "drum-22-91bpm.json", 329099u, 20348u },
    { "drum-23-96bpm.json", 349447u, 12810u },
    { "drum-24-92bpm.json", 362257u, 11991u },
    { "drum-25-92bpm.json", 374248u, 11638u },
    { "drum-26-94bpm.json", 385886u, 10555u },
    { "drum-27-90bpm.json", 396441u, 12565u },
    { "drum-28-94bpm.json", 409006u, 13254u },
    { "drum-29-93bpm.json", 422260u, 11359u },
    { "drum-30-96bpm.json", 433619u, 10192u },
    { "drum-31-98bpm.json", 443811u, 19191u },
    { "drum-32-91bpm.json", 463002u, 19198u },
    { "drum-33-90bpm.json", 482200u, 9650u },
    { "drum-34-91bpm.json", 491850u, 13230u },
    { "drum-35-92bpm.json", 505080u, 11379u },
    { "drum-36-92bpm.json", 516459u, 12861u },
    { "drum-37-90bpm.json", 529320u, 13400u },
    { "drum-38-91bpm.json", 542720u, 17925u },
    { "drum-39-89bpm.json", 560645u, 19003u },
    { "drum-40-93bpm.json", 579648u, 13342u },
    { "drum-41-93bpm.json", 592990u, 12914u },
    { "drum-42-92bpm.json", 605904u, 11390u },
    { "drum-43-95bpm.json", 617294u, 12573u },
    { "drum-44-93bpm.json", 629867u, 15095u },
    { "drum-45-93bpm.json", 644962u, 11670u },
    { "drum-46-92bpm.json", 656632u, 13727u },
    { "drum-47-90bpm.json", 670359u, 11907u },
    { "drum-48-91bpm.json", 682266u, 12546u },
    { "drum-49-91bpm.json", 694812u, 13749u },
    { "drum-50-88bpm.json", 708561u, 12964u },
};

inline const PatternEntry basslines[50] = 
{
    { "bass-001-93bpm-Asharp.json", 721525u, 4608u },
    { "bass-002-92bpm-Csharp.json", 726133u, 6690u },
    { "bass-003-90bpm-D.json", 732823u, 9941u },
    { "bass-004-89bpm-A.json", 742764u, 8934u },
    { "bass-005-90bpm-A.json", 751698u, 9059u },
    { "bass-006-96bpm-G.json", 760757u, 6178u },
    { "bass-007-94bpm-Am.json", 766935u, 7111u },
    { "bass-008-95bpm-Bm.json", 774046u, 4496u },
    { "bass-009-95bpm-Bm.json", 778542u, 7644u },
    { "bass-010-90bpm-D.json", 786186u, 9460u },
    { "bass-011-95bpm-A.json", 795646u, 6647u },
    { "bass-012-92bpm-C.json", 802293u, 4185u },
    { "bass-013-95bpm-B.json", 806478u, 4704u },
    { "bass-014-93bpm-Gsharp.json", 811182u, 6916u },
    { "bass-015-98bpm-Em.json", 818098u, 7386u },
    { "bass-016-95bpm-C.json", 825484u, 8132u },
    { "bass-017-96bpm-D.json", 833616u, 4123u },
    { "bass-018-92bpm-G.json", 837739u, 9056u },
    { "bass-019-94bpm-G.json", 846795u, 6170u },
    { "bass-020-92bpm-C.json", 852965u, 62764u },
    { "bass-021-94bpm-C.json", 915729u, 5370u },
    { "bass-022-93bpm-G.json", 921099u, 6851u },
    { "bass-023-91bpm-F.json", 927950u, 6735u },
    { "bass-024-92bpm-E.json", 934685u, 10325u },
    { "bass-025-92bpm-A.json", 945010u, 5583u },
    { "bass-026-94bpm-C.json", 950593u, 6130u },
    { "bass-027-94bpm-D.json", 956723u, 4614u },
    { "bass-028-92bpm-A.json", 961337u, 128867u },
    { "bass-029-90bpm-A.json", 1090204u, 7016u },
    { "bass-030-94bpm-G.json", 1097220u, 2694u },
    { "bass-031-94bpm-D.json", 1099914u, 7254u },
    { "bass-032-94bpm-Am.json", 1107168u, 5099u },
    { "bass-033-95bpm-G.json", 1112267u, 5677u },
    { "bass-034-94bpm-Am.json", 1117944u, 5663u },
    { "bass-035-95bpm-Asharp.json", 1123607u, 4653u },
    { "bass-036-96bpm-B.json", 1128260u, 6857u },
    { "bass-037-96bpm-Bm.json", 1135117u, 5670u },
    { "bass-038-94bpm-A.json", 1140787u, 2663u },
    { "bass-039-95bpm-D.json", 1143450u, 5609u },
    { "bass-040-93bpm-Dm.json", 1149059u, 7363u },
    { "bass-041-93bpm-C.json", 1156422u, 9339u },
    { "bass-042-92bpm-E.json", 1165761u, 5187u },
    { "bass-043-96bpm-A.json", 1170948u, 4461u },
    { "bass-044-94bpm-D.json", 1175409u, 6213u },
    { "bass-045-95bpm-G.json", 1181622u, 6096u },
    { "bass-046-94bpm-Asharp.json", 1187718u, 5639u },
    { "bass-047-94bpm-C.json", 1193357u, 5347u },
    { "bass-048-96bpm-F.json", 1198704u, 2209u },
    { "bass-049-90bpm-A.json", 1200913u, 8514u },
    { "bass-050-94bpm-Em.json", 1209427u, 6638u },
};
} // namespace patterndata
} // namespace boombap
