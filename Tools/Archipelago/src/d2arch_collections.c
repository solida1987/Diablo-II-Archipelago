/* ================================================================
 * D2Archipelago - F1 Collection Page (1.9.0 NEW)
 *
 * Phase 1 scaffolding: data model + persistence stubs + sample data.
 *
 * Sample data only (1 of 32 sets, all 33 runes, all 35 gems, all 10
 * specials authored). The other 31 sets need to be filled in before
 * shipping. See COLL_FILL_TODO comments for spots that still need
 * data authoring.
 *
 * Implementation phases:
 *   1. Data model + persistence       (THIS FILE — Phase 1)
 *   2. Detection scan                 (next)
 *   3. F1 page rendering              (after Phase 2)
 *   4. Goal=3 mode                    (after Phase 3, optional 1.9.1)
 *   5. Polish                         (last)
 *
 * See d2arch_collections.h for the data model and API contract.
 * ================================================================ */
#ifndef D2ARCH_COLLECTIONS_C
#define D2ARCH_COLLECTIONS_C

#include "d2arch_collections.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ----------------------------------------------------------------
 * GLOBAL STATE
 * ---------------------------------------------------------------- */
CollectionState      g_collState;
CollectionGoalConfig g_collGoal;

/* 1.9.0 — apworld slot_data overrides. Set by d2arch_ap.c when the bridge
 * delivers `collection_target_*` and `collection_gold_target` keys. When
 * present, these override the strict-default "all on" config inside
 * Coll_LoadForCharacter. */
/* 1.9.0 — granular per-item Collection target masks. Sourced from the
 * apworld's slot_data (parsed in d2arch_ap.c). Defaults are "all on"
 * so a v1 sidecar with no mask data behaves like the old all-or-nothing
 * toggles. Bit N of each mask corresponds to item N in the catalog
 * (sets 0..31 in g_collSets order, runes 0..32 in g_collRunes order,
 * specials 0..9 in g_collSpecials order). */
uint32_t g_collGoalOverrideSetsMask     = 0xFFFFFFFFu;          /* 32 bits all on */
uint64_t g_collGoalOverrideRunesMask    = (1ull << 33) - 1ull;  /* 33 bits all on */
uint16_t g_collGoalOverrideSpecialsMask = 0x03FFu;              /* 10 bits all on */
uint8_t  g_collGoalOverrideGems         = 1;
uint64_t g_collGoalOverrideGold         = 0;
BOOL     g_collGoalOverridePresent      = FALSE;

/* External helpers from d2arch_helpers.c / d2arch_save.c — match the
 * conventions used by the multi-tab stash and STK modules. */
extern void Log(const char* fmt, ...);
extern void GetArchDir(char* outDir, int outSize);

/* ================================================================
 * STATIC CATALOG — SETS
 * ================================================================
 * Authoring convention:
 *   - One row per set-piece in g_collSetPieces[] in firstSlot order.
 *   - g_collSets[] indexes into g_collSetPieces[] via firstSlot.
 *   - Set order: alphabetical for predictability.
 *
 * COLL_FILL_TODO: 31 of 32 sets still need pieces filled in. Below
 * is Sigon's Complete Steel as a fully-authored example. The other
 * 31 sets follow the same template.
 * ================================================================ */

/* All 32 vanilla 1.10f sets authored from SetItems.txt. Layout
 * follows SetItems.txt row order (Civerb's first, McAuley's last).
 * Sigon's Complete Steel is now at slots 35..40 (10th set), not 0.
 *
 * Drop-source strings group by reqLevel band:
 *   lvl 3-9   = "Normal Act 1+ early TC drops"
 *   lvl 11-21 = "Normal Act 2-5 / NM Act 1+ TC drops"
 *   lvl 22-29 = "Normal Act 5 / Nightmare Act 1+ TC drops"
 *   lvl 32-44 = "Nightmare Act 4+ / Hell drops"
 *   lvl 50+   = "Hell-difficulty boss runs + Hellforge"
 *   class-locked Hell sets = "Hell-difficulty class-monster TCs"
 *
 * invFile is set to "inv<base>" using the base item's invfile pattern.
 * For ghost rendering this works because each set piece's icon is
 * either the base inv graphic or a near-identical recolor; the
 * Phase 5 polish step will refine these to set-specific invfiles
 * if needed. */

const CollectionItem g_collSetPieces[COLL_NUM_SET_PIECES] = {
    /* === Civerb's Vestments (slots 0..2) — Paladin-favored caster === */
    {"lrg", "Civerb's Ward",            "Normal Act 2-3 / NM Act 1+ TC drops",         13, "invlrg"},
    {"amu", "Civerb's Icon",            "Normal Act 2-3 / NM Act 1+ TC drops",         13, "invamu"},
    {"gsc", "Civerb's Cudgel",          "Normal Act 2-3 / NM Act 1+ TC drops",         13, "invgsc"},

    /* === Hsarus' Defense (slots 3..5) — early-game === */
    {"mbt", "Hsarus' Iron Heel",        "Normal Act 1+ early TC drops",                4,  "invmbt"},
    {"buc", "Hsarus' Iron Fist",        "Normal Act 1+ early TC drops",                4,  "invbuc"},
    {"mbl", "Hsarus' Iron Stay",        "Normal Act 1+ early TC drops",                4,  "invmbl"},

    /* === Cleglaw's Brace (slots 6..8) — early-game === */
    {"lsd", "Cleglaw's Tooth",          "Normal Act 1+ early TC drops",                6,  "invlsd"},
    {"sml", "Cleglaw's Claw",           "Normal Act 1+ early TC drops",                6,  "invsml"},
    {"mgl", "Cleglaw's Pincers",        "Normal Act 1+ early TC drops",                6,  "invmgl"},

    /* === Iratha's Finery (slots 9..12) — mid Normal === */
    {"amu", "Iratha's Collar",          "Normal Act 3-4 / NM Act 1+ TC drops",         21, "invamu"},
    {"tgl", "Iratha's Cuff",            "Normal Act 3-4 / NM Act 1+ TC drops",         21, "invtgl"},
    {"crn", "Iratha's Coil",            "Normal Act 3-4 / NM Act 1+ TC drops",         21, "invcrn"},
    {"tbl", "Iratha's Cord",            "Normal Act 3-4 / NM Act 1+ TC drops",         21, "invtbl"},

    /* === Isenhart's Armory (slots 13..16) === */
    {"bsd", "Isenhart's Lightbrand",    "Normal Act 2-3 / NM Act 1+ TC drops",         11, "invbsd"},
    {"gts", "Isenhart's Parry",         "Normal Act 2-3 / NM Act 1+ TC drops",         11, "invgts"},
    {"brs", "Isenhart's Case",          "Normal Act 2-3 / NM Act 1+ TC drops",         11, "invbrs"},
    {"fhl", "Isenhart's Horns",         "Normal Act 2-3 / NM Act 1+ TC drops",         11, "invfhl"},

    /* === Vidala's Rig (slots 17..20) — Amazon-favored === */
    {"lbb", "Vidala's Barb",            "Normal Act 3-4 / NM Act 1+ TC drops",         19, "invlbb"},
    {"tbt", "Vidala's Fetlock",         "Normal Act 3-4 / NM Act 1+ TC drops",         19, "invtbt"},
    {"lea", "Vidala's Ambush",          "Normal Act 3-4 / NM Act 1+ TC drops",         19, "invlea"},
    {"amu", "Vidala's Snare",           "Normal Act 3-4 / NM Act 1+ TC drops",         19, "invamu"},

    /* === Milabrega's Regalia (slots 21..24) — Paladin-favored === */
    {"kit", "Milabrega's Orb",          "Normal Act 4-5 / NM Act 1+ TC drops",         23, "invkit"},
    {"wsp", "Milabrega's Rod",          "Normal Act 4-5 / NM Act 1+ TC drops",         23, "invwsp"},
    {"crn", "Milabrega's Diadem",       "Normal Act 4-5 / NM Act 1+ TC drops",         23, "invcrn"},
    {"aar", "Milabrega's Robe",         "Normal Act 4-5 / NM Act 1+ TC drops",         23, "invaar"},

    /* === Cathan's Traps (slots 25..29) — caster set === */
    {"bst", "Cathan's Rule",            "Normal Act 2-3 / NM Act 1+ TC drops",         15, "invbst"},
    {"chn", "Cathan's Mesh",            "Normal Act 2-3 / NM Act 1+ TC drops",         15, "invchn"},
    {"msk", "Cathan's Visage",          "Normal Act 2-3 / NM Act 1+ TC drops",         15, "invmsk"},
    {"amu", "Cathan's Sigil",           "Normal Act 2-3 / NM Act 1+ TC drops",         15, "invamu"},
    {"rin", "Cathan's Seal",            "Normal Act 2-3 / NM Act 1+ TC drops",         15, "invrin"},

    /* === Tancred's Battlegear (slots 30..34) === */
    {"mpi", "Tancred's Crowbill",       "Normal Act 4-5 / NM Act 1+ TC drops",         27, "invmpi"},
    {"ful", "Tancred's Spine",          "Normal Act 4-5 / NM Act 1+ TC drops",         27, "invful"},
    {"lbt", "Tancred's Hobnails",       "Normal Act 4-5 / NM Act 1+ TC drops",         27, "invlbt"},
    {"amu", "Tancred's Weird",          "Normal Act 4-5 / NM Act 1+ TC drops",         27, "invamu"},
    {"bhm", "Tancred's Skull",          "Normal Act 4-5 / NM Act 1+ TC drops",         27, "invbhm"},

    /* === Sigon's Complete Steel (slots 35..40) === */
    {"hgl", "Sigon's Gage",             "Normal Act 1+ early TC drops",                9,  "invhgl"},
    {"ghm", "Sigon's Visor",            "Normal Act 1+ early TC drops",                9,  "invghm"},
    {"gth", "Sigon's Shelter",          "Normal Act 1+ early TC drops",                9,  "invgth"},
    {"hbt", "Sigon's Sabot",            "Normal Act 1+ early TC drops",                9,  "invhbt"},
    {"hbl", "Sigon's Wrap",             "Normal Act 1+ early TC drops",                9,  "invhbl"},
    {"tow", "Sigon's Guard",            "Normal Act 1+ early TC drops",                9,  "invtow"},

    /* === Infernal Tools (slots 41..43) — early caster === */
    {"cap", "Infernal Cranium",         "Normal Act 1+ early TC drops",                7,  "invcap"},
    {"gwn", "Infernal Torch",           "Normal Act 1+ early TC drops",                7,  "invgwn"},
    {"tbl", "Infernal Sign",            "Normal Act 1+ early TC drops",                7,  "invtbl"},

    /* === Berserker's Garb (slots 44..46) === */
    {"hlm", "Berserker's Headgear",     "Normal Act 1+ early TC drops",                5,  "invhlm"},
    {"spl", "Berserker's Hauberk",      "Normal Act 1+ early TC drops",                5,  "invspl"},
    {"2ax", "Berserker's Hatchet",      "Normal Act 1+ early TC drops",                5,  "inv2ax"},

    /* === Death's Disguise (slots 47..49) === */
    {"lgl", "Death's Hand",             "Normal Act 1+ early TC drops",                8,  "invlgl"},
    {"lbl", "Death's Guard",            "Normal Act 1+ early TC drops",                8,  "invlbl"},
    {"wsd", "Death's Touch",            "Normal Act 1+ early TC drops",                8,  "invwsd"},

    /* === Angelical Raiment (slots 50..53) === */
    {"sbr", "Angelic Sickle",           "Normal Act 2-3 / NM Act 1+ TC drops",         17, "invsbr"},
    {"rng", "Angelic Mantle",           "Normal Act 2-3 / NM Act 1+ TC drops",         17, "invrng"},
    {"rin", "Angelic Halo",             "Normal Act 2-3 / NM Act 1+ TC drops",         17, "invrin"},
    {"amu", "Angelic Wings",            "Normal Act 2-3 / NM Act 1+ TC drops",         17, "invamu"},

    /* === Arctic Gear (slots 54..57) — early Amazon-favored === */
    {"swb", "Arctic Horn",              "Normal Act 1+ early TC drops",                3,  "invswb"},
    {"qui", "Arctic Furs",              "Normal Act 1+ early TC drops",                3,  "invqui"},
    {"vbl", "Arctic Binding",           "Normal Act 1+ early TC drops",                3,  "invvbl"},
    {"tgl", "Arctic Mitts",             "Normal Act 1+ early TC drops",                3,  "invtgl"},

    /* === Arcanna's Tricks (slots 58..61) — caster set === */
    {"amu", "Arcanna's Sign",           "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invamu"},
    {"wst", "Arcanna's Deathwand",      "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invwst"},
    {"skp", "Arcanna's Head",           "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invskp"},
    {"ltp", "Arcanna's Flesh",          "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invltp"},

    /* === Natalya's Odium (slots 62..65) — ASSASSIN-locked === */
    {"xh9", "Natalya's Totem",          "Hell-difficulty Assassin-class TCs",          22, "invxh9"},
    {"7qr", "Natalya's Mark",           "Hell-difficulty Assassin-class TCs",          22, "inv7qr"},
    {"ucl", "Natalya's Shadow",         "Hell-difficulty Assassin-class TCs",          22, "invucl"},
    {"xmb", "Natalya's Soul",           "Hell-difficulty Assassin-class TCs",          22, "invxmb"},

    /* === Aldur's Watchtower (slots 66..69) — DRUID-locked === */
    {"dr8", "Aldur's Stony Gaze",       "Hell-difficulty Druid-class TCs",             29, "invdr8"},
    {"uul", "Aldur's Deception",        "Hell-difficulty Druid-class TCs",             29, "invuul"},
    {"9mt", "Aldur's Gauntlet",         "Hell-difficulty Druid-class TCs",             29, "inv9mt"},
    {"xtb", "Aldur's Advance",          "Hell-difficulty Druid-class TCs",             29, "invxtb"},

    /* === Immortal King (slots 70..75) — BARBARIAN-locked, full 6-piece === */
    {"ba5", "Immortal King's Will",     "Hell-difficulty Barbarian-class TCs",         37, "invba5"},
    {"uar", "Immortal King's Soul Cage","Hell-difficulty Barbarian-class TCs",         37, "invuar"},
    {"zhb", "Immortal King's Detail",   "Hell-difficulty Barbarian-class TCs",         37, "invzhb"},
    {"xhg", "Immortal King's Forge",    "Hell-difficulty Barbarian-class TCs",         37, "invxhg"},
    {"xhb", "Immortal King's Pillar",   "Hell-difficulty Barbarian-class TCs",         37, "invxhb"},
    {"7m7", "Immortal King's Stone Crusher","Hell-difficulty Barbarian-class TCs",     37, "inv7m7"},

    /* === Tal Rasha's Wrappings (slots 76..80) — SORCERESS-locked === */
    {"zmb", "Tal Rasha's Fire-Spun Cloth","Hell-difficulty Sorceress-class TCs",       26, "invzmb"},
    {"amu", "Tal Rasha's Adjudication", "Hell-difficulty Sorceress-class TCs",         26, "invamu"},
    {"oba", "Tal Rasha's Lidless Eye",  "Hell-difficulty Sorceress-class TCs",         26, "invoba"},
    {"uth", "Tal Rasha's Howling Wind", "Hell-difficulty Sorceress-class TCs",         26, "invuth"},
    {"xsk", "Tal Rasha's Horadric Crest","Hell-difficulty Sorceress-class TCs",        26, "invxsk"},

    /* === Griswold's Legacy (slots 81..84) — PALADIN-locked === */
    {"urn", "Griswold's Valor",         "Hell-difficulty Paladin-class TCs",           44, "invurn"},
    {"xar", "Griswold's Heart",         "Hell-difficulty Paladin-class TCs",           44, "invxar"},
    {"7ws", "Griswold's Redemption",    "Hell-difficulty Paladin-class TCs",           44, "inv7ws"},
    {"paf", "Griswold's Honor",         "Hell-difficulty Paladin-class TCs",           44, "invpaf"},

    /* === Trang-Oul's Avatar (slots 85..89) — NECROMANCER-locked === */
    {"uh9", "Trang-Oul's Guise",        "Hell-difficulty Necromancer-class TCs",       32, "invuh9"},
    {"xul", "Trang-Oul's Scales",       "Hell-difficulty Necromancer-class TCs",       32, "invxul"},
    {"ne9", "Trang-Oul's Wing",         "Hell-difficulty Necromancer-class TCs",       32, "invne9"},
    {"xmg", "Trang-Oul's Claws",        "Hell-difficulty Necromancer-class TCs",       32, "invxmg"},
    {"utc", "Trang-Oul's Girth",        "Hell-difficulty Necromancer-class TCs",       32, "invutc"},

    /* === M'avina's Battle Hymn (slots 90..94) — AMAZON-locked === */
    {"ci3", "M'avina's True Sight",     "Hell-difficulty Amazon-class TCs",            21, "invci3"},
    {"uld", "M'avina's Embrace",        "Hell-difficulty Amazon-class TCs",            21, "invuld"},
    {"xtg", "M'avina's Icy Clutch",     "Hell-difficulty Amazon-class TCs",            21, "invxtg"},
    {"zvb", "M'avina's Tenet",          "Hell-difficulty Amazon-class TCs",            21, "invzvb"},
    {"amc", "M'avina's Caster",         "Hell-difficulty Amazon-class TCs",            21, "invamc"},

    /* === The Disciple (slots 95..99) === */
    {"amu", "Telling of Beads",         "Hell-difficulty boss runs",                   39, "invamu"},
    {"ulg", "Laying of Hands",          "Hell-difficulty boss runs",                   39, "invulg"},
    {"xlb", "Rite of Passage",          "Hell-difficulty boss runs",                   39, "invxlb"},
    {"uui", "Spiritual Custodian",      "Hell-difficulty boss runs",                   39, "invuui"},
    {"umc", "Credendum",                "Hell-difficulty boss runs",                   39, "invumc"},

    /* === Heaven's Brethren (slots 100..103) === */
    {"7ma", "Dangoon's Teaching",       "Hell-difficulty boss runs + Hellforge",       55, "inv7ma"},
    {"uts", "Heaven's Taebaek",         "Hell-difficulty boss runs + Hellforge",       55, "invuts"},
    {"xrs", "Haemosu's Adament",        "Hell-difficulty boss runs + Hellforge",       55, "invxrs"},
    {"uhm", "Ondal's Almighty",         "Hell-difficulty boss runs + Hellforge",       55, "invuhm"},

    /* === Orphan's Call (slots 104..107) === */
    {"xhm", "Guillaume's Face",         "Hell-difficulty boss runs",                   41, "invxhm"},
    {"ztb", "Wilhelm's Pride",          "Hell-difficulty boss runs",                   41, "invztb"},
    {"xvg", "Magnus' Skin",             "Hell-difficulty boss runs",                   41, "invxvg"},
    {"xml", "Wihtstan's Guard",         "Hell-difficulty boss runs",                   41, "invxml"},

    /* === Hwanin's Majesty (slots 108..111) === */
    {"xrn", "Hwanin's Splendor",        "Nightmare Act 4+ / Hell drops",               28, "invxrn"},
    {"xcl", "Hwanin's Refuge",          "Nightmare Act 4+ / Hell drops",               28, "invxcl"},
    {"mbl", "Hwanin's Seal",            "Nightmare Act 4+ / Hell drops",               28, "invmbl"},
    {"9vo", "Hwanin's Justice",         "Nightmare Act 4+ / Hell drops",               28, "inv9vo"},

    /* === Sazabi's Grand Tribute (slots 112..114) === */
    {"7ls", "Sazabi's Cobalt Redeemer", "Hell-difficulty boss runs",                   34, "inv7ls"},
    {"upl", "Sazabi's Ghost Liberator", "Hell-difficulty boss runs",                   34, "invupl"},
    {"xhl", "Sazabi's Mental Sheath",   "Hell-difficulty boss runs",                   34, "invxhl"},

    /* === Bul-Kathos' Children (slots 115..116) — BARBARIAN-favored 2-piece === */
    {"7gd", "Bul-Kathos' Sacred Charge","Hell-difficulty boss runs + Hellforge",       50, "inv7gd"},
    {"7wd", "Bul-Kathos' Tribal Guardian","Hell-difficulty boss runs + Hellforge",     50, "inv7wd"},

    /* === Cow King's Leathers (slots 117..119) — Cow Level themed === */
    {"xap", "Cow King's Horns",         "Cow Level (any difficulty) + TC drops",       20, "invxap"},
    {"stu", "Cow King's Hide",          "Cow Level (any difficulty) + TC drops",       20, "invstu"},
    {"vbt", "Cow King's Hoofs",         "Cow Level (any difficulty) + TC drops",       20, "invvbt"},

    /* === Naj's Ancient Set (slots 120..122) — caster set === */
    {"6cs", "Naj's Puzzler",            "Hell-difficulty boss runs",                   43, "inv6cs"},
    {"ult", "Naj's Light Plate",        "Hell-difficulty boss runs",                   43, "invult"},
    {"ci0", "Naj's Circlet",            "Hell-difficulty boss runs",                   43, "invci0"},

    /* === McAuley's Folly / Sander's Folly (slots 123..126) === */
    {"cap", "McAuley's Paragon",        "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invcap"},
    {"vbt", "McAuley's Riprap",         "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invvbt"},
    {"vgl", "McAuley's Taboo",          "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invvgl"},
    {"bwn", "McAuley's Superstition",   "Normal Act 3-4 / NM Act 1+ TC drops",         20, "invbwn"},
};

const CollectionSet g_collSets[COLL_NUM_SETS] = {
    /* { name, classLock, firstSlot, pieceCount, pieces } */
    /* Order matches SetItems.txt row order so debugging cross-references stay clean. */
    {"Civerb's Vestments",           NULL,           0,   3, g_collSetPieces +   0},
    {"Hsarus' Defense",              NULL,           3,   3, g_collSetPieces +   3},
    {"Cleglaw's Brace",              NULL,           6,   3, g_collSetPieces +   6},
    {"Iratha's Finery",              NULL,           9,   4, g_collSetPieces +   9},
    {"Isenhart's Armory",            NULL,          13,   4, g_collSetPieces +  13},
    {"Vidala's Rig",                 NULL,          17,   4, g_collSetPieces +  17},
    {"Milabrega's Regalia",          NULL,          21,   4, g_collSetPieces +  21},
    {"Cathan's Traps",               NULL,          25,   5, g_collSetPieces +  25},
    {"Tancred's Battlegear",         NULL,          30,   5, g_collSetPieces +  30},
    {"Sigon's Complete Steel",       NULL,          35,   6, g_collSetPieces +  35},
    {"Infernal Tools",               NULL,          41,   3, g_collSetPieces +  41},
    {"Berserker's Garb",             NULL,          44,   3, g_collSetPieces +  44},
    {"Death's Disguise",             NULL,          47,   3, g_collSetPieces +  47},
    {"Angelical Raiment",            NULL,          50,   4, g_collSetPieces +  50},
    {"Arctic Gear",                  NULL,          54,   4, g_collSetPieces +  54},
    {"Arcanna's Tricks",             NULL,          58,   4, g_collSetPieces +  58},
    /* --- Class-locked sets (LoD expansion) --- */
    {"Natalya's Odium",              "Assassin",    62,   4, g_collSetPieces +  62},
    {"Aldur's Watchtower",           "Druid",       66,   4, g_collSetPieces +  66},
    {"Immortal King",                "Barbarian",   70,   6, g_collSetPieces +  70},
    {"Tal Rasha's Wrappings",        "Sorceress",   76,   5, g_collSetPieces +  76},
    {"Griswold's Legacy",            "Paladin",     81,   4, g_collSetPieces +  81},
    {"Trang-Oul's Avatar",           "Necromancer", 85,   5, g_collSetPieces +  85},
    {"M'avina's Battle Hymn",        "Amazon",      90,   5, g_collSetPieces +  90},
    /* --- Generic LoD expansion sets --- */
    {"The Disciple",                 NULL,          95,   5, g_collSetPieces +  95},
    {"Heaven's Brethren",            NULL,         100,   4, g_collSetPieces + 100},
    {"Orphan's Call",                NULL,         104,   4, g_collSetPieces + 104},
    {"Hwanin's Majesty",             NULL,         108,   4, g_collSetPieces + 108},
    {"Sazabi's Grand Tribute",       NULL,         112,   3, g_collSetPieces + 112},
    {"Bul-Kathos' Children",         NULL,         115,   2, g_collSetPieces + 115},
    {"Cow King's Leathers",          NULL,         117,   3, g_collSetPieces + 117},
    {"Naj's Ancient Set",            NULL,         120,   3, g_collSetPieces + 120},
    {"McAuley's Folly",              NULL,         123,   4, g_collSetPieces + 123},
};

/* 1.9.0 — exposed lookup used by d2arch_quests.c (REWARD_DROP_SET
 * pre-roll pipeline). Catalog row order matches SetItems.txt; idx
 * 0 = Civerb's Ward, idx 126 = McAuley's Superstition. */
const char* Coll_GetSetPieceName(int idx) {
    if (idx < 0 || idx >= COLL_NUM_SET_PIECES) return "Random Set Item";
    const char* n = g_collSetPieces[idx].displayName;
    return n ? n : "Random Set Item";
}

/* 1.9.0 — exposed lookup for the base item code so the drop helper
 * can pass it straight to QUESTS_CreateItem. Returns NULL on out-of-
 * range so callers can skip the spawn safely. */
const char* Coll_GetSetPieceCode(int idx) {
    if (idx < 0 || idx >= COLL_NUM_SET_PIECES) return NULL;
    return g_collSetPieces[idx].code;
}

/* ================================================================
 * STATIC CATALOG — RUNES (all 33 authored)
 * ================================================================ */
const CollectionItem g_collRunes[COLL_NUM_RUNES] = {
    /*  code     name              dropSource                                    reqLvl  invFile     */
    {  "r01",  "El Rune",         "Any monster lvl 11+",                         11,    "invrEl"   },
    {  "r02",  "Eld Rune",        "Any monster lvl 11+",                         11,    "invrEld"  },
    {  "r03",  "Tir Rune",        "Any monster lvl 13+",                         13,    "invrTir"  },
    {  "r04",  "Nef Rune",        "Any monster lvl 13+",                         13,    "invrNef"  },
    {  "r05",  "Eth Rune",        "Any monster lvl 15+",                         15,    "invrEth"  },
    {  "r06",  "Ith Rune",        "Any monster lvl 15+",                         15,    "invrIth"  },
    {  "r07",  "Tal Rune",        "Any monster lvl 17+",                         17,    "invrTal"  },
    {  "r08",  "Ral Rune",        "Any monster lvl 19+",                         19,    "invrRal"  },
    {  "r09",  "Ort Rune",        "Any monster lvl 21+",                         21,    "invrOrt"  },
    {  "r10",  "Thul Rune",       "Any monster lvl 23+",                         23,    "invrThul" },
    {  "r11",  "Amn Rune",        "Countess (NM Tower L5) + monster lvl 25+",    25,    "invrAmn"  },
    {  "r12",  "Sol Rune",        "Countess (NM) + monster lvl 27+",             27,    "invrSol"  },
    {  "r13",  "Shael Rune",      "Countess (NM) + monster lvl 29+",             29,    "invrShae" },
    {  "r14",  "Dol Rune",        "Countess (NM) + monster lvl 31+",             31,    "invrDol"  },
    {  "r15",  "Hel Rune",        "Countess (NM) + monster lvl 33+",             33,    "invrHel"  },
    {  "r16",  "Io Rune",         "Countess (NM) + monster lvl 35+",             35,    "invrIo"   },
    {  "r17",  "Lum Rune",        "Countess (Hell) + monster lvl 37+",           37,    "invrLum"  },
    {  "r18",  "Ko Rune",         "Countess (Hell) + monster lvl 39+",           39,    "invrKo"   },
    {  "r19",  "Fal Rune",        "Countess (Hell) + monster lvl 41+",           41,    "invrFal"  },
    {  "r20",  "Lem Rune",        "Countess (Hell) + monster lvl 43+",           43,    "invrLem"  },
    {  "r21",  "Pul Rune",        "Hell-difficulty bosses + Hellforge",          45,    "invrPul"  },
    {  "r22",  "Um Rune",         "Hell-difficulty bosses + Hellforge",          47,    "invrUm"   },
    {  "r23",  "Mal Rune",        "Hell-difficulty bosses + Hellforge",          49,    "invrMal"  },
    {  "r24",  "Ist Rune",        "Hell-difficulty bosses + Hellforge",          51,    "invrIst"  },
    {  "r25",  "Gul Rune",        "Hell-difficulty bosses + Hellforge",          53,    "invrGul"  },
    {  "r26",  "Vex Rune",        "Hell-difficulty bosses + Hellforge",          55,    "invrVex"  },
    {  "r27",  "Ohm Rune",        "Hell-difficulty bosses + Hellforge",          57,    "invrOhm"  },
    {  "r28",  "Lo Rune",         "Hell-difficulty bosses + Hellforge",          59,    "invrLo"   },
    {  "r29",  "Sur Rune",        "Hell-difficulty bosses + Hellforge",          61,    "invrSur"  },
    {  "r30",  "Ber Rune",        "Hell-difficulty bosses + Hellforge",          63,    "invrBer"  },
    {  "r31",  "Jah Rune",        "Hell Andariel/Mephisto/Diablo/Baal",          65,    "invrJah"  },
    {  "r32",  "Cham Rune",       "Hell Andariel/Mephisto/Diablo/Baal",          67,    "invrCham" },
    {  "r33",  "Zod Rune",        "Hell Andariel/Mephisto/Diablo/Baal",          69,    "invrZod"  },
};

/* ================================================================
 * STATIC CATALOG — GEMS (all 35 authored: 7 colors × 5 grades)
 * ================================================================ */
const CollectionItem g_collGems[COLL_NUM_GEMS] = {
    /* Amethyst */
    {"gcv", "Chipped Amethyst",   "Any monster lvl 1+",   1,  "invgcv"},
    {"gfv", "Flawed Amethyst",    "Any monster lvl 12+",  12, "invgfv"},
    {"gsv", "Amethyst",           "Any monster lvl 22+",  22, "invgsv"},
    {"gzv", "Flawless Amethyst",  "Any monster lvl 30+",  30, "invgzv"},
    {"gpv", "Perfect Amethyst",   "Hell-difficulty drops + Cube upgrade", 40, "invgpv"},
    /* Topaz */
    {"gcy", "Chipped Topaz",      "Any monster lvl 1+",   1,  "invgcy"},
    {"gfy", "Flawed Topaz",       "Any monster lvl 12+",  12, "invgfy"},
    {"gsy", "Topaz",              "Any monster lvl 22+",  22, "invgsy"},
    {"gly", "Flawless Topaz",     "Any monster lvl 30+",  30, "invgly"},
    {"gpy", "Perfect Topaz",      "Hell-difficulty drops + Cube upgrade", 40, "invgpy"},
    /* Sapphire */
    {"gcb", "Chipped Sapphire",   "Any monster lvl 1+",   1,  "invgcb"},
    {"gfb", "Flawed Sapphire",    "Any monster lvl 12+",  12, "invgfb"},
    {"gsb", "Sapphire",           "Any monster lvl 22+",  22, "invgsb"},
    {"glb", "Flawless Sapphire",  "Any monster lvl 30+",  30, "invglb"},
    {"gpb", "Perfect Sapphire",   "Hell-difficulty drops + Cube upgrade", 40, "invgpb"},
    /* Emerald */
    {"gcg", "Chipped Emerald",    "Any monster lvl 1+",   1,  "invgcg"},
    {"gfg", "Flawed Emerald",     "Any monster lvl 12+",  12, "invgfg"},
    {"gsg", "Emerald",            "Any monster lvl 22+",  22, "invgsg"},
    {"glg", "Flawless Emerald",   "Any monster lvl 30+",  30, "invglg"},
    {"gpg", "Perfect Emerald",    "Hell-difficulty drops + Cube upgrade", 40, "invgpg"},
    /* Ruby */
    {"gcr", "Chipped Ruby",       "Any monster lvl 1+",   1,  "invgcr"},
    {"gfr", "Flawed Ruby",        "Any monster lvl 12+",  12, "invgfr"},
    {"gsr", "Ruby",               "Any monster lvl 22+",  22, "invgsr"},
    {"glr", "Flawless Ruby",      "Any monster lvl 30+",  30, "invglr"},
    {"gpr", "Perfect Ruby",       "Hell-difficulty drops + Cube upgrade", 40, "invgpr"},
    /* Diamond */
    {"gcw", "Chipped Diamond",    "Any monster lvl 1+",   1,  "invgcw"},
    {"gfw", "Flawed Diamond",     "Any monster lvl 12+",  12, "invgfw"},
    {"gsw", "Diamond",            "Any monster lvl 22+",  22, "invgsw"},
    {"glw", "Flawless Diamond",   "Any monster lvl 30+",  30, "invglw"},
    {"gpw", "Perfect Diamond",    "Hell-difficulty drops + Cube upgrade", 40, "invgpw"},
    /* Skull */
    {"skc", "Chipped Skull",      "Any monster lvl 1+",   1,  "invskc"},
    {"skf", "Flawed Skull",       "Any monster lvl 12+",  12, "invskf"},
    {"sku", "Skull",              "Any monster lvl 22+",  22, "invsku"},
    {"skl", "Flawless Skull",     "Any monster lvl 30+",  30, "invskl"},
    {"skz", "Perfect Skull",      "Hell-difficulty drops + Cube upgrade", 40, "invskz"},
};

/* ================================================================
 * STATIC CATALOG — SPECIAL ITEMS (10 slots)
 * ================================================================ */
const CollectionItem g_collSpecials[COLL_NUM_SPECIALS] = {
    {"pk1", "Key of Terror",         "Hell event/cow loot table — random drop",      60, "invpk1"},
    {"pk2", "Key of Hate",           "Hell event/cow loot table — random drop",      60, "invpk2"},
    {"pk3", "Key of Destruction",    "Hell event/cow loot table — random drop",      60, "invpk3"},
    {"mbr", "Mephisto's Brain",      "Hell Mephisto (Durance L3)",                   80, "invmbr"},
    {"dhn", "Diablo's Horn",         "Hell Diablo (Chaos Sanctuary)",                80, "invdhn"},
    {"bey", "Baal's Eye",            "Hell Baal (Throne of Destruction)",            80, "invbey"},
    {"tes", "Twisted Essence of Suffering",  "Hell Andariel/Duriel quest drop",      80, "invtes"},
    {"ceh", "Charged Essence of Hatred",     "Hell Mephisto quest drop",             80, "invceh"},
    {"bet", "Burning Essence of Terror",     "Hell Diablo quest drop",               80, "invbet"},
    {"cm2", "Hellfire Torch",        "Pandemonium Event finale (3 organs cubed)",    75, "invcm2"},
};

/* COLL_FILL_TODO: Token of Absolution (`tof`) is implicit in the
 * STK_C tab but not yet in g_collSpecials. Decide if `tof` should
 * be slot 11 (bumping COLL_NUM_SPECIALS to 11) or omitted because
 * it's a consumable. */

/* ================================================================
 * INITIALIZATION
 * ================================================================ */
BOOL Coll_Init(void) {
    /* Sanity-check FILLED catalog entries at startup. Unfilled-set
     * placeholders (pieceCount=0 or name=NULL) are tolerated so we
     * can ship Phase 1 with only Sigon's authored.
     *
     * If a filled entry overflows the piece array, fail loud — that
     * indicates a data-authoring bug that would corrupt the F1 page. */
    for (int i = 0; i < COLL_NUM_SETS; i++) {
        if (!g_collSets[i].name || g_collSets[i].pieceCount == 0)
            continue;  /* unfilled placeholder — skip */
        if (g_collSets[i].firstSlot + g_collSets[i].pieceCount > COLL_NUM_SET_PIECES) {
            Log("Coll_Init: g_collSets[%d] '%s' overflows piece array\n",
                 i, g_collSets[i].name);
            return FALSE;
        }
    }
    /* Zero the runtime state. Real values come from sidecar load. */
    memset(&g_collState, 0, sizeof(g_collState));
    g_collState.magic   = COLL_FILE_MAGIC;
    g_collState.version = COLL_FILE_VERSION;
    memset(&g_collGoal, 0, sizeof(g_collGoal));
    Log("Coll_Init: catalog OK (%d sets, %d set-pieces, %d runes, %d gems, %d specials)\n",
         COLL_NUM_SETS, COLL_NUM_SET_PIECES, COLL_NUM_RUNES, COLL_NUM_GEMS, COLL_NUM_SPECIALS);
    return TRUE;
}

/* ================================================================
 * SIDECAR I/O
 * ================================================================
 * File format: just dumps CollectionState as a binary blob. Magic +
 * version on the front, fixed-size body, future-proof reserved tail.
 * ================================================================ */
static void Coll_BuildSidecarPath(const char* charName, char* outPath, size_t outSize) {
    char dir[MAX_PATH];
    /* 1.9.0: per-character files now live in Game/Save/ alongside .d2s
     * instead of being mixed into Game/Archipelago/. */
    GetCharFileDir(dir, MAX_PATH);
    _snprintf(outPath, outSize - 1, "%sap_collections_%s.dat", dir, charName);
    outPath[outSize - 1] = '\0';
}

/* Auto-deselect any set whose classLock doesn't match the current
 * player's class. The 7 class-locked sets in vanilla:
 *   Natalya's Odium       → Assassin
 *   Aldur's Watchtower    → Druid
 *   Immortal King         → Barbarian
 *   Tal Rasha's Wrappings → Sorceress
 *   Griswold's Legacy     → Paladin
 *   Trang-Oul's Avatar    → Necromancer
 *   M'avina's Battle Hymn → Amazon
 *
 * Class IDs from D2 1.10f:
 *   0=Amazon, 1=Sorceress, 2=Necromancer, 3=Paladin,
 *   4=Barbarian, 5=Druid, 6=Assassin
 *
 * Called from Coll_DefaultGoalAllOn so a fresh Goal=Collection
 * character automatically excludes the 6 sets they can't equip.
 * The user keeps the option of manually re-enabling a class-locked
 * set via INI override if they want a "play Sorc, collect for the
 * museum" run. */
static void Coll_AutoDeselectClassLockedSets(void) {
    extern int GetPlayerClass(void);
    int cls = -1;
    __try { cls = GetPlayerClass(); } __except(EXCEPTION_EXECUTE_HANDLER) { cls = -1; }
    if (cls < 0 || cls > 6) return;  /* unknown — leave as-is */

    static const char* k_clsName[7] = {
        "Amazon", "Sorceress", "Necromancer", "Paladin",
        "Barbarian", "Druid", "Assassin"
    };
    const char* myClass = k_clsName[cls];

    for (int i = 0; i < COLL_NUM_SETS; i++) {
        const char* lock = g_collSets[i].classLock;
        if (lock && strcmp(lock, myClass) != 0) {
            g_collGoal.setsTargeted[i] = 0;
        }
    }
}

/* Default Goal=3 config: every set + all runes + all gems + every
 * special must be collected. Gold target stays 0 (not part of goal
 * unless explicitly set). User can override via INI in v1; future
 * v2 adds a title-screen sub-config UI. */
static void Coll_DefaultGoalAllOn(void) {
    memset(&g_collGoal, 0, sizeof(g_collGoal));
    for (int i = 0; i < COLL_NUM_SETS;     i++) g_collGoal.setsTargeted[i]     = 1;
    for (int i = 0; i < COLL_NUM_RUNES;    i++) g_collGoal.runesTargeted[i]    = 1;
    for (int i = 0; i < COLL_NUM_SPECIALS; i++) g_collGoal.specialsTargeted[i] = 1;
    g_collGoal.allRunesTargeted = 1;  /* legacy mirror */
    g_collGoal.allGemsTargeted  = 1;
    g_collGoal.goldTarget = 0;
    /* 1.9.0 — auto-deselect sets that the current player class can't
     * use, so a Sorc on Goal=Collection isn't required to collect
     * Trang-Oul's, IK, etc. */
    Coll_AutoDeselectClassLockedSets();
}

static BOOL Coll_GoalConfigIsZero(void) {
    for (int i = 0; i < COLL_NUM_SETS; i++)
        if (g_collGoal.setsTargeted[i]) return FALSE;
    for (int i = 0; i < COLL_NUM_RUNES; i++)
        if (g_collGoal.runesTargeted[i]) return FALSE;
    for (int i = 0; i < COLL_NUM_SPECIALS; i++)
        if (g_collGoal.specialsTargeted[i]) return FALSE;
    if (g_collGoal.allRunesTargeted) return FALSE;
    if (g_collGoal.allGemsTargeted)  return FALSE;
    if (g_collGoal.goldTarget)       return FALSE;
    return TRUE;
}

/* 1.9.0 — set TRUE by Coll_LoadForCharacter when a v1 sidecar was
 * loaded. The next inv-scan tick runs Coll_ScanPlayerHoldings_Grace
 * (legacy behavior) once to grandfather pre-update inventory items,
 * then clears the flag. The sidecar was already in-memory upgraded
 * to v2 at load time, so the next save commits the migration. */
static BOOL s_collGracePending = FALSE;

/* Forward decl — body lives in the DETECTION SCAN section below. We
 * only need the symbol earlier so Coll_LoadForCharacter can reset
 * the per-game GUID classification table on every character load. */
static void Coll_GuidTblReset(void);

void Coll_LoadForCharacter(const char* charName) {
    char path[MAX_PATH];
    Coll_BuildSidecarPath(charName, path, sizeof(path));

    /* Reset memory first — fresh state is the safe default. */
    memset(&g_collState, 0, sizeof(g_collState));
    memset(&g_collGoal,  0, sizeof(g_collGoal));
    g_collState.magic   = COLL_FILE_MAGIC;
    g_collState.version = COLL_FILE_VERSION;

    /* Per-character GUID table starts empty. dwUnitId values aren't
     * stable across games anyway, so persisting them would be wrong. */
    Coll_GuidTblReset();
    s_collGracePending = FALSE;

    FILE* f = fopen(path, "rb");
    if (!f) {
        Log("Coll_Load: no sidecar for '%s' — fresh state (strict two-flag mode)\n",
            charName);
    } else {
        /* Sidecar layout:
         *   [CollectionState]                  ~size = sizeof(CollectionState)
         *   [CollectionGoalConfig] (optional, present if file is large enough)
         */
        CollectionState onDisk;
        size_t got = fread(&onDisk, 1, sizeof(onDisk), f);
        if (got == sizeof(onDisk) && onDisk.magic == COLL_FILE_MAGIC &&
            onDisk.version <= COLL_FILE_VERSION) {
            uint32_t loadedVersion = onDisk.version;
            g_collState = onDisk;
            /* Force version forward so the next save persists the
             * migration. Already-marked slots are preserved verbatim. */
            g_collState.version = COLL_FILE_VERSION;

            if (loadedVersion < 2) {
                /* Pre-two-flag char — schedule a one-time grace scan
                 * so items the player legitimately had at upgrade time
                 * stay credited even though we never observed them on
                 * the ground. */
                s_collGracePending = TRUE;
                Log("Coll_Load: '%s' v%u → v2 — grace scan scheduled\n",
                    charName, loadedVersion);
            }

            /* Try to read goal config — older sidecars may not have it */
            CollectionGoalConfig goalDisk;
            size_t gotGoal = fread(&goalDisk, 1, sizeof(goalDisk), f);
            if (gotGoal == sizeof(goalDisk)) {
                g_collGoal = goalDisk;
                Log("Coll_Load: '%s' OK (gold=%llu, goal config restored)\n",
                    charName, (unsigned long long)g_collState.goldEarned);
            } else {
                Log("Coll_Load: '%s' OK (gold=%llu, no goal config — using defaults)\n",
                    charName, (unsigned long long)g_collState.goldEarned);
            }
        } else {
            Log("Coll_Load: corrupt sidecar for '%s' (got=%zu magic=0x%X v=%u)\n",
                 charName, got, onDisk.magic, onDisk.version);
        }
        fclose(f);
    }

    /* If Goal=3 is selected and the goal config was never set, default
     * to "all targets on" — strictest default, matches feedback_settings_isolation
     * "freeze at character creation" pattern. The defaulted values get
     * persisted on the next save so subsequent loads reuse them.
     * g_apGoal is a file-scope static from d2arch_input.c (in scope
     * via unity build because input.c is included before this file). */
    if (g_apGoal == 3 && Coll_GoalConfigIsZero()) {
        Coll_DefaultGoalAllOn();
        /* 1.9.0 — apworld slot_data carries 6 bitmask integers (sets×2,
         * runes×3, specials×1) plus the gems boolean and gold target.
         * Each bit corresponds to one item in the catalog. We apply
         * the masks bit-by-bit on top of the all-on default so any
         * toggle the user turned OFF in their YAML clears the
         * corresponding target. */
        if (g_collGoalOverridePresent) {
            for (int i = 0; i < COLL_NUM_SETS; i++) {
                if (!(g_collGoalOverrideSetsMask & (1u << i)))
                    g_collGoal.setsTargeted[i] = 0;
            }
            for (int i = 0; i < COLL_NUM_RUNES; i++) {
                if (!(g_collGoalOverrideRunesMask & ((uint64_t)1 << i)))
                    g_collGoal.runesTargeted[i] = 0;
            }
            for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
                if (!(g_collGoalOverrideSpecialsMask & (1u << i)))
                    g_collGoal.specialsTargeted[i] = 0;
            }
            if (!g_collGoalOverrideGems) g_collGoal.allGemsTargeted = 0;
            g_collGoal.goldTarget = g_collGoalOverrideGold;
            /* Update legacy allRunesTargeted to mirror granular state:
             * TRUE iff every rune is targeted. Rendering still uses
             * setsTargeted[]/runesTargeted[]/specialsTargeted[] arrays
             * for actual gating; allRunesTargeted is just for
             * pre-1.9.0 sidecar compat. */
            uint8_t allRunes = 1;
            for (int i = 0; i < COLL_NUM_RUNES; i++)
                if (!g_collGoal.runesTargeted[i]) { allRunes = 0; break; }
            g_collGoal.allRunesTargeted = allRunes;
            Log("Coll_Load: Goal=3 granular masks applied — sets=0x%08X runes=0x%llX specials=0x%X gems=%d gold=%llu\n",
                g_collGoalOverrideSetsMask,
                (unsigned long long)g_collGoalOverrideRunesMask,
                (unsigned)g_collGoalOverrideSpecialsMask,
                (int)g_collGoalOverrideGems,
                (unsigned long long)g_collGoalOverrideGold);
        } else {
            Log("Coll_Load: Goal=3 detected with empty config — defaulting to all-targets-on (no slot_data override)\n");
        }
    }
}

void Coll_SaveForCharacter(const char* charName) {
    char path[MAX_PATH];
    Coll_BuildSidecarPath(charName, path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        Log("Coll_Save: cannot open '%s' for write\n", path);
        return;
    }
    fwrite(&g_collState, 1, sizeof(g_collState), f);
    fwrite(&g_collGoal,  1, sizeof(g_collGoal),  f);  /* 1.9.0: persist goal config */
    fclose(f);
}

void Coll_ResetOnPlayerGone(void) {
    memset(&g_collState, 0, sizeof(g_collState));
    memset(&g_collGoal, 0, sizeof(g_collGoal));
    g_collState.magic   = COLL_FILE_MAGIC;
    g_collState.version = COLL_FILE_VERSION;
    /* Forward decl — Coll_ResetCelCache lives below in this file. */
    extern void Coll_ResetCelCache(void);
    Coll_ResetCelCache();
}

/* ================================================================
 * SLOT ACCESS
 * ================================================================ */
BOOL Coll_IsSlotCollected(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= COLL_NUM_SLOTS) return FALSE;
    return (g_collState.flags[slotIdx >> 3] & (1u << (slotIdx & 7))) != 0;
}

/* 1.9.0 Phase 5.3 — gold-flash celebration. Per-slot GetTickCount()
 * snapshot at the moment Coll_MarkSlotCollected fires for the first
 * time. The cell render path checks elapsed time against
 * COLL_GOLD_FLASH_MS and draws a pulsing gold border while the timer
 * is active. 0 = no pending flash. Not persisted (in-memory only —
 * the celebration is for the live "just collected!" moment). */
#define COLL_GOLD_FLASH_MS  1500   /* total visible duration */
static DWORD s_collJustCollectedTick[COLL_NUM_SLOTS];

/* 1.9.0 — Collection AP-check firing. When a targeted collection
 * slot is marked, we either fire an AP location check (multiworld
 * mode) or grant a local reward (standalone mode).
 *
 * Location ID layout — must match apworld locations.py:
 *   COLL_LOC_BASE + 0   .. +31    = 32 set-completed checks
 *   COLL_LOC_BASE + 32  .. +64    = 33 rune-collected checks
 *   COLL_LOC_BASE + 65  .. +99    = 35 gem-collected checks
 *   COLL_LOC_BASE + 100 .. +109   = 10 special-collected checks
 *
 * `s_collCheckFired[]` is a 110-bit per-character flag set persisted
 * in g_collState.reserved[] so reloading the char doesn't double-fire
 * checks for items already collected on a previous session. */
#define COLL_LOC_BASE          50000
#define COLL_NUM_CHECK_LOCS    110     /* 32 + 33 + 35 + 10 */

static uint8_t s_collCheckFired[(COLL_NUM_CHECK_LOCS + 7) / 8];

static BOOL Coll_CheckAlreadyFired(int locOffset) {
    if (locOffset < 0 || locOffset >= COLL_NUM_CHECK_LOCS) return TRUE;
    return (s_collCheckFired[locOffset >> 3] & (1u << (locOffset & 7))) != 0;
}
static void Coll_MarkCheckFired(int locOffset) {
    if (locOffset < 0 || locOffset >= COLL_NUM_CHECK_LOCS) return;
    s_collCheckFired[locOffset >> 3] |= (1u << (locOffset & 7));
}

/* Map a catalog slotIdx to a collection location-offset (0..109).
 * For sets, returns -1 because sets fire when the WHOLE set completes,
 * not per-piece — handled separately in Coll_MaybeFireSetCheck. */
static int Coll_SlotToLocOffset(int slotIdx) {
    if (slotIdx >= COLL_SLOT_RUNES_BASE && slotIdx < COLL_SLOT_GEMS_BASE) {
        /* Rune slot. */
        return 32 + (slotIdx - COLL_SLOT_RUNES_BASE);
    }
    if (slotIdx >= COLL_SLOT_GEMS_BASE && slotIdx < COLL_SLOT_SPECIALS_BASE) {
        /* Gem slot. */
        return 65 + (slotIdx - COLL_SLOT_GEMS_BASE);
    }
    if (slotIdx >= COLL_SLOT_SPECIALS_BASE && slotIdx < COLL_NUM_SLOTS) {
        return 100 + (slotIdx - COLL_SLOT_SPECIALS_BASE);
    }
    return -1;  /* set piece — handled separately */
}

/* Find which set a piece slot belongs to. Returns set index (0..31)
 * or -1 if not a set piece. */
static int Coll_SetIdxFromPieceSlot(int slotIdx) {
    if (slotIdx < COLL_SLOT_SETS_BASE || slotIdx >= COLL_SLOT_RUNES_BASE) return -1;
    int pieceIdx = slotIdx - COLL_SLOT_SETS_BASE;
    for (int i = 0; i < COLL_NUM_SETS; i++) {
        const CollectionSet* s = &g_collSets[i];
        if (pieceIdx >= s->firstSlot && pieceIdx < s->firstSlot + s->pieceCount) {
            return i;
        }
    }
    return -1;
}

/* TRUE if every piece of the given set is currently collected. */
static BOOL Coll_IsSetComplete(int setIdx) {
    if (setIdx < 0 || setIdx >= COLL_NUM_SETS) return FALSE;
    const CollectionSet* s = &g_collSets[setIdx];
    for (int p = 0; p < s->pieceCount; p++) {
        if (!Coll_IsSlotCollected(COLL_SLOT_SETS_BASE + s->firstSlot + p))
            return FALSE;
    }
    return TRUE;
}

/* g_pendingRewardGold is `static int` in d2arch_skilltree.c. In the
 * unity build it's visible from its declaration onwards in the same
 * translation unit, so collections.c (which is included later) can
 * reference it by name without an extern declaration. The standalone
 * Collection-check reward path bumps it to queue gold for the next
 * tick (see Coll_FireCheckOffset below). */

/* Fire a collection-check. AP mode: the location ID gets persisted
 * into our s_collCheckFired[] flagset which WriteChecksFile then
 * writes to disk for the bridge. Standalone mode: grant a small
 * gold reward (500g flat — same scale as quest fillers) so each
 * collected item still feels rewarding without requiring a full
 * filler-pool implementation in this release. */
static void Coll_FireCheckOffset(int locOffset, const char* tagForLog) {
    if (locOffset < 0 || locOffset >= COLL_NUM_CHECK_LOCS) return;
    if (Coll_CheckAlreadyFired(locOffset)) return;
    Coll_MarkCheckFired(locOffset);

    int locId = COLL_LOC_BASE + locOffset;
    extern BOOL g_apConnected;
    if (g_apConnected) {
        Log("COLL_CHECK: AP fire — %s -> locId=%d\n",
            tagForLog ? tagForLog : "?", locId);
        /* WriteChecksFile is called from the existing save pipeline.
         * It will emit our flagset on next flush (see d2arch_save.c). */
    } else {
        /* Standalone — queue 500g for the next tick to apply via
         * the existing reward pipeline. Avoids file-I/O during the
         * collection-mark moment. */
        g_pendingRewardGold += 500;
        Log("COLL_CHECK: standalone — %s -> +500g pending (locId=%d)\n",
            tagForLog ? tagForLog : "?", locId);
    }
}

/* Public — called from Coll_MarkSlotCollected after the slot bit is
 * set. Routes to the right check based on slot type, applying the
 * targeting filter so untargeted items don't fire. */
static void Coll_MaybeFireCheckForSlot(int slotIdx) {
    /* Check targeting first — un-targeted items don't fire checks. */
    if (slotIdx >= COLL_SLOT_SETS_BASE && slotIdx < COLL_SLOT_RUNES_BASE) {
        int setIdx = Coll_SetIdxFromPieceSlot(slotIdx);
        if (setIdx < 0) return;
        if (!g_collGoal.setsTargeted[setIdx]) return;
        /* Fire only when the WHOLE set just completed. */
        if (!Coll_IsSetComplete(setIdx)) return;
        Coll_FireCheckOffset(setIdx, g_collSets[setIdx].name);
        return;
    }
    if (slotIdx >= COLL_SLOT_RUNES_BASE && slotIdx < COLL_SLOT_GEMS_BASE) {
        int runeIdx = slotIdx - COLL_SLOT_RUNES_BASE;
        if (!g_collGoal.runesTargeted[runeIdx]) return;
        Coll_FireCheckOffset(32 + runeIdx, g_collRunes[runeIdx].displayName);
        return;
    }
    if (slotIdx >= COLL_SLOT_GEMS_BASE && slotIdx < COLL_SLOT_SPECIALS_BASE) {
        int gemIdx = slotIdx - COLL_SLOT_GEMS_BASE;
        if (!g_collGoal.allGemsTargeted) return;
        Coll_FireCheckOffset(65 + gemIdx, g_collGems[gemIdx].displayName);
        return;
    }
    if (slotIdx >= COLL_SLOT_SPECIALS_BASE && slotIdx < COLL_NUM_SLOTS) {
        int specIdx = slotIdx - COLL_SLOT_SPECIALS_BASE;
        if (!g_collGoal.specialsTargeted[specIdx]) return;
        Coll_FireCheckOffset(100 + specIdx, g_collSpecials[specIdx].displayName);
        return;
    }
}

/* Public accessor — d2arch_save.c needs to read this flagset to emit
 * checks. Returns TRUE if the given offset has been fired. */
BOOL Coll_IsCheckFired(int locOffset) {
    return Coll_CheckAlreadyFired(locOffset);
}

/* Forward decl — Coll_GetItemForSlot is defined in the rendering
 * section below. The filter helpers need it earlier. */
static const CollectionItem* Coll_GetItemForSlot(int slotIdx);

/* 1.9.0 Phase 5.1 — search/filter box. When `s_collSearchBuf` is
 * non-empty, cell render paths dim out items whose displayName
 * doesn't contain the typed substring (case-insensitive).
 * `s_collSearchFocus` is the keyboard-input gate — set to TRUE when
 * the search box is clicked, cleared on Escape / Enter / click
 * elsewhere. The buffer is exported so WndProc's WM_CHAR handler
 * can append to it from d2arch_main.c. */
char s_collSearchBuf[40] = {0};
BOOL s_collSearchFocus    = FALSE;

/* Case-insensitive substring match. Returns TRUE if needle is empty
 * or appears anywhere in haystack (or haystack is NULL). */
static BOOL Coll_StrIContains(const char* haystack, const char* needle) {
    if (!needle || !needle[0]) return TRUE;
    if (!haystack || !haystack[0]) return FALSE;
    int hLen = (int)strlen(haystack);
    int nLen = (int)strlen(needle);
    if (nLen > hLen) return FALSE;
    for (int i = 0; i <= hLen - nLen; i++) {
        BOOL match = TRUE;
        for (int j = 0; j < nLen; j++) {
            char hc = haystack[i + j];
            char nc = needle[j];
            if (hc >= 'a' && hc <= 'z') hc = (char)(hc - 32);
            if (nc >= 'a' && nc <= 'z') nc = (char)(nc - 32);
            if (hc != nc) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

/* Returns TRUE if this catalog slot passes the active search filter.
 * Empty filter = always TRUE. Match is case-insensitive substring on
 * the slot's displayName. */
static BOOL Coll_SlotMatchesFilter(int slotIdx) {
    if (s_collSearchBuf[0] == 0) return TRUE;
    const CollectionItem* item = Coll_GetItemForSlot(slotIdx);
    if (!item) return FALSE;
    return Coll_StrIContains(item->displayName, s_collSearchBuf);
}

/* For sets — match against the set name OR any of its piece names. */
static BOOL Coll_SetMatchesFilter(int setIdx) {
    if (s_collSearchBuf[0] == 0) return TRUE;
    if (setIdx < 0 || setIdx >= COLL_NUM_SETS) return FALSE;
    const CollectionSet* s = &g_collSets[setIdx];
    if (Coll_StrIContains(s->name, s_collSearchBuf)) return TRUE;
    for (int p = 0; p < s->pieceCount; p++) {
        if (Coll_StrIContains(s->pieces[p].displayName, s_collSearchBuf))
            return TRUE;
    }
    return FALSE;
}

void Coll_MarkSlotCollected(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= COLL_NUM_SLOTS) return;
    if (Coll_IsSlotCollected(slotIdx)) return;  /* already flagged */

    g_collState.flags[slotIdx >> 3] |= (1u << (slotIdx & 7));
    g_collState.timestamps[slotIdx]  = (uint32_t)time(NULL);

    /* 1.9.0 Phase 5.3 — start the gold-flash celebration timer. */
    s_collJustCollectedTick[slotIdx] = GetTickCount();

    /* 1.9.0 — Collection AP-check / standalone reward. Only fires when
     * Goal=Collection (g_apGoal == 3) AND the slot is in the active
     * targeting set. AP mode flags the location for WriteChecksFile;
     * standalone mode queues a 500g reward via g_pendingRewardGold. */
    if (g_apGoal == 3) {
        Coll_MaybeFireCheckForSlot(slotIdx);
    }

    /* 1.9.0 NEW — Bonus check: set piece pickup. Independent of goal
     * mode; gated only by check_set_pickups slot_data toggle. The
     * apworld places one location per set piece (127 total) and the
     * pickup pipeline fires the matching index once per first pickup. */
    if (slotIdx >= COLL_SLOT_SETS_BASE && slotIdx < COLL_SLOT_SETS_BASE + COLL_NUM_SET_PIECES) {
        int pieceIdx = slotIdx - COLL_SLOT_SETS_BASE;
        extern BOOL Bonus_OnSetPiecePickup(int pieceIdx);
        Bonus_OnSetPiecePickup(pieceIdx);
    }
}

/* 1.9.0 — Public accessor used by d2arch_bonuschecks.c set-pickup
 * gating (we want to skip pickup checks for sets the player toggled
 * off in their YAML's collect_set_* options). g_collGoal.setsTargeted
 * is populated by Coll_LoadForCharacter from slot_data masks. */
uint8_t Coll_IsSetTargeted(int setIdx) {
    if (setIdx < 0 || setIdx >= COLL_NUM_SETS) return 1;  /* default permissive */
    return g_collGoal.setsTargeted[setIdx];
}

int Coll_FindSlotByCode(const char* code4) {
    if (!code4) return -1;
    /* Set pieces */
    for (int i = 0; i < COLL_NUM_SET_PIECES; i++) {
        if (strncmp(g_collSetPieces[i].code, code4, 4) == 0)
            return COLL_SLOT_SETS_BASE + i;
    }
    /* Runes */
    for (int i = 0; i < COLL_NUM_RUNES; i++) {
        if (strncmp(g_collRunes[i].code, code4, 4) == 0)
            return COLL_SLOT_RUNES_BASE + i;
    }
    /* Gems */
    for (int i = 0; i < COLL_NUM_GEMS; i++) {
        if (strncmp(g_collGems[i].code, code4, 4) == 0)
            return COLL_SLOT_GEMS_BASE + i;
    }
    /* Specials */
    for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
        if (strncmp(g_collSpecials[i].code, code4, 4) == 0)
            return COLL_SLOT_SPECIALS_BASE + i;
    }
    return -1;
}

/* ================================================================
 * DETECTION SCAN — Phase 2
 * ================================================================ */

/* D2 1.10f layout offsets (verified against D2MOO headers).
 *   pItem + 0x04 = dwClassId       (uint32, row in Misc/Weapons/Armor)
 *   pItem + 0x14 = pItemData       (D2ItemDataStrc*)
 *   pItem + 0x60 = pInventory      (D2InventoryStrc*, items only)
 *   pItemData + 0x00 = dwQualityNo (1=lowq, 4=magic, 5=set, 6=rare, 7=unique)
 *   pItemData + 0x28 = dwFileIndex (set: SetItems.txt row; unique: UniqueItems row)
 *   pUnit + 0x60     = pInventory  (player only)
 */
#define COLL_OFF_PITEMDATA      0x14
#define COLL_OFF_PINVENTORY     0x60
#define COLL_OFF_DWCLASSID      0x04
#define COLL_OFF_DWQUALITY      0x00
#define COLL_OFF_DWFILEINDEX    0x28
#define COLL_QUALITY_SET        5
#define COLL_STAT_GOLD          14
#define COLL_STAT_GOLDBANK      15

/* 1.9.0 — two-flag detection. We need three more pieces of info
 * per UnitAny when classifying drops:
 *   pUnit + 0x0C = dwUnitId    (per-game GUID, unique to this unit)
 *   pUnit + 0x10 = dwMode      (3 = ONGROUND, used to filter ground items)
 *   pUnit + 0xE8 = pListNext   (room-unit linked list step)
 *   pItemData + 0x18 = dwItemFlags (IFLAG_* bitset)
 *   pPath  + 0x1C = pRoom1     (player's current room)
 *   pRoom1 + 0x24 = ppRoomList (nearby rooms)
 *   pRoom1 + 0x28 = nNumRooms
 *   pRoom1 + 0x2C = first unit in room linked list
 * IFLAG_NEWITEM (0x10) is set when the item is freshly created by
 * the engine (monster drop, chest/barrel/object drop, vendor inv
 * generation, cube output) and cleared the first time a player
 * picks it up. So an item observed on the ground with the bit set
 * is guaranteed to be a fresh world-source drop — not something a
 * player is sharing.
 */
#define COLL_OFF_DWUNITID        0x0C
#define COLL_OFF_DWMODE          0x10
#define COLL_OFF_PLISTNEXT       0xE8
#define COLL_OFF_DWITEMFLAGS     0x18
#define COLL_OFF_PPATH_FROM_UNIT 0x2C
#define COLL_OFF_PROOM_FROM_PATH 0x1C
#define COLL_OFF_PROOMLIST       0x24
#define COLL_OFF_NUMROOMS        0x28
#define COLL_OFF_FIRSTUNIT       0x2C
#define COLL_UNIT_TYPE_ITEM      4
#define COLL_MODE_ONGROUND       3
#define COLL_IFLAG_NEWITEM       0x00000010
#define COLL_IFLAG_IDENTIFIED    0x00000001
#define COLL_IFLAG_RUNEWORD      0x04000000

/* dwClassId → slot index map. -1 = not a tracked collectible.
 * D2 class IDs are < ~4096 in 1.10f (Misc/Weapons/Armor combined). */
#define COLL_CLASSID_MAX        4096
static int  s_classIdToSlot[COLL_CLASSID_MAX];
static int  s_setFileIdxToSlot[COLL_NUM_SET_PIECES + 32]; /* +32 padding */
static BOOL s_itemMapsBuilt = FALSE;

/* dwFileIndex (SetItems.txt row) → catalog slot. We assume 1:1 because
 * our authoring order follows SetItems.txt row order. If this turns
 * out wrong on real items we'll need a name-based mapping. */

static DWORD CodeToDword(const char* code) {
    DWORD d = 0;
    if (!code) return 0;
    /* D2 packs item codes as 4 ASCII bytes, space-padded for short codes
     * (e.g. "box" → 'box ' = 0x20786F62 little-endian). Empty entries
     * remain 0 so the lookup naturally rejects them. */
    int len = (int)strlen(code);
    if (len > 4) len = 4;
    char padded[4] = {' ', ' ', ' ', ' '};
    for (int i = 0; i < len; i++) padded[i] = code[i];
    memcpy(&d, padded, 4);
    return d;
}

typedef void* (__stdcall *Coll_GetItemFromCode_t)(DWORD dwCode, int* pItemId);
typedef void* (__stdcall *Coll_INV_GetFirstItem_t)(void* pInv);
typedef void* (__stdcall *Coll_INV_GetNextItem_t)(void* pItem);
typedef int   (__stdcall *Coll_GetStat_t)(void* pUnit, int statId, int statRef);

static Coll_GetItemFromCode_t  s_fnGetItemFromCode  = NULL;
static Coll_INV_GetFirstItem_t s_fnGetFirstItem     = NULL;
static Coll_INV_GetNextItem_t  s_fnGetNextItem      = NULL;
static Coll_GetStat_t          s_fnGetStat          = NULL;

/* sgptDataTables offsets (per D2MOO D2DataTbls.h) — defined here so
 * Coll_BuildItemMaps below can reference them; the actual values are
 * also re-used by Coll_BuildInvFileOverrides further down. */
#define COLL_DT_PSETITEMSTXT    0xC18
#define COLL_DT_NSETITEMSTXTREC 0xC1C
#define COLL_DT_ITEMDATATBL     0xCD8
#define COLL_SETITEMS_STRIDE    0x1B8
#define COLL_SETITEMS_OFF_NAME  0x02
#define COLL_SETITEMS_OFF_INV   0x62
#define COLL_ITEMS_STRIDE       0x1A8
#define COLL_ITEMS_OFF_INV      0x20
#define COLL_ITEMS_OFF_DWCODE   0x80

/* Forward declaration for Coll_BuildItemMaps; the body is below the
 * invFile section because both functions need the sgptDataTables
 * pointer and we want to keep that helper colocated with its users. */
static DWORD Coll_GetSgptDT(void);

/* Resolve D2Common imports lazily — at DLL_PROCESS_ATTACH time
 * D2Common may not be fully loaded yet. We retry on every tick
 * until we have what we need. */
static void Coll_ResolveImports(void) {
    if (s_fnGetItemFromCode && s_fnGetFirstItem && s_fnGetNextItem) return;
    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (!hCommon) return;
    if (!s_fnGetItemFromCode) {
        s_fnGetItemFromCode = (Coll_GetItemFromCode_t)GetProcAddress(hCommon, (LPCSTR)10601);
    }
    if (!s_fnGetFirstItem) {
        s_fnGetFirstItem = (Coll_INV_GetFirstItem_t)GetProcAddress(hCommon, (LPCSTR)10277);
    }
    if (!s_fnGetNextItem) {
        s_fnGetNextItem = (Coll_INV_GetNextItem_t)GetProcAddress(hCommon, (LPCSTR)10304);
    }
    /* fnGetStat lives in d2arch_api.c as an external — we re-declare
     * it here. Initialized by InitAPI. */
    if (!s_fnGetStat) {
        extern int (__stdcall *fnGetStat)(void*, int, int);
        s_fnGetStat = (Coll_GetStat_t)fnGetStat;
    }
}

static void Coll_BuildItemMaps(void) {
    if (s_itemMapsBuilt) return;
    Coll_ResolveImports();
    if (!s_fnGetItemFromCode) {
        Log("Coll_BuildItemMaps: D2Common ord 10601 not yet resolved — deferring\n");
        return;  /* try again next tick */
    }

    for (int i = 0; i < COLL_CLASSID_MAX; i++) s_classIdToSlot[i] = -1;
    for (int i = 0; i < (int)(sizeof(s_setFileIdxToSlot)/sizeof(s_setFileIdxToSlot[0])); i++) {
        s_setFileIdxToSlot[i] = -1;
    }

    /* Set pieces: shared base codes (e.g. "amu" appears in 9+ sets).
     * We use dwFileIndex (the SetItems.txt row index at runtime) as the
     * routing key for set-quality items.
     *
     * IMPORTANT: SetItems.txt has an "Expansion" divider blank row at
     * position 62 (between Arcanna's Flesh and Natalya's Totem). D2's
     * runtime parser INCLUDES this blank row in pSetItemsTxt[], so all
     * LoD class-locked sets have a fileIndex that's 1 higher than
     * their authoring position in our catalog. A naive 1:1 mapping
     * misroutes 65 set pieces.
     *
     * Fix: build the map by NAME-matching our catalog displayName
     * against pSetItemsTxt[].szName at runtime. Same approach as the
     * invFile lookup. Returns the correct fileIdx → slot mapping
     * regardless of how D2 indexes the divider. */
    DWORD dt = Coll_GetSgptDT();
    if (dt) {
        DWORD pSetTxt = 0;
        int   nSetRec = 0;
        __try {
            pSetTxt = *(DWORD*)(dt + COLL_DT_PSETITEMSTXT);
            nSetRec = *(int*)  (dt + COLL_DT_NSETITEMSTXTREC);
        } __except(EXCEPTION_EXECUTE_HANDLER) { pSetTxt = 0; nSetRec = 0; }

        if (pSetTxt && nSetRec > 0 && nSetRec < 256) {
            int matched = 0;
            for (int s = 0; s < COLL_NUM_SET_PIECES; s++) {
                const char* myName = g_collSetPieces[s].displayName;
                if (!myName || !myName[0]) continue;
                for (int r = 0; r < nSetRec; r++) {
                    char* row = (char*)(pSetTxt + r * COLL_SETITEMS_STRIDE);
                    char  rowName[34] = {0};
                    __try {
                        memcpy(rowName, row + COLL_SETITEMS_OFF_NAME, 32);
                    } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
                    rowName[32] = 0;
                    if (rowName[0] && strncmp(myName, rowName, 32) == 0) {
                        if (r >= 0 && r < (int)(sizeof(s_setFileIdxToSlot)/sizeof(s_setFileIdxToSlot[0]))) {
                            s_setFileIdxToSlot[r] = COLL_SLOT_SETS_BASE + s;
                            matched++;
                        }
                        break;
                    }
                }
            }
            Log("Coll_BuildItemMaps: set-piece fileIdx map built by NAME (%d/%d matched, %d D2 rows)\n",
                matched, COLL_NUM_SET_PIECES, nSetRec);
        }
    }
    /* If sgptDataTables wasn't reachable, set-piece fileIdx map stays
     * all -1 (Coll_ProcessItem treats that as "no slot match" so set
     * items just won't be detected this run). Don't set s_itemMapsBuilt
     * — the next tick re-runs this builder once D2Common finishes
     * loading. Runes/gems/specials use the classId map below which only
     * needs D2Common ord 10601 (already gated by the s_fnGetItemFromCode
     * check at function entry), so those work even before sgptDataTables. */

    /* Runes — uniquely identifiable by classId */
    for (int i = 0; i < COLL_NUM_RUNES; i++) {
        const char* code = g_collRunes[i].code;
        if (!code[0]) continue;
        DWORD dwCode = CodeToDword(code);
        int classId = -1;
        __try { s_fnGetItemFromCode(dwCode, &classId); }
        __except(EXCEPTION_EXECUTE_HANDLER) { classId = -1; }
        if (classId >= 0 && classId < COLL_CLASSID_MAX) {
            s_classIdToSlot[classId] = COLL_SLOT_RUNES_BASE + i;
        }
    }
    /* Gems */
    for (int i = 0; i < COLL_NUM_GEMS; i++) {
        const char* code = g_collGems[i].code;
        if (!code[0]) continue;
        DWORD dwCode = CodeToDword(code);
        int classId = -1;
        __try { s_fnGetItemFromCode(dwCode, &classId); }
        __except(EXCEPTION_EXECUTE_HANDLER) { classId = -1; }
        if (classId >= 0 && classId < COLL_CLASSID_MAX) {
            s_classIdToSlot[classId] = COLL_SLOT_GEMS_BASE + i;
        }
    }
    /* Special items */
    for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
        const char* code = g_collSpecials[i].code;
        if (!code[0]) continue;
        DWORD dwCode = CodeToDword(code);
        int classId = -1;
        __try { s_fnGetItemFromCode(dwCode, &classId); }
        __except(EXCEPTION_EXECUTE_HANDLER) { classId = -1; }
        if (classId >= 0 && classId < COLL_CLASSID_MAX) {
            s_classIdToSlot[classId] = COLL_SLOT_SPECIALS_BASE + i;
        }
    }

    /* Only flag built when BOTH the classId map and the set-piece
     * fileIdx map are populated. If sgptDataTables wasn't reachable
     * yet, we leave s_itemMapsBuilt FALSE so the next tick retries
     * the set-piece name match. The classId-only catalogs (runes/
     * gems/specials) work either way. */
    BOOL setMapPopulated = FALSE;
    for (int i = 0; i < (int)(sizeof(s_setFileIdxToSlot)/sizeof(s_setFileIdxToSlot[0])); i++) {
        if (s_setFileIdxToSlot[i] >= 0) { setMapPopulated = TRUE; break; }
    }
    if (setMapPopulated) {
        s_itemMapsBuilt = TRUE;
        Log("Coll_BuildItemMaps: complete — %d classId entries mapped + set fileIdx map populated\n",
            COLL_NUM_RUNES + COLL_NUM_GEMS + COLL_NUM_SPECIALS);
    } else {
        Log("Coll_BuildItemMaps: classId entries mapped but sgptDataTables not yet ready — will retry for sets\n");
    }
}

/* ================================================================
 * TWO-FLAG DETECTION — GUID CLASSIFICATION TABLE (1.9.0)
 * ================================================================
 * Per-game-session table of item GUIDs (dwUnitId) we've observed,
 * with their classification:
 *   COLL_SRC_LEGIT   — first observed on the ground with IFLAG_NEWITEM
 *                      set. The drop came from a monster/chest/barrel/
 *                      object the engine spawned. Picking this up
 *                      satisfies Flag A.
 *   COLL_SRC_TAINTED — first observed on the ground without IFLAG_NEWITEM
 *                      (so it had been picked up by someone before — a
 *                      friend's drop or a re-drop) OR first observed in
 *                      the player's own inventory (cube output, vendor
 *                      purchase, quest reward, items already there at
 *                      character load, items withdrawn from any stash).
 *
 * The table is per-game-session — reset on character change and on
 * every Coll_LoadForCharacter call. dwUnitId is unique within a game
 * but recycled across games, so cross-game persistence isn't safe and
 * isn't needed (the catalog flags themselves are sticky and persisted).
 *
 * Capacity is sized for "lots of items in a play session" without going
 * crazy on memory. 4096 entries × 12 bytes ≈ 48 KB. Eviction of the
 * oldest stale entry kicks in when full so long sessions don't OOM.
 */
#define COLL_GUID_TBL_CAP        4096
#define COLL_SRC_LEGIT           1
#define COLL_SRC_TAINTED         2

typedef struct {
    DWORD guid;            /* 0 = empty slot */
    BYTE  source;          /* low bits: COLL_SRC_LEGIT or COLL_SRC_TAINTED.
                            * bit 7 (0x80): "already counted as items-picked-up". */
    BYTE  pad;             /* alignment */
    WORD  prevFlagsLo;     /* low 16 bits of pItemData->dwItemFlags last
                            * time we observed this GUID. Used to detect
                            * IFLAG_IDENTIFIED (bit 0) and IFLAG_RUNEWORD
                            * (bit 26 — out of WORD range, see prevFlagsHi) */
    WORD  prevFlagsHi;     /* high 16 bits of dwItemFlags. IFLAG_RUNEWORD
                            * (0x04000000) lives in bit 10 of this word. */
    DWORD lastSeenTick;    /* GetTickCount() of last observation */
} CollGuidEntry;

static CollGuidEntry s_collGuidTbl[COLL_GUID_TBL_CAP];

static void Coll_GuidTblReset(void) {
    memset(s_collGuidTbl, 0, sizeof(s_collGuidTbl));
}

/* Find the entry for `guid`, or NULL. Linear scan — the table is small
 * and the typical population is low (~tens of items per active room). */
static CollGuidEntry* Coll_LookupGuid(DWORD guid) {
    if (!guid) return NULL;
    for (int i = 0; i < COLL_GUID_TBL_CAP; i++) {
        if (s_collGuidTbl[i].guid == guid) return &s_collGuidTbl[i];
    }
    return NULL;
}

/* Insert a GUID with a given classification. Once classified, the
 * source is locked — re-adding the same GUID just refreshes lastSeen.
 * Falls back to evicting the oldest entry when the table is full. */
static void Coll_AddOrTouchGuid(DWORD guid, BYTE source, DWORD nowTick) {
    if (!guid) return;
    int firstEmpty = -1;
    int oldestIdx  = 0;
    DWORD oldestTick = 0xFFFFFFFFu;
    for (int i = 0; i < COLL_GUID_TBL_CAP; i++) {
        if (s_collGuidTbl[i].guid == guid) {
            s_collGuidTbl[i].lastSeenTick = nowTick;
            return;
        }
        if (s_collGuidTbl[i].guid == 0 && firstEmpty < 0) firstEmpty = i;
        if (s_collGuidTbl[i].lastSeenTick < oldestTick) {
            oldestTick = s_collGuidTbl[i].lastSeenTick;
            oldestIdx  = i;
        }
    }
    int slot = (firstEmpty >= 0) ? firstEmpty : oldestIdx;
    s_collGuidTbl[slot].guid         = guid;
    s_collGuidTbl[slot].source       = source;
    s_collGuidTbl[slot].lastSeenTick = nowTick;
}

/* Walk every item unit in the current player's room + nearby rooms.
 * Items currently in IMODE_ONGROUND are classified by their IFLAG_NEWITEM
 * bit. Already-classified GUIDs keep their source (so re-classification
 * doesn't promote a TAINTED player-drop to LEGIT just because the engine
 * happens to set NEWITEM somewhere weird). */
static void Coll_ScanGroundItems(void* pPlayerUnit) {
    if (!pPlayerUnit) return;
    DWORD nowTick = GetTickCount();

    __try {
        DWORD pPath = *(DWORD*)((BYTE*)pPlayerUnit + COLL_OFF_PPATH_FROM_UNIT);
        if (!pPath) return;
        DWORD pRoom = *(DWORD*)(pPath + COLL_OFF_PROOM_FROM_PATH);
        if (!pRoom) return;

        /* Current room + nearby rooms (same pattern as ScanMonsters in
         * d2arch_gameloop.c) so we catch drops slightly off-screen too. */
        DWORD *ppRoomList = *(DWORD**)(pRoom + COLL_OFF_PROOMLIST);
        int    nNumRooms  = *(int*)   (pRoom + COLL_OFF_NUMROOMS);
        if (nNumRooms < 0 || nNumRooms > 20) nNumRooms = 0;

        DWORD rooms[21];
        int   roomCount = 0;
        rooms[roomCount++] = pRoom;
        if (ppRoomList && nNumRooms > 0) {
            for (int r = 0; r < nNumRooms && roomCount < 21; r++) {
                DWORD nr = ppRoomList[r];
                if (nr && nr != pRoom) rooms[roomCount++] = nr;
            }
        }

        for (int ri = 0; ri < roomCount; ri++) {
            DWORD unit = *(DWORD*)(rooms[ri] + COLL_OFF_FIRSTUNIT);
            int chain = 0;
            while (unit && chain++ < 500) {
                DWORD type   = *(DWORD*)(unit + 0x00);
                DWORD unitId = *(DWORD*)(unit + COLL_OFF_DWUNITID);
                DWORD mode   = *(DWORD*)(unit + COLL_OFF_DWMODE);

                if (type == COLL_UNIT_TYPE_ITEM && unitId != 0 &&
                    mode == COLL_MODE_ONGROUND) {
                    /* Classify on first observation only. Re-classifying
                     * would let cheats taint→legit by manipulating flag
                     * state across ticks. */
                    if (!Coll_LookupGuid(unitId)) {
                        DWORD pItemData = *(DWORD*)(unit + COLL_OFF_PITEMDATA);
                        DWORD itemFlags = pItemData
                                ? *(DWORD*)(pItemData + COLL_OFF_DWITEMFLAGS)
                                : 0;
                        BYTE source = (itemFlags & COLL_IFLAG_NEWITEM)
                                          ? COLL_SRC_LEGIT
                                          : COLL_SRC_TAINTED;
                        Coll_AddOrTouchGuid(unitId, source, nowTick);
                    } else {
                        Coll_AddOrTouchGuid(unitId, 0, nowTick); /* refresh lastSeen */
                    }
                }
                unit = *(DWORD*)(unit + COLL_OFF_PLISTNEXT);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        /* Best-effort scan — bad pointers anywhere just abort the walk. */
    }
}

/* Process a single item: read its quality + classId/fileIndex, find
 * the matching catalog slot, mark collected.
 *
 * `requireLegit` controls the two-flag enforcement:
 *   TRUE  — strict mode (1.9.0+ default). The item's GUID must be in
 *           our session table classified as COLL_SRC_LEGIT, i.e. we
 *           saw it on the ground with IFLAG_NEWITEM set before it
 *           landed in the player's inventory. Items first observed
 *           in inventory get inserted as TAINTED so they can't later
 *           be promoted by re-encountering them.
 *   FALSE — legacy mode, used only during the one-time grace pass for
 *           sidecars upgrading from v1 → v2. Marks anything matching
 *           the catalog without GUID checks. */
/* 1.9.4: non-static so d2arch_gameloop.c can call this on AP-delivered
 * items via forward extern decl. AP-delivered items go straight to
 * inventory (bDroppable=0) and bypassed the inventory-walk pickup path
 * that normally triggers Coll_ProcessItem. Without this hook, F1
 * Collection counters never advance for AP rewards. */
void Coll_ProcessItem(void* pItem, BOOL requireLegit) {
    if (!pItem) return;
    DWORD classId = 0;
    void* pItemData = NULL;
    DWORD quality = 0;
    int   fileIdx = -1;
    DWORD unitId  = 0;
    __try {
        unitId    = *(DWORD*)((BYTE*)pItem + COLL_OFF_DWUNITID);
        classId   = *(DWORD*)((BYTE*)pItem + COLL_OFF_DWCLASSID);
        pItemData = *(void**)((BYTE*)pItem + COLL_OFF_PITEMDATA);
        if (pItemData) {
            quality = *(DWORD*)((BYTE*)pItemData + COLL_OFF_DWQUALITY);
            fileIdx = *(int*)  ((BYTE*)pItemData + COLL_OFF_DWFILEINDEX);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    /* Resolve which catalog slot (if any) this item maps to. */
    int slotIdx = -1;
    if (quality == COLL_QUALITY_SET && fileIdx >= 0 &&
        fileIdx < (int)(sizeof(s_setFileIdxToSlot)/sizeof(s_setFileIdxToSlot[0]))) {
        slotIdx = s_setFileIdxToSlot[fileIdx];
    } else if (classId < COLL_CLASSID_MAX) {
        slotIdx = s_classIdToSlot[classId];
    }
    if (slotIdx < 0) return;  /* not a tracked collectible */

    /* Read current item flags so we can detect IFLAG_RUNEWORD and
     * IFLAG_IDENTIFIED transitions for the Logbook counters. Done
     * regardless of `requireLegit` so the legacy/grace path also
     * detects them. */
    DWORD currentFlags = 0;
    if (pItemData) {
        __try { currentFlags = *(DWORD*)((BYTE*)pItemData + COLL_OFF_DWITEMFLAGS); }
        __except(EXCEPTION_EXECUTE_HANDLER) { currentFlags = 0; }
    }

    if (requireLegit) {
        /* Two-flag enforcement. The inventory-walk we're called from
         * already proves Flag B (item is in the player's possession).
         * Flag A is satisfied iff the GUID was previously observed on
         * the ground with IFLAG_NEWITEM.
         *
         * Source byte layout:
         *   bits 0..6 = classification (COLL_SRC_LEGIT=1 or COLL_SRC_TAINTED=2)
         *   bit  7    = "already counted as items-picked-up" flag
         * Always mask with 0x7F before comparing classification. */
        CollGuidEntry* e = Coll_LookupGuid(unitId);

        /* Flag-transition detection — fires for every observed inv item
         * whether catalog-matched or not. We compare current flags with
         * stored prev flags; if IFLAG_RUNEWORD or IFLAG_IDENTIFIED just
         * went 0→1, bump the relevant Logbook counter. The Stats_*
         * functions are forward-declared via the unity-build extern
         * because g_charStats lives in stats.c which is included AFTER
         * us. */
        extern void Stats_OnRunewordCreated(void);
        extern void Stats_OnItemIdentified(void);
        if (e) {
            DWORD prevFlags = ((DWORD)e->prevFlagsHi << 16) | e->prevFlagsLo;
            DWORD newOn = currentFlags & ~prevFlags;  /* bits flipped 0→1 */
            if (newOn & COLL_IFLAG_RUNEWORD) {
                Stats_OnRunewordCreated();
                /* 1.9.2 Cat 5 — fire the next sequential runeword AP
                 * slot. Extra_OnRunewordCreated maintains its own
                 * counter inside d2arch_extrachecks.c (g_charStats
                 * isn't visible here yet — stats.c is included AFTER
                 * collections.c in the unity build). The function
                 * picks the right next slot internally. */
                extern void Extra_OnRunewordCreatedAuto(void);
                Extra_OnRunewordCreatedAuto();
            }
            if (newOn & COLL_IFLAG_IDENTIFIED) Stats_OnItemIdentified();
            e->prevFlagsLo = (WORD)(currentFlags & 0xFFFF);
            e->prevFlagsHi = (WORD)((currentFlags >> 16) & 0xFFFF);
        }
        if (!e) {
            /* First-ever observation, and it's in inventory — never
             * transited through ground state under our watch. Lock it
             * as TAINTED so a later ground re-encounter without
             * IFLAG_NEWITEM (player might drop+pickup) can't flip it.
             *
             * 1.9.0 — F1 Logbook: count this as an "items picked up"
             * event with the counted-flag bit so the next tick that
             * re-observes this same GUID doesn't re-count it. */
            extern void Stats_OnItemPickedUp(int quality, int slotIdx);
            Stats_OnItemPickedUp((int)quality, slotIdx);
            Coll_AddOrTouchGuid(unitId,
                                COLL_SRC_TAINTED | 0x80, GetTickCount());
            /* Capture initial flags so the next tick's transition
             * detection has a baseline. Without this, the very first
             * observation of a runeword would falsely register as a
             * 0→1 transition next tick. */
            CollGuidEntry* fresh = Coll_LookupGuid(unitId);
            if (fresh) {
                fresh->prevFlagsLo = (WORD)(currentFlags & 0xFFFF);
                fresh->prevFlagsHi = (WORD)((currentFlags >> 16) & 0xFFFF);
            }
            return;
        }
        if ((e->source & 0x7F) != COLL_SRC_LEGIT) return;
        /* LEGIT classification — first time we visit this entry from
         * the inv walk, count as picked-up (catalog item that was
         * legitimately ground-acquired). The classification bits stay
         * intact; only the high counted-flag bit is set. */
        if ((e->source & 0x80) == 0) {
            extern void Stats_OnItemPickedUp(int quality, int slotIdx);
            Stats_OnItemPickedUp((int)quality, slotIdx);
            e->source |= 0x80;
        }
    }
    Coll_MarkSlotCollected(slotIdx);

    /* 1.9.2 Extra check Cat 3 — High-rune pickup detection.
     * Rune slots are 32..64 (r01..r33). High runes are r21..r33 (Pul+).
     * Extra_OnHighRunePickup is dedup-protected internally (bitmap
     * fire-once per tier×difficulty), so calling on every observed
     * rune is safe. Forward-decl because extrachecks.c is included
     * after collections.c in the unity build. */
    if (slotIdx >= COLL_SLOT_RUNES_BASE && slotIdx < COLL_SLOT_GEMS_BASE) {
        int runeIdx = slotIdx - COLL_SLOT_RUNES_BASE + 1;  /* 1..33 = r01..r33 */
        if (runeIdx >= 21) {
            extern void Extra_OnHighRunePickup(int runeIdx, int diff);
            extern int  g_currentDifficulty;
            Extra_OnHighRunePickup(runeIdx, g_currentDifficulty);
        }
    }
}

/* 1.9.0 — STK sidecar scan. Walks the per-character + account-wide
 * STK tabs and marks any catalog slot whose dwCode matches a stored
 * stack. STK sidecars store the dwCode directly per-slot (no bitstream
 * parsing needed) so this scan is cheap. */
static void Coll_ScanStkSidecars(void) {
    /* g_stashStkAp[3] = per-char tabs (Consumables, Runes, Gems)
     * g_stashStkSh[3] = account-wide tabs */
    extern StkTab g_stashStkAp[STASH_NUM_STK_TABS];
    extern StkTab g_stashStkSh[STASH_NUM_STK_TABS];

    for (int scope = 0; scope < 2; scope++) {
        StkTab* tabs = (scope == 0) ? g_stashStkAp : g_stashStkSh;
        for (int t = 0; t < STASH_NUM_STK_TABS; t++) {
            for (int c = 0; c < STASH_SLOTS_PER_TAB; c++) {
                StkSlot* s = &tabs[t].slots[c];
                if (s->count == 0 || s->dwCode == 0) continue;

                /* Resolve dwCode → classId → slotIdx via the existing map */
                if (!s_fnGetItemFromCode) continue;
                int classId = -1;
                __try { s_fnGetItemFromCode(s->dwCode, &classId); }
                __except(EXCEPTION_EXECUTE_HANDLER) { classId = -1; }
                if (classId >= 0 && classId < COLL_CLASSID_MAX) {
                    int slot = s_classIdToSlot[classId];
                    if (slot >= 0) Coll_MarkSlotCollected(slot);
                }
            }
        }
    }
}

/* Internal worker — `requireLegit` controls strict vs legacy behavior.
 * `includeStkScan` controls whether STK sidecar tabs are also scanned
 * (legacy grace path only — strict mode skips it because items in
 * stash didn't observably transit through ground under our watch). */
static void Coll_ScanPlayerHoldings_Internal(void* pPlayerUnit,
                                             BOOL requireLegit,
                                             BOOL includeStkScan) {
    if (!pPlayerUnit) return;
    Coll_BuildItemMaps();
    if (!s_itemMapsBuilt || !s_fnGetFirstItem || !s_fnGetNextItem) return;

    /* The player unit's pInventory contains the linked list of EVERY
     * item the player owns: backpack, equipped slots, belt, even cursor.
     * Walking it covers all in-character items in one pass. */
    void* pInv = NULL;
    __try { pInv = *(void**)((BYTE*)pPlayerUnit + COLL_OFF_PINVENTORY); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pInv) return;

    void* pItem = NULL;
    __try { pItem = s_fnGetFirstItem(pInv); }
    __except(EXCEPTION_EXECUTE_HANDLER) { pItem = NULL; }

    int safety = 0;
    while (pItem && safety++ < 4096) {
        Coll_ProcessItem(pItem, requireLegit);
        void* pNext = NULL;
        __try { pNext = s_fnGetNextItem(pItem); }
        __except(EXCEPTION_EXECUTE_HANDLER) { pNext = NULL; }
        pItem = pNext;
    }

    /* STK sidecar scan — only in legacy/grace path. Strict mode does
     * not auto-mark from STK tabs because:
     *   - STK_AP (per-char): items may have been picked up + deposited
     *     between scan ticks without us seeing the GUID classified.
     *   - STK_SH (account-wide): items can be deposited by any of
     *     your characters, so the current character may not have
     *     legitimately acquired them. This is exactly the cheating
     *     vector the two-flag rule is designed to block.
     * Already-marked slots stay marked regardless. Going forward,
     * legitimate withdrawals re-enter the inventory but with fresh
     * GUIDs, which fail the strict check (correct anti-cheat). */
    if (includeStkScan) Coll_ScanStkSidecars();

    /* Regular stash sidecars (ap_stash/shared_stash) deferred to v2 —
     * those store items as serialized bitstreams that need
     * ITEMS_GetCompactItemDataFromBitstream + quality-aware routing.
     * Mercenary inventory scan also deferred to v2 — needs walk of
     * D2Client unit hash table to find the merc unit by owner ID. */
}

void Coll_ScanPlayerHoldings(void* pPlayerUnit) {
    /* Public entry point — always strict. The grace pass uses the
     * internal helper directly. */
    Coll_ScanPlayerHoldings_Internal(pPlayerUnit, TRUE, FALSE);
}

/* 1.9.0 Phase 6 — grandfather a single inv item past the strict
 * two-flag check. Used by the stash-load pipeline when items get
 * spawned into the player's pInventory from `ap_stash_ser_<charname>.dat`
 * or `shared_stash_ser.dat` — those items are trusted (player put
 * them in their own stash) but were never observed in the GUID
 * classification table, so the strict path would lock them as
 * TAINTED forever and never auto-mark.
 *
 * The implementation just calls Coll_ProcessItem with requireLegit=
 * FALSE, which performs the catalog lookup + Coll_MarkSlotCollected
 * without any GUID gating. Items that don't map to a catalog slot
 * silently no-op. Items that ARE in the catalog get marked AND get
 * the gold-flash celebration as a side effect (same code path as
 * any first-time mark). */
void Coll_GrandfatherInvItem(void* pItem) {
    if (!pItem) return;
    Coll_ProcessItem(pItem, FALSE);
}

/* 1.9.0 — one-time grace scan for sidecars upgrading v1 → v2. Marks
 * everything currently in inventory + STK tabs without GUID checks,
 * grandfathering items the player legitimately acquired before the
 * two-flag rule went live. Called once after a v1 sidecar load. */
static void Coll_ScanPlayerHoldings_Grace(void* pPlayerUnit) {
    Coll_ScanPlayerHoldings_Internal(pPlayerUnit, FALSE, TRUE);
}

/* ================================================================
 * GOLD DELTA-WATCHER
 * ================================================================
 * Approach A (delta-watcher) per the spec. Each tick we read the
 * player's STAT_GOLD value. If it increased AND no vendor UI is
 * open, count the delta as "earned". We DON'T currently distinguish
 * gambling/repair refunds; those edge cases are accepted at v1.
 *
 * Vendor-UI detection: D2 sets ui_state bit for trade/gamble/repair
 * windows. Our heuristic uses a simpler test — TODO refine later.
 * ================================================================ */
static int  s_lastSeenGold     = -1;     /* -1 = uninitialized */
static int  s_lastSeenGoldBank = -1;

/* D2Client gpUIState is a DWORD array indexed by UI element ID.
 * Address: D2Client image base + 0x11A6A8 (per d2arch_stashui.c).
 * Each entry: 0 = closed, non-zero = open. We check UI_NPCSHOP and
 * UI_MPTRADE — when either is open, gold delta is likely vendor-
 * driven (sale revenue or trade) and should not count toward the
 * lifetime "earned" counter.
 *
 * UI panel IDs from D2MOO/D2Constants.h:
 *   UI_NPCSHOP = 0x0C   (vendor trade — sell/buy/gamble all here)
 *   UI_MPTRADE = 0x17   (player-to-player trade)
 *   UI_NPCMENU = 0x08   (NPC initial menu — shouldn't trigger gold deltas)
 *
 * Anvil (0x0E) doesn't trigger gold deltas (just imbues an item).
 * Cube (0x1A) cube recipes can produce gold but rarely; we count those.
 */
#define COLL_UI_NPCSHOP   0x0C
#define COLL_UI_MPTRADE   0x17
#define COLL_RVA_GPUISTATE 0x11A6A8

static BOOL Coll_IsVendorUIOpen(void) {
    HMODULE h = GetModuleHandleA("D2Client.dll");
    if (!h) return FALSE;

    DWORD* pUIState = (DWORD*)((BYTE*)h + COLL_RVA_GPUISTATE);
    DWORD shop = 0, trade = 0;
    __try {
        shop  = pUIState[COLL_UI_NPCSHOP];
        trade = pUIState[COLL_UI_MPTRADE];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }

    return (shop != 0) || (trade != 0);
}

static void Coll_PollGoldDelta(void* pPlayerUnit) {
    if (!pPlayerUnit || !s_fnGetStat) return;
    int curGold     = 0;
    int curGoldBank = 0;
    __try {
        curGold     = s_fnGetStat(pPlayerUnit, COLL_STAT_GOLD, 0);
        curGoldBank = s_fnGetStat(pPlayerUnit, COLL_STAT_GOLDBANK, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    /* First-tick init: latch current value, do not credit historical
     * gold the player loaded with. The lifetime counter starts from
     * "earned since 1.9.0 Collection feature went live for this char". */
    if (s_lastSeenGold < 0) {
        s_lastSeenGold     = curGold;
        s_lastSeenGoldBank = curGoldBank;
        return;
    }

    int totalCur  = curGold + curGoldBank;
    int totalPrev = s_lastSeenGold + s_lastSeenGoldBank;
    int delta = totalCur - totalPrev;

    if (delta > 0 && !Coll_IsVendorUIOpen()) {
        Coll_AddGoldEarned((uint64_t)delta);
    }
    /* delta < 0 = spending; delta == 0 = no change. */

    /* 1.9.0 — F1 Logbook: forward the same delta to the stats module
     * so it can bucket per-act and track lifetime spend. The vendor-UI
     * filter only applies to Coll's lifetime-only counter; stats logs
     * spending separately so it makes sense regardless. */
    {
        extern int  GetCurrentArea(void);
        extern void Stats_OnGoldDelta(int64_t delta, int currentArea);
        int curArea = GetCurrentArea();
        if (delta != 0 && !Coll_IsVendorUIOpen()) {
            Stats_OnGoldDelta((int64_t)delta, curArea);
        } else if (delta < 0) {
            /* vendor-spending case still gets recorded as goldSpent */
            Stats_OnGoldDelta((int64_t)delta, curArea);
        }
    }

    s_lastSeenGold     = curGold;
    s_lastSeenGoldBank = curGoldBank;
}

/* ================================================================
 * TICK ENTRY POINT — called from ProcessPendingGameTick
 * ================================================================ */
static DWORD s_lastScanTick = 0;

void Coll_OnGameTick(void* pPlayerUnit) {
    if (!pPlayerUnit) return;

    /* 1.9.0 — Two-flag detection. Walk on-ground items every tick so
     * we never miss the brief LEGIT classification window between a
     * monster drop and the player picking it up. Cheap: typical room
     * has a handful of items, and the per-GUID dedup means re-walking
     * the same items is a no-op. */
    Coll_ScanGroundItems(pPlayerUnit);

    /* Throttled inventory scan — every ~2 seconds is plenty for an
     * always-on tracker. Items don't pop in/out faster than that in
     * normal play. The ground walk above runs faster (every tick) so
     * the LEGIT classification is in place before this runs. */
    DWORD now = GetTickCount();
    if (now - s_lastScanTick > 2000) {
        s_lastScanTick = now;
        if (s_collGracePending) {
            /* One-time legacy scan to grandfather a pre-update char's
             * inventory + STK tabs. Marks anything matching the catalog
             * without GUID checks, then strict mode takes over. */
            Coll_ScanPlayerHoldings_Grace(pPlayerUnit);
            s_collGracePending = FALSE;
            Log("Coll: grace scan complete — strict two-flag mode now active\n");
        } else {
            Coll_ScanPlayerHoldings(pPlayerUnit);
        }
    }

    /* Gold tracking ticks every frame so we don't miss large pile
     * grabs that happen between throttled scans. */
    Coll_PollGoldDelta(pPlayerUnit);
}

/* Reset gold-tracking state on character change so the next char
 * starts from a clean baseline. Also clears the GUID classification
 * table — dwUnitId values aren't stable across games, so a stale
 * entry could collide with a fresh item in the next session. */
void Coll_ResetTickState(void) {
    s_lastScanTick     = 0;
    s_lastSeenGold     = -1;
    s_lastSeenGoldBank = -1;
    Coll_GuidTblReset();
    /* s_collGracePending is set/cleared by Coll_LoadForCharacter on
     * the next character load; don't touch it here. */
}

/* ================================================================
 * GOLD COUNTER
 * ================================================================ */
void Coll_AddGoldEarned(uint64_t delta) {
    if (delta == 0) return;
    /* Saturating add — paranoia for the 2^64 case nobody will ever hit. */
    if (g_collState.goldEarned + delta < g_collState.goldEarned) {
        g_collState.goldEarned = (uint64_t)-1;
    } else {
        g_collState.goldEarned += delta;
    }
    /* 1.9.0 — Bonus check pipeline gold-milestone watcher.
     * Implemented as forward extern so collections.c stays decoupled
     * from bonuschecks.c declaration order. */
    extern void Bonus_CheckGoldMilestones(uint64_t lifetimeGold);
    Bonus_CheckGoldMilestones(g_collState.goldEarned);
}

/* ================================================================
 * GOAL COMPLETION
 * ================================================================ */
BOOL Coll_IsGoalComplete(void) {
    /* Sets */
    for (int i = 0; i < COLL_NUM_SETS; i++) {
        if (!g_collGoal.setsTargeted[i]) continue;
        const CollectionSet* s = &g_collSets[i];
        for (int p = 0; p < s->pieceCount; p++) {
            if (!Coll_IsSlotCollected(COLL_SLOT_SETS_BASE + s->firstSlot + p))
                return FALSE;
        }
    }
    /* Runes — 1.9.0 granular check. Per-rune targeting via
     * runesTargeted[]; allRunesTargeted is a legacy mirror kept only
     * for v1 sidecar back-compat. */
    for (int i = 0; i < COLL_NUM_RUNES; i++) {
        if (!g_collGoal.runesTargeted[i]) continue;
        if (!Coll_IsSlotCollected(COLL_SLOT_RUNES_BASE + i)) return FALSE;
    }
    /* Gems */
    if (g_collGoal.allGemsTargeted) {
        for (int i = 0; i < COLL_NUM_GEMS; i++) {
            if (!Coll_IsSlotCollected(COLL_SLOT_GEMS_BASE + i)) return FALSE;
        }
    }
    /* Specials */
    for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
        if (!g_collGoal.specialsTargeted[i]) continue;
        if (!Coll_IsSlotCollected(COLL_SLOT_SPECIALS_BASE + i)) return FALSE;
    }
    /* Gold target */
    if (g_collGoal.goldTarget > 0 && g_collState.goldEarned < g_collGoal.goldTarget)
        return FALSE;

    return TRUE;
}

/* ================================================================
 * F1 PAGE RENDER (Phase 3) — Multi-page Collection tracker
 * ================================================================
 * 5 sub-pages dispatched by g_editorPage value:
 *   3 = Sets Part 1   (sets 0..15, the 16 Normal/early-game sets)
 *   4 = Sets Part 2   (sets 16..31, class-locked + LoD expansion)
 *   5 = Runes         (33 runes El..Zod in 11x3 grid)
 *   6 = Gems          (35 gems, 7 colors x 5 grades grid)
 *   7 = Specials+Gold (10 special items + lifetime gold counter)
 *
 * Every page has the same layout frame:
 *   - Title bar at top
 *   - Content area in middle
 *   - Hover tooltip overlay (above content)
 *   - Bottom progress summary
 *
 * Drawing primitives are provided by d2arch_api.c statics — they're
 * file-scope in the unity build because d2arch_api.c is included
 * before this file. We use them directly without re-declaration.
 * ================================================================ */

/* F1 book content area — empirically measured from user click data
 * (DRAG log entries while clicking the visible parchment edges).
 *
 * LEFT PAGE corners:  X 50..322,  Y 50..407  →  272 × 357 px
 * SPINE (dead zone):  X 322..378                     56 wide
 * RIGHT PAGE corners: X 378..642, Y 50..411  →  264 × 361 px
 *
 * CONTENT MUST STAY INSIDE the per-page rect. Anything in the spine
 * gets eaten by the book's center binding artwork. */

/* ================================================================
 * LAYOUT CONFIGURATION — fully tunable via d2arch.ini [Collection]
 * ================================================================
 * All values reload on every F1 render frame so you can edit the INI
 * and see changes immediately by closing+reopening the F1 book (no
 * need to restart D2). Values are in screen pixels.
 *
 * Default values match the empirically-measured book bounds for the
 * current resolution. See [Collection] section in d2arch.ini for
 * per-key documentation.
 * ================================================================ */
/* Page bounds — used to compute book center for default Title/Total/Footer X. */
static int g_collLpX        = 58;
static int g_collLpW        = 260;
static int g_collRpX        = 384;
static int g_collRpW        = 254;
/* Title band — vertical positions for the 3 centered text elements */
static int g_collTitleY     = 72;
static int g_collTotalY     = 90;
static int g_collFooterY    = 390;
/* Cell dimensions per page-type */
static int g_collSetsRowH    = 26;
static int g_collRunesCellW  = 44;
static int g_collRunesCellH  = 44;
static int g_collRunesGap    = 4;
static int g_collGemsCellW   = 30;
static int g_collGemsCellH   = 36;
static int g_collGemsGap     = 4;
static int g_collSpecCellW   = 42;
static int g_collSpecCellH   = 42;
static int g_collSpecGap     = 8;
static int g_collSpecRowH    = 68;
/* Gold counter Y positions */
static int g_collGoldCounterY = 220;
static int g_collGoldExplY    = 242;
static int g_collGoldGoalY    = 280;
/* X positions for the three centered text elements (default: spine center).
 * 188 = left page center, 511 = right page center, or any custom X. */
static int g_collTitleX  = 350;        /* "Collection - Sets (1/2)" */
static int g_collTotalX  = 350;        /* "Total: 68 / 205" */
static int g_collFooterX = 350;        /* "Sets Complete" / "Runes:" / "Gems:" */

/* === Per-element X/Y overrides — full positional control ============ */
/* Sets pages */
static int g_collSetsLpHdrY     = 108;
static int g_collSetsLpNameX    = 62;
static int g_collSetsLpFoundX   = 288;
static int g_collSetsRpHdrY     = 108;
static int g_collSetsRpNameX    = 388;
static int g_collSetsRpFoundX   = 608;
static int g_collSetsRowsY      = 139;

/* Runes — Low (left page top section) */
static int g_collLowRunesLabelX = 188;
static int g_collLowRunesLabelY = 125;
static int g_collLowRunesGridX  = 70;
static int g_collLowRunesGridY  = 143;
/* Runes — Mid (left page bottom section) */
static int g_collMidRunesLabelX = 188;
static int g_collMidRunesLabelY = 245;
static int g_collMidRunesGridX  = 70;
static int g_collMidRunesGridY  = 263;
/* Runes — High (right page top section) */
static int g_collHighRunesLabelX = 511;
static int g_collHighRunesLabelY = 125;
static int g_collHighRunesGridX  = 393;
static int g_collHighRunesGridY  = 143;
/* Runes — Top (right page bottom section, 3 cells centered) */
static int g_collTopRunesLabelX  = 511;
static int g_collTopRunesLabelY  = 245;
static int g_collTopRunesGridX   = 441;
static int g_collTopRunesGridY   = 263;

/* Gems — Left page (4 colors) */
static int g_collGemsLpHdrX      = 138;
static int g_collGemsLpHdrY      = 108;
static int g_collGemsLpLabelX    = 70;
static int g_collGemsLpGridX     = 138;
static int g_collGemsLpRowY      = 131;
static int g_collGemsLpRowSp     = 56;
/* Gems — Right page (3 colors) */
static int g_collGemsRpHdrX      = 462;
static int g_collGemsRpHdrY      = 108;
static int g_collGemsRpLabelX    = 394;
static int g_collGemsRpGridX     = 462;
static int g_collGemsRpRowY      = 131;
static int g_collGemsRpRowSp     = 56;

/* Specials section (left page) */
static int g_collSpecHdrX        = 188;
static int g_collSpecHdrY        = 108;
static int g_collSpecGridX       = 67;
static int g_collSpecGridY       = 142;
static int g_collSpecLabelOffY   = 12;     /* Y offset of label under cell */
static int g_collSpecFooterX     = 188;
static int g_collSpecFooterY     = 390;
/* Gold section (right page) */
static int g_collGoldHdrX        = 511;
static int g_collGoldHdrY        = 108;
static int g_collGoldCounterX    = 511;
static int g_collGoldExplX       = 511;
static int g_collGoldGoalX       = 511;

/* Derived values — recomputed in Coll_LoadIniLayout */
#define COLL_LP_X       g_collLpX
#define COLL_LP_W       g_collLpW
#define COLL_LP_CTR     (g_collLpX + g_collLpW / 2)
#define COLL_RP_X       g_collRpX
#define COLL_RP_W       g_collRpW
#define COLL_RP_CTR     (g_collRpX + g_collRpW / 2)
#define COLL_TITLE_Y    g_collTitleY
#define COLL_TOTAL_Y    g_collTotalY
#define COLL_FOOTER_Y   g_collFooterY
#define COLL_BOOK_CTR   ((g_collLpX + g_collRpX + g_collRpW) / 2)

/* Legacy aliases used only by hover-rect calculations. */
#define COLL_BOOK_X     g_collLpX
#define COLL_BOOK_W     (g_collRpX + g_collRpW - g_collLpX)
#define COLL_PANEL_X    g_collLpX
#define COLL_PANEL_Y    g_collTitleY
#define COLL_PANEL_W    COLL_BOOK_W
#define COLL_PANEL_H    (g_collFooterY - g_collTitleY)

/* Read all layout values from d2arch.ini [Collection] section.
 * Called from Coll_RenderF1Page on every frame so edits show up
 * after the F1 book is closed and reopened. */
static void Coll_LoadIniLayout(void) {
    char ini[MAX_PATH], dir[MAX_PATH];
    GetArchDir(dir, MAX_PATH);
    _snprintf(ini, sizeof(ini) - 1, "%sd2arch.ini", dir);
    ini[sizeof(ini) - 1] = 0;

    #define R(field, key, def) field = GetPrivateProfileIntA("Collection", key, def, ini)
    /* Page bounds */
    R(g_collLpX,    "LeftPageX",  58);
    R(g_collLpW,    "LeftPageW",  260);
    R(g_collRpX,    "RightPageX", 384);
    R(g_collRpW,    "RightPageW", 254);
    /* Title band shared across pages */
    R(g_collTitleY, "TitleY",     72);
    R(g_collTotalY, "TotalY",     90);
    R(g_collFooterY,"FooterY",    390);
    /* Cell dimensions */
    R(g_collSetsRowH,    "SetsRowH",   26);
    R(g_collRunesCellW,  "RunesCellW", 44);
    R(g_collRunesCellH,  "RunesCellH", 44);
    R(g_collRunesGap,    "RunesGap",   4);
    R(g_collGemsCellW,   "GemsCellW",  30);
    R(g_collGemsCellH,   "GemsCellH",  36);
    R(g_collGemsGap,     "GemsGap",    4);
    R(g_collSpecCellW,   "SpecCellW",  42);
    R(g_collSpecCellH,   "SpecCellH",  42);
    R(g_collSpecGap,     "SpecGap",    8);
    R(g_collSpecRowH,    "SpecRowH",   68);
    /* Gold Y positions */
    R(g_collGoldCounterY, "GoldCounterY", 220);
    R(g_collGoldExplY,    "GoldExplY",    242);
    R(g_collGoldGoalY,    "GoldGoalY",    280);

    /* X positions for the three centered text elements. Default to the
     * computed spine center so existing setups stay unchanged. */
    int defaultCtr = (g_collLpX + g_collRpX + g_collRpW) / 2;
    R(g_collTitleX,  "TitleX",  defaultCtr);
    R(g_collTotalX,  "TotalX",  defaultCtr);
    R(g_collFooterX, "FooterX", defaultCtr);

    /* === Per-element X/Y overrides === */
    R(g_collSetsLpHdrY,    "SetsLpHdrY",    108);
    R(g_collSetsLpNameX,   "SetsLpNameX",   62);
    R(g_collSetsLpFoundX,  "SetsLpFoundX",  288);
    R(g_collSetsRpHdrY,    "SetsRpHdrY",    108);
    R(g_collSetsRpNameX,   "SetsRpNameX",   388);
    R(g_collSetsRpFoundX,  "SetsRpFoundX",  608);
    R(g_collSetsRowsY,     "SetsRowsY",     139);

    R(g_collLowRunesLabelX,  "LowRunesLabelX",  188);
    R(g_collLowRunesLabelY,  "LowRunesLabelY",  125);
    R(g_collLowRunesGridX,   "LowRunesGridX",   70);
    R(g_collLowRunesGridY,   "LowRunesGridY",   143);
    R(g_collMidRunesLabelX,  "MidRunesLabelX",  188);
    R(g_collMidRunesLabelY,  "MidRunesLabelY",  245);
    R(g_collMidRunesGridX,   "MidRunesGridX",   70);
    R(g_collMidRunesGridY,   "MidRunesGridY",   263);
    R(g_collHighRunesLabelX, "HighRunesLabelX", 511);
    R(g_collHighRunesLabelY, "HighRunesLabelY", 125);
    R(g_collHighRunesGridX,  "HighRunesGridX",  393);
    R(g_collHighRunesGridY,  "HighRunesGridY",  143);
    R(g_collTopRunesLabelX,  "TopRunesLabelX",  511);
    R(g_collTopRunesLabelY,  "TopRunesLabelY",  245);
    R(g_collTopRunesGridX,   "TopRunesGridX",   441);
    R(g_collTopRunesGridY,   "TopRunesGridY",   263);

    R(g_collGemsLpHdrX,    "GemsLpHdrX",   138);
    R(g_collGemsLpHdrY,    "GemsLpHdrY",   108);
    R(g_collGemsLpLabelX,  "GemsLpLabelX", 70);
    R(g_collGemsLpGridX,   "GemsLpGridX",  138);
    R(g_collGemsLpRowY,    "GemsLpRowY",   131);
    R(g_collGemsLpRowSp,   "GemsLpRowSp",  56);
    R(g_collGemsRpHdrX,    "GemsRpHdrX",   462);
    R(g_collGemsRpHdrY,    "GemsRpHdrY",   108);
    R(g_collGemsRpLabelX,  "GemsRpLabelX", 394);
    R(g_collGemsRpGridX,   "GemsRpGridX",  462);
    R(g_collGemsRpRowY,    "GemsRpRowY",   131);
    R(g_collGemsRpRowSp,   "GemsRpRowSp",  56);

    R(g_collSpecHdrX,      "SpecHdrX",      188);
    R(g_collSpecHdrY,      "SpecHdrY",      108);
    R(g_collSpecGridX,     "SpecGridX",     67);
    R(g_collSpecGridY,     "SpecGridY",     142);
    R(g_collSpecLabelOffY, "SpecLabelOffY", 12);
    R(g_collSpecFooterX,   "SpecFooterX",   188);
    R(g_collSpecFooterY,   "SpecFooterY",   390);
    R(g_collGoldHdrX,      "GoldHdrX",      511);
    R(g_collGoldHdrY,      "GoldHdrY",      108);
    R(g_collGoldCounterX,  "GoldCounterX",  511);
    R(g_collGoldExplX,     "GoldExplX",     511);
    R(g_collGoldGoalX,     "GoldGoalX",     511);
    #undef R
}

/* Hover state — populated during cell rendering, consumed at end of frame */
static int          s_hoverSlotIdx = -1;
static int          s_hoverX       = 0;
static int          s_hoverY       = 0;
static const char*  s_hoverExtra   = NULL;  /* additional context line */

/* External D2Win drawing primitives (file-scope in unity build) */
extern void  (__fastcall *fnText)(const wchar_t*, int, int, int, int);
extern void  (__fastcall *fnFont)(int);
extern void  (__stdcall  *fnRect)(int, int, int, int, int, int);
extern void* (__fastcall *fnCelLoad)(const char*, int);
extern void  (__fastcall *fnCelDraw)(void*, int, int, int, int, int);

static BOOL InRectColl(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

/* Convert ASCII string to wide and write into a wchar_t buffer.
 * outSize must be at least the max chars including NUL. */
static void Coll_AsciiToWide(const char* in, wchar_t* out, int outSize) {
    if (!in || !out || outSize <= 0) return;
    int i = 0;
    for (; in[i] && i < outSize - 1; i++) out[i] = (wchar_t)(unsigned char)in[i];
    out[i] = 0;
}

/* Format a uint64 with thousand-separators ("1,234,567"). */
static void Coll_FormatGold(uint64_t v, wchar_t* out, int outSize) {
    char tmp[32];
    /* %llu prints the value */
    _snprintf(tmp, sizeof(tmp) - 1, "%llu", (unsigned long long)v);
    tmp[sizeof(tmp) - 1] = 0;

    int len = (int)strlen(tmp);
    int sepCount = (len - 1) / 3;
    int outLen   = len + sepCount;
    if (outLen >= outSize) outLen = outSize - 1;

    int srcIdx = len - 1;
    int dstIdx = outLen - 1;
    int counter = 0;
    while (srcIdx >= 0 && dstIdx >= 0) {
        if (counter == 3) {
            out[dstIdx--] = L',';
            counter = 0;
            continue;
        }
        out[dstIdx--] = (wchar_t)(unsigned char)tmp[srcIdx--];
        counter++;
    }
    out[outLen] = 0;
}

/* Try to load the inv DC6 for a slot; cache via a parallel pointer
 * array so we don't reload every frame. (void*)-1 means "tried, failed". */
static void* s_celCache[COLL_NUM_SLOTS];
static BOOL  s_celCacheInit = FALSE;

/* 1.9.0 — runtime invFile override per slot. Populated lazily by
 * Coll_BuildInvFileOverrides() when sgptDataTables is reachable.
 * Empty string = no override (use catalog placeholder). */
static char  s_invFileOverride[COLL_NUM_SLOTS][32];
static BOOL  s_invFileOverridesBuilt = FALSE;

/* Locate sgptDataTables — same approach as d2arch_shuffle.c. Returns
 * 0 if D2Common isn't loaded yet. */
static DWORD Coll_GetSgptDT(void) {
    static DWORD s_dt = 0;
    if (s_dt) return s_dt;
    HMODULE hCommon = GetModuleHandleA("D2Common.dll");
    if (!hCommon) return 0;
    DWORD* p = (DWORD*)GetProcAddress(hCommon, (LPCSTR)10042);
    if (p) {
        __try { s_dt = *p; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return s_dt;
}

/* sgptDataTables offsets — see top of file for #defines (moved up so
 * Coll_BuildItemMaps can reference them).
 *
 * D2SetItemsTxt struct (size = 0x1B8 / 440 bytes):
 *   +0x02 = char szName[32]
 *   +0x62 = char szInvFile[32]
 *
 * D2ItemsTxt struct (size = 0x1A8 / 424 bytes):
 *   +0x20 = char szInvFile[32]
 *   +0x80 = uint32 dwCode (4 ASCII chars)
 */

static void Coll_BuildInvFileOverrides(void) {
    if (s_invFileOverridesBuilt) return;
    DWORD dt = Coll_GetSgptDT();
    if (!dt) return;  /* try again next frame */

    /* === Set pieces: match by szName from pSetItemsTxt === */
    DWORD pSetTxt = 0;
    int   nSetRec = 0;
    __try {
        pSetTxt = *(DWORD*)(dt + COLL_DT_PSETITEMSTXT);
        nSetRec = *(int*)  (dt + COLL_DT_NSETITEMSTXTREC);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pSetTxt || nSetRec <= 0 || nSetRec > 256) return;

    int matchedSet = 0;
    for (int s = 0; s < COLL_NUM_SET_PIECES; s++) {
        const char* myName = g_collSetPieces[s].displayName;
        if (!myName || !myName[0]) continue;
        for (int r = 0; r < nSetRec; r++) {
            char* row = (char*)(pSetTxt + r * COLL_SETITEMS_STRIDE);
            char  rowName[34] = {0};
            char  rowInv[34]  = {0};
            __try {
                memcpy(rowName, row + COLL_SETITEMS_OFF_NAME, 32);
                memcpy(rowInv,  row + COLL_SETITEMS_OFF_INV,  32);
            } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
            rowName[32] = 0;
            rowInv[32]  = 0;
            if (strncmp(myName, rowName, 32) == 0 && rowInv[0]) {
                /* Append .dc6 if not already present (D2 normally stores
                 * raw basename; fnCelLoad expects path including extension
                 * OR just basename — both work depending on the loader. */
                _snprintf(s_invFileOverride[COLL_SLOT_SETS_BASE + s], 31, "%s", rowInv);
                s_invFileOverride[COLL_SLOT_SETS_BASE + s][31] = 0;
                matchedSet++;
                break;
            }
        }
    }

    /* === Runes / Gems / Specials: match by dwCode from pItemsTxt === */
    DWORD pItemsTxt = 0;
    int   nItemsRec = 0;
    __try {
        DWORD pItemDataTbl = dt + COLL_DT_ITEMDATATBL;
        nItemsRec = *(int*)  (pItemDataTbl + 0x00);
        pItemsTxt = *(DWORD*)(pItemDataTbl + 0x04);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!pItemsTxt || nItemsRec <= 0 || nItemsRec > 4096) {
        s_invFileOverridesBuilt = TRUE;
        Log("Coll_BuildInvFileOverrides: %d/%d set invFiles resolved (no pItemsTxt for runes/gems/specials)\n",
            matchedSet, COLL_NUM_SET_PIECES);
        return;
    }

    int matchedNonSet = 0;
    /* Walk pItemsTxt once, look up each row's dwCode against our 3
     * non-set catalogs. O(N*M) but N≈800 rows, M≈78 catalog entries
     * → ~60K compares; one-time cost at first scan. */
    for (int r = 0; r < nItemsRec; r++) {
        char* row = (char*)(pItemsTxt + r * COLL_ITEMS_STRIDE);
        DWORD rowCode = 0;
        char  rowInv[34] = {0};
        __try {
            rowCode = *(DWORD*)(row + COLL_ITEMS_OFF_DWCODE);
            memcpy(rowInv, row + COLL_ITEMS_OFF_INV, 32);
        } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!rowCode || !rowInv[0]) continue;
        rowInv[32] = 0;

        /* Compare rowCode against our 3 non-set catalogs. */
        for (int i = 0; i < COLL_NUM_RUNES; i++) {
            if (CodeToDword(g_collRunes[i].code) == rowCode) {
                _snprintf(s_invFileOverride[COLL_SLOT_RUNES_BASE + i], 31, "%s", rowInv);
                s_invFileOverride[COLL_SLOT_RUNES_BASE + i][31] = 0;
                matchedNonSet++;
                break;
            }
        }
        for (int i = 0; i < COLL_NUM_GEMS; i++) {
            if (CodeToDword(g_collGems[i].code) == rowCode) {
                _snprintf(s_invFileOverride[COLL_SLOT_GEMS_BASE + i], 31, "%s", rowInv);
                s_invFileOverride[COLL_SLOT_GEMS_BASE + i][31] = 0;
                matchedNonSet++;
                break;
            }
        }
        for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
            if (CodeToDword(g_collSpecials[i].code) == rowCode) {
                _snprintf(s_invFileOverride[COLL_SLOT_SPECIALS_BASE + i], 31, "%s", rowInv);
                s_invFileOverride[COLL_SLOT_SPECIALS_BASE + i][31] = 0;
                matchedNonSet++;
                break;
            }
        }
    }

    s_invFileOverridesBuilt = TRUE;
    Log("Coll_BuildInvFileOverrides: sets=%d/%d non-set=%d/%d (runes+gems+specials)\n",
        matchedSet, COLL_NUM_SET_PIECES,
        matchedNonSet, COLL_NUM_RUNES + COLL_NUM_GEMS + COLL_NUM_SPECIALS);
}

static const CollectionItem* Coll_GetItemForSlot(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= COLL_NUM_SLOTS) return NULL;
    if (slotIdx < COLL_SLOT_RUNES_BASE)
        return &g_collSetPieces[slotIdx - COLL_SLOT_SETS_BASE];
    if (slotIdx < COLL_SLOT_GEMS_BASE)
        return &g_collRunes[slotIdx - COLL_SLOT_RUNES_BASE];
    if (slotIdx < COLL_SLOT_SPECIALS_BASE)
        return &g_collGems[slotIdx - COLL_SLOT_GEMS_BASE];
    return &g_collSpecials[slotIdx - COLL_SLOT_SPECIALS_BASE];
}

/* DC6 file-format sanity check. fnCelLoad can return a non-NULL pointer
 * for files that aren't valid DC6 v6 (a placeholder/dummy or a different
 * format). Calling fnCelDraw on such a pointer asserts in CelCmp.cpp:1485
 * with "pFile->dwVersion == CELFILE_VERSION". We mirror STK's pattern
 * (StkCelLooksValid in d2arch_stashui.c): read the first DWORD and
 * confirm it's 6 = DC6_VERSION. SEH-guarded against bad pointers. */
static BOOL Coll_CelLooksValid(void* pCel) {
    if (!pCel) return FALSE;
    DWORD version = 0;
    __try { version = *(DWORD*)pCel; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
    return (version == 6) ? TRUE : FALSE;
}

static void* Coll_ResolveCelForSlot(int slotIdx) {
    if (!s_celCacheInit) {
        for (int i = 0; i < COLL_NUM_SLOTS; i++) s_celCache[i] = NULL;
        memset(s_invFileOverride, 0, sizeof(s_invFileOverride));
        s_celCacheInit = TRUE;
    }
    /* Build runtime invFile overrides on first call (lazy — needs
     * D2Common loaded). Repeats are cheap (just checks a flag). */
    if (!s_invFileOverridesBuilt) Coll_BuildInvFileOverrides();

    if (slotIdx < 0 || slotIdx >= COLL_NUM_SLOTS) return NULL;
    if (s_celCache[slotIdx] == (void*)-1) return NULL;
    if (s_celCache[slotIdx]) return s_celCache[slotIdx];
    if (!fnCelLoad) return NULL;

    /* Prefer the runtime-resolved name (from pSetItemsTxt/pItemsTxt)
     * over the static catalog placeholder. The override is the actual
     * D2 invFile basename — guaranteed correct for any item D2 knows
     * about. The catalog placeholder is "inv<base>" which works for
     * runes ("invrJah") and gems ("invgpr") but rarely for set pieces. */
    const char* invFile = NULL;
    if (s_invFileOverride[slotIdx][0])
        invFile = s_invFileOverride[slotIdx];
    else {
        const CollectionItem* item = Coll_GetItemForSlot(slotIdx);
        if (item && item->invFile && item->invFile[0])
            invFile = item->invFile;
    }
    if (!invFile) {
        s_celCache[slotIdx] = (void*)-1;
        return NULL;
    }

    char path1[160], path2[160];
    _snprintf(path1, sizeof(path1) - 1, "data\\global\\items\\inv\\%s", invFile);
    _snprintf(path2, sizeof(path2) - 1, "data\\global\\items\\%s",     invFile);
    path1[sizeof(path1) - 1] = 0;
    path2[sizeof(path2) - 1] = 0;

    /* Try each candidate path. fnCelLoad can return a non-NULL pointer
     * to a placeholder / wrong-format file, so we MUST validate each
     * result with Coll_CelLooksValid before accepting it. Without this
     * check, fnCelDraw would crash with the CelCmp.cpp:1485 assertion. */
    void* p = NULL;
    __try { p = fnCelLoad(path1, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { p = NULL; }
    if (!Coll_CelLooksValid(p)) {
        p = NULL;
        __try { p = fnCelLoad(path2, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { p = NULL; }
        if (!Coll_CelLooksValid(p)) p = NULL;
    }
    s_celCache[slotIdx] = p ? p : (void*)-1;
    return p;
}

/* Render a single cell at (cx, cy) of size (w, h).
 * No backdrop — content draws directly on D2's parchment book.
 * Collected: solid icon, OR gold-text fallback if no DC6.
 * Uncollected: ghost icon (trans50), OR dim text fallback. */
static void Coll_DrawCell(int slotIdx, int cx, int cy, int w, int h,
                           int mx, int my)
{
    BOOL collected = Coll_IsSlotCollected(slotIdx);

    /* 1.9.0 Phase 5.1 — search filter. When a search is active and
     * this slot doesn't match, render a faint placeholder rect and
     * skip the icon/text. Hover detection still runs at the bottom
     * so a tooltip on the dimmed cell still works. */
    BOOL filterActive = (s_collSearchBuf[0] != 0);
    BOOL passesFilter = !filterActive || Coll_SlotMatchesFilter(slotIdx);
    if (filterActive && !passesFilter) {
        /* Faint dim rect — same place where the icon would be. */
        if (fnRect) fnRect(cx, cy, cx + w, cy + h, 0, 5);
        if (InRectColl(mx, my, cx, cy, w, h)) {
            s_hoverSlotIdx = slotIdx;
            s_hoverX = mx;
            s_hoverY = my;
            s_hoverExtra = NULL;
        }
        return;
    }

    /* Try the actual D2 inv icon first. Falls back to text if the
     * DC6 doesn't load (most catalog placeholders won't resolve). */
    void* pCel = Coll_ResolveCelForSlot(slotIdx);
    if (pCel && fnCelDraw) {
        int iconX = cx + 1;
        int iconY = cy + h - 1;
        /* Mode 2 = trans50 (ghost) for uncollected, mode 5 = solid */
        int drawMode = collected ? 5 : 2;
        __try { fnCelDraw(pCel, iconX, iconY, 0, drawMode, -1); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    } else {
        /* Fallback: 3-char code label, centered.
         * Colors:
         *   collected   = 9 (bright yellow) — pops against parchment
         *   uncollected = 3 (dim grey)      — visible but ghosted
         */
        const CollectionItem* item = Coll_GetItemForSlot(slotIdx);
        if (item && item->code[0] && fnText && fnFont) {
            wchar_t lbl[8];
            Coll_AsciiToWide(item->code, lbl, 8);
            fnFont(0);
            fnText(lbl, cx + w / 2, cy + h - 6, collected ? 9 : 3, 1);
        }
    }

    /* 1.9.0 Phase 5.3 — gold-flash celebration. When this slot was
     * just first-collected (within COLL_GOLD_FLASH_MS = 1500ms), draw
     * a pulsing gold border around the cell. The pulse uses a simple
     * sawtooth on the elapsed time so the border thickens then fades.
     * Drawn AFTER the icon so it overlays everything. */
    if (slotIdx >= 0 && slotIdx < COLL_NUM_SLOTS) {
        DWORD flashStart = s_collJustCollectedTick[slotIdx];
        if (flashStart != 0) {
            DWORD elapsed = GetTickCount() - flashStart;
            if (elapsed < COLL_GOLD_FLASH_MS) {
                /* Pulse 3x at ~6Hz: thickness alternates 1↔3 px.
                 * (elapsed / 167) gives ~9 frames over 1.5s; & 1
                 * gives the on/off alternation. */
                int thick = ((elapsed / 167) & 1) ? 3 : 1;
                if (fnRect) {
                    /* Color 4 = gold in D2's palette. Border-only so
                     * it doesn't obscure the icon. */
                    fnRect(cx,             cy,             cx + w,        cy + thick,    4, 5);
                    fnRect(cx,             cy + h - thick, cx + w,        cy + h,        4, 5);
                    fnRect(cx,             cy,             cx + thick,    cy + h,        4, 5);
                    fnRect(cx + w - thick, cy,             cx + w,        cy + h,        4, 5);
                }
            } else {
                /* Timer expired — clear so subsequent ticks skip the
                 * compare and we don't keep redrawing forever. */
                s_collJustCollectedTick[slotIdx] = 0;
            }
        }
    }

    /* Hover detection */
    if (InRectColl(mx, my, cx, cy, w, h)) {
        s_hoverSlotIdx = slotIdx;
        s_hoverX = mx;
        s_hoverY = my;
        s_hoverExtra = NULL;
    }
}

/* Title text — drawn directly on D2's existing F1 book parchment,
 * centered across both pages at the top (matches Quest Book's full-
 * width layout). No backdrop. */
static void Coll_DrawPanelFrame(const wchar_t* title) {
    if (fnText && fnFont && title) {
        fnFont(1);
        fnText(title, g_collTitleX, COLL_TITLE_Y, 4, 1);
    }
}

/* Draw a "X / Y" progress label */
static void Coll_DrawProgress(int x, int y, int collected, int total, int color) {
    if (!fnText || !fnFont) return;
    char buf[32];
    _snprintf(buf, sizeof(buf) - 1, "%d / %d", collected, total);
    buf[sizeof(buf) - 1] = 0;
    wchar_t wbuf[32];
    Coll_AsciiToWide(buf, wbuf, 32);
    fnFont(0);
    fnText(wbuf, x, y, color, 0);
}

/* Count flagged slots in a range */
static int Coll_CountCollectedInRange(int firstSlot, int lastSlot) {
    int count = 0;
    for (int i = firstSlot; i <= lastSlot; i++) {
        if (Coll_IsSlotCollected(i)) count++;
    }
    return count;
}

/* Render one set row inside a given book-page column.
 * nameX = X for the set name text (left edge of name col)
 * progX = X for the "X / Y" progress text (right-edge anchor, left-aligned) */
static void Coll_DrawSetRowInPage(int s, int pageX, int pageW,
                                    int nameX, int progX,
                                    int rowY, int mx, int my) {
    if (s < 0 || s >= COLL_NUM_SETS) return;
    const CollectionSet* set = &g_collSets[s];
    if (!set->name || set->pieceCount == 0) return;

    /* 1.9.0 Phase 5.1 — search filter. If a filter is active and
     * neither the set name nor any piece name matches, skip the
     * row entirely (don't render). The visual gap reads as "filter
     * is hiding things" without being a separate "not matching"
     * state on every row. */
    if (s_collSearchBuf[0] != 0 && !Coll_SetMatchesFilter(s)) {
        return;
    }

    int setFirstSlot = COLL_SLOT_SETS_BASE + set->firstSlot;
    int setLastSlot  = setFirstSlot + set->pieceCount - 1;
    int gotPieces    = Coll_CountCollectedInRange(setFirstSlot, setLastSlot);
    (void)pageW;

    wchar_t wname[64];
    Coll_AsciiToWide(set->name, wname, 64);
    fnFont(0);
    int nameColor = (gotPieces == set->pieceCount) ? 4 :
                    (gotPieces > 0)               ? 9 : 0;
    fnText(wname, nameX, rowY, nameColor, 0);

    Coll_DrawProgress(progX, rowY, gotPieces, set->pieceCount,
                      (gotPieces == set->pieceCount) ? 4 :
                      (gotPieces > 0)               ? 9 : 7);

    /* Hover for tooltip */
    if (InRectColl(mx, my, pageX, rowY - 10, pageW, 22)) {
        s_hoverSlotIdx = setFirstSlot;
        s_hoverX = mx; s_hoverY = my;
    }
}

/* === Sets pages (3 = sets 0..15, 4 = sets 16..31) ===
 * Two-page split: 8 sets per book-page = 16 sets per F1-page.
 *   LEFT page  = sets[firstSet+0 .. firstSet+7]
 *   RIGHT page = sets[firstSet+8 .. firstSet+15]
 * Each row: Set Name (left) | X / Y (right). Class shown in hover tooltip.
 */
static void Coll_RenderSetsPage(int firstSet, int lastSet, int mx, int my) {
    int rowH = g_collSetsRowH;

    /* Header row labels — positions individually tunable per page */
    if (fnText && fnFont) {
        fnFont(0);
        wchar_t hdr1[20], hdr3[20];
        Coll_AsciiToWide("Set Name", hdr1, 20);
        Coll_AsciiToWide("Found",    hdr3, 20);
        fnText(hdr1, g_collSetsLpNameX,  g_collSetsLpHdrY, 7, 0);
        fnText(hdr3, g_collSetsLpFoundX, g_collSetsLpHdrY, 7, 0);
        fnText(hdr1, g_collSetsRpNameX,  g_collSetsRpHdrY, 7, 0);
        fnText(hdr3, g_collSetsRpFoundX, g_collSetsRpHdrY, 7, 0);
    }

    /* Up to 8 sets per page */
    int total = lastSet - firstSet;
    int half  = total / 2;
    if (half < 1) half = 1;

    for (int i = 0; i < half && firstSet + i < lastSet; i++) {
        int rowY = g_collSetsRowsY + i * rowH;
        Coll_DrawSetRowInPage(firstSet + i, COLL_LP_X, COLL_LP_W,
                              g_collSetsLpNameX, g_collSetsLpFoundX, rowY, mx, my);
    }
    for (int i = 0; i < (total - half) && firstSet + half + i < lastSet; i++) {
        int rowY = g_collSetsRowsY + i * rowH;
        Coll_DrawSetRowInPage(firstSet + half + i, COLL_RP_X, COLL_RP_W,
                              g_collSetsRpNameX, g_collSetsRpFoundX, rowY, mx, my);
    }

    /* Footer: count of fully-completed sets across all 32 (regardless of
     * which page we're on). Centered between pages. */
    int completedSets = 0;
    for (int s = 0; s < COLL_NUM_SETS; s++) {
        const CollectionSet* set = &g_collSets[s];
        if (!set->name || set->pieceCount == 0) continue;
        int sFirst = COLL_SLOT_SETS_BASE + set->firstSlot;
        int sLast  = sFirst + set->pieceCount - 1;
        if (Coll_CountCollectedInRange(sFirst, sLast) == set->pieceCount)
            completedSets++;
    }
    if (fnText && fnFont) {
        fnFont(0);
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "Sets Complete: %d / %d",
                  completedSets, COLL_NUM_SETS);
        buf[sizeof(buf) - 1] = 0;
        wchar_t wbuf[40];
        Coll_AsciiToWide(buf, wbuf, 40);
        fnText(wbuf, g_collFooterX, COLL_FOOTER_Y,
               (completedSets == COLL_NUM_SETS) ? 4 : 9, 1);
    }
}

/* === Runes page (5 = all 33 runes) ===
 * Two-page split using full vertical space:
 *
 *   Y 125  | Section A label
 *   Y 145  | Section A grid (5×2)
 *   Y 245  | Section B label
 *   Y 265  | Section B grid (5×2 or 3 centered)
 *   Y 390  | Footer progress
 *
 *   LEFT page sections: Low (A) + Mid (B)
 *   RIGHT page sections: High (A) + Top (B, just 3 cells centered)
 */
static void Coll_RenderRunesPage(int mx, int my) {
    int cellW = g_collRunesCellW;
    int cellH = g_collRunesCellH;
    int gap   = g_collRunesGap;
    int colsPerRow = 5;

    /* === Low Runes (10 cells) === */
    if (fnText && fnFont) {
        fnFont(0);
        wchar_t lbl[24];
        Coll_AsciiToWide("Low Runes (El..Thul)", lbl, 24);
        fnText(lbl, g_collLowRunesLabelX, g_collLowRunesLabelY, 4, 1);
    }
    for (int i = 0; i < 10; i++) {
        int row = i / colsPerRow;
        int col = i % colsPerRow;
        int cx = g_collLowRunesGridX + col * (cellW + gap);
        int cy = g_collLowRunesGridY + row * (cellH + gap);
        Coll_DrawCell(COLL_SLOT_RUNES_BASE + i, cx, cy, cellW, cellH, mx, my);
    }

    /* === Mid Runes (10 cells) === */
    if (fnText && fnFont) {
        fnFont(0);
        wchar_t lbl[24];
        Coll_AsciiToWide("Mid Runes (Amn..Lem)", lbl, 24);
        fnText(lbl, g_collMidRunesLabelX, g_collMidRunesLabelY, 4, 1);
    }
    for (int i = 0; i < 10; i++) {
        int row = i / colsPerRow;
        int col = i % colsPerRow;
        int cx = g_collMidRunesGridX + col * (cellW + gap);
        int cy = g_collMidRunesGridY + row * (cellH + gap);
        Coll_DrawCell(COLL_SLOT_RUNES_BASE + 10 + i, cx, cy, cellW, cellH, mx, my);
    }

    /* === High Runes (10 cells) === */
    if (fnText && fnFont) {
        fnFont(0);
        wchar_t lbl[24];
        Coll_AsciiToWide("High Runes (Pul..Ber)", lbl, 24);
        fnText(lbl, g_collHighRunesLabelX, g_collHighRunesLabelY, 4, 1);
    }
    for (int i = 0; i < 10; i++) {
        int row = i / colsPerRow;
        int col = i % colsPerRow;
        int cx = g_collHighRunesGridX + col * (cellW + gap);
        int cy = g_collHighRunesGridY + row * (cellH + gap);
        Coll_DrawCell(COLL_SLOT_RUNES_BASE + 20 + i, cx, cy, cellW, cellH, mx, my);
    }

    /* === Top Runes (3 cells) === */
    if (fnText && fnFont) {
        fnFont(0);
        wchar_t lbl[24];
        Coll_AsciiToWide("Top Runes (Jah Cham Zod)", lbl, 24);
        fnText(lbl, g_collTopRunesLabelX, g_collTopRunesLabelY, 4, 1);
    }
    for (int i = 0; i < 3; i++) {
        int cx = g_collTopRunesGridX + i * (cellW + gap);
        Coll_DrawCell(COLL_SLOT_RUNES_BASE + 30 + i, cx, g_collTopRunesGridY, cellW, cellH, mx, my);
    }

    /* Progress summary centered between pages at the bottom */
    int got = Coll_CountCollectedInRange(COLL_SLOT_RUNES_BASE,
                                         COLL_SLOT_RUNES_BASE + COLL_NUM_RUNES - 1);
    if (fnText && fnFont) {
        fnFont(0);
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "Runes: %d / %d", got, COLL_NUM_RUNES);
        buf[sizeof(buf) - 1] = 0;
        wchar_t wbuf[40];
        Coll_AsciiToWide(buf, wbuf, 40);
        fnText(wbuf, g_collFooterX, COLL_FOOTER_Y, (got == COLL_NUM_RUNES) ? 4 : 9, 1);
    }
}

/* === Gems page (6 = 35 = 7 colors × 5 grades) ===
 * Two-page split:
 *   LEFT page  = Amethyst, Topaz, Sapphire, Emerald (4 colors)
 *   RIGHT page = Ruby, Diamond, Skull             (3 colors)
 *
 * Each row: color label (left) + 5 cells (Chip..Perfect).
 * The whole block (label + cells) is centered horizontally inside its
 * page so there's no big empty area on either side. Vertical layout
 * uses the full Y=125..380 range with comfortable spacing.
 */
static void Coll_RenderGemsPage(int mx, int my) {
    int cellW = g_collGemsCellW;
    int cellH = g_collGemsCellH;
    int gap   = g_collGemsGap;

    static const char* colorNames[7] = {
        "Amethyst", "Topaz", "Sapphire", "Emerald", "Ruby", "Diamond", "Skull"
    };

    /* Per-page positions read from INI keys for full layout control */
    int lpLabelX = g_collGemsLpLabelX;
    int lpGridX  = g_collGemsLpGridX;
    int rpLabelX = g_collGemsRpLabelX;
    int rpGridX  = g_collGemsRpGridX;

    /* Grade headers above each grid — rendered as ONE dashed string
     * ("Chip-Flawd-Norm-Flwls-Prfct") instead of 5 column-centered
     * abbreviations, so the words don't visually merge into each
     * other when cell widths are tight. Centered over the grid
     * (mode 1 = center alignment), so the band stays at the same
     * effective position as before — the X anchor is the grid mid-
     * point, computed from the existing HdrX (left edge of grid)
     * plus half the grid's total width. */
    if (fnText && fnFont) {
        wchar_t wHdr[64];
        Coll_AsciiToWide("Chip-Flawd-Norm-Flwls-Prfct", wHdr, 64);
        int gridCenterOffX = (cellW * 5 + gap * 4) / 2;
        fnFont(0);
        fnText(wHdr, g_collGemsLpHdrX + gridCenterOffX, g_collGemsLpHdrY, 4, 1);
        fnText(wHdr, g_collGemsRpHdrX + gridCenterOffX, g_collGemsRpHdrY, 4, 1);
    }

    /* LEFT PAGE: 4 colors */
    for (int c = 0; c < 4; c++) {
        int rowY = g_collGemsLpRowY + c * g_collGemsLpRowSp;
        if (fnText && fnFont) {
            wchar_t wcol[16];
            Coll_AsciiToWide(colorNames[c], wcol, 16);
            fnFont(0);
            fnText(wcol, lpLabelX, rowY + cellH / 2 + 4, 9, 0);
        }
        for (int grade = 0; grade < 5; grade++) {
            int idx = c * 5 + grade;
            int cx = lpGridX + grade * (cellW + gap);
            Coll_DrawCell(COLL_SLOT_GEMS_BASE + idx, cx, rowY, cellW, cellH, mx, my);
        }
    }

    /* RIGHT PAGE: 3 colors */
    for (int c = 0; c < 3; c++) {
        int rowY = g_collGemsRpRowY + c * g_collGemsRpRowSp;
        int colorIdx = 4 + c;
        if (fnText && fnFont) {
            wchar_t wcol[16];
            Coll_AsciiToWide(colorNames[colorIdx], wcol, 16);
            fnFont(0);
            fnText(wcol, rpLabelX, rowY + cellH / 2 + 4, 9, 0);
        }
        for (int grade = 0; grade < 5; grade++) {
            int idx = colorIdx * 5 + grade;
            int cx = rpGridX + grade * (cellW + gap);
            Coll_DrawCell(COLL_SLOT_GEMS_BASE + idx, cx, rowY, cellW, cellH, mx, my);
        }
    }

    /* Progress summary at bottom — centered between pages */
    int got = Coll_CountCollectedInRange(COLL_SLOT_GEMS_BASE,
                                         COLL_SLOT_GEMS_BASE + COLL_NUM_GEMS - 1);
    if (fnText && fnFont) {
        fnFont(0);
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "Gems: %d / %d", got, COLL_NUM_GEMS);
        buf[sizeof(buf) - 1] = 0;
        wchar_t wbuf[40];
        Coll_AsciiToWide(buf, wbuf, 40);
        fnText(wbuf, g_collFooterX, COLL_FOOTER_Y, (got == COLL_NUM_GEMS) ? 4 : 9, 1);
    }
}

/* === Specials + Gold page (7) ===
 * Two-page split, both pages use the FULL vertical space:
 *   LEFT page  = "Special Items" centered top, 5x2 grid centered, count footer
 *   RIGHT page = "Lifetime Gold Earned" — header, big counter, sub-line,
 *                 optional goal target — all centered and stacked
 */
static void Coll_RenderSpecialsAndGoldPage(int mx, int my) {
    /* === LEFT PAGE: Special Items === */
    {
        int cellW = g_collSpecCellW;
        int cellH = g_collSpecCellH;
        int gap   = g_collSpecGap;
        int colsPerRow = 5;
        int rowH = g_collSpecRowH;
        (void)colsPerRow;
        if (fnText && fnFont) {
            fnFont(0);
            wchar_t hdr[24]; Coll_AsciiToWide("Special Items", hdr, 24);
            fnText(hdr, g_collSpecHdrX, g_collSpecHdrY, 4, 1);
        }

        /* Grid origin from INI keys. Cells fan out from g_collSpecGridX/Y */
        for (int i = 0; i < COLL_NUM_SPECIALS; i++) {
            int row = i / 5;
            int col = i % 5;
            int cx = g_collSpecGridX + col * (cellW + gap);
            int cy = g_collSpecGridY + row * rowH;
            Coll_DrawCell(COLL_SLOT_SPECIALS_BASE + i, cx, cy, cellW, cellH, mx, my);

            static const char* shortNames[10] = {
                "Terror", "Hate", "Destr",
                "Brain", "Horn",  "Eye",
                "Suff",   "Hate",  "Terr",
                "Torch"
            };
            if (fnText && fnFont) {
                wchar_t wlbl[16];
                Coll_AsciiToWide(shortNames[i], wlbl, 16);
                fnFont(0);
                fnText(wlbl, cx + cellW / 2, cy + cellH + g_collSpecLabelOffY,
                              Coll_IsSlotCollected(COLL_SLOT_SPECIALS_BASE + i) ? 4 : 9, 1);
            }
        }

        /* Specials progress footer — own X/Y */
        int sgot = Coll_CountCollectedInRange(COLL_SLOT_SPECIALS_BASE,
                                              COLL_SLOT_SPECIALS_BASE + COLL_NUM_SPECIALS - 1);
        if (fnText && fnFont) {
            fnFont(0);
            char buf[24];
            _snprintf(buf, sizeof(buf) - 1, "Specials: %d / %d", sgot, COLL_NUM_SPECIALS);
            buf[sizeof(buf) - 1] = 0;
            wchar_t wbuf[24];
            Coll_AsciiToWide(buf, wbuf, 24);
            fnText(wbuf, g_collSpecFooterX, g_collSpecFooterY,
                         (sgot == COLL_NUM_SPECIALS) ? 4 : 9, 1);
        }
    }

    /* === RIGHT PAGE: Lifetime Gold Earned === */
    {
        if (fnText && fnFont) {
            fnFont(0);
            wchar_t hdr[32]; Coll_AsciiToWide("Lifetime Gold Earned", hdr, 32);
            fnText(hdr, g_collGoldHdrX, g_collGoldHdrY, 4, 1);

            wchar_t wgold[40];
            Coll_FormatGold(g_collState.goldEarned, wgold, 40);
            int len = (int)wcslen(wgold);
            if (len < 36) {
                wgold[len++] = L' ';
                wgold[len++] = L'g';
                wgold[len++] = L'p';
                wgold[len]   = 0;
            }
            fnFont(1);
            fnText(wgold, g_collGoldCounterX, g_collGoldCounterY, 4, 1);

            fnFont(0);
            wchar_t expl[40];
            Coll_AsciiToWide("(pickup + quests only)", expl, 40);
            fnText(expl, g_collGoldExplX, g_collGoldExplY, 7, 1);

            /* Goal target line at INI-tunable position */
            if (g_collGoal.goldTarget > 0) {
                wchar_t wtarg[40], wprogress[80];
                Coll_FormatGold(g_collGoal.goldTarget, wtarg, 40);
                wchar_t wcur[40];
                Coll_FormatGold(g_collState.goldEarned, wcur, 40);
                _snwprintf(wprogress, 79, L"%s / %s", wcur, wtarg);
                wprogress[79] = 0;
                fnFont(0);
                fnText(L"Goal Target:", g_collGoldGoalX, g_collGoldGoalY, 4, 1);
                fnText(wprogress,       g_collGoldGoalX, g_collGoldGoalY + 18,
                       (g_collState.goldEarned >= g_collGoal.goldTarget) ? 4 : 9, 1);
            } else {
                fnText(L"(no target set)", g_collGoldGoalX, g_collGoldGoalY, 7, 1);
            }

            /* 1.9.0 — Bonus check: gold milestone strip. Only renders
             * when check_gold_milestones is enabled in slot_data /
             * d2arch.ini. Layout: 17 milestones in 4 columns below
             * the goal target line. ✓ for fired, ☐ for unfired.
             * All offsets/sizes are INI-tunable via [GoldMilestoneStrip]
             * in d2arch.ini. Loaded once on first paint. */
            extern BOOL Bonus_IsCategoryEnabled(int category);
            extern BOOL Bonus_IsGoldMilestoneFired(int idx);
            extern uint64_t Bonus_GetGoldMilestoneThreshold(int idx);
            if (Bonus_IsCategoryEnabled(5 /* BX_GOLD_MS */)) {
                /* 1.9.0 — three VERTICAL columns side by side, one per
                 * difficulty. Horizontal layout overflowed the right
                 * page (Normal had 7 cells × 75px = 525px, way past the
                 * 254px right-page width). Vertical stack keeps every
                 * milestone inside the page bounds. All offsets INI-
                 * tunable via [GoldMilestoneStrip]. Markers: '+' fired,
                 * '-' pending (D2's bitmap font garbles U+2713 / U+00B7). */
                static int s_gmInitDone = 0;
                static int s_gmStripOffY = 50;
                static int s_gmHdrToGridY = 18;
                static int s_gmColW = 75;
                static int s_gmRowH = 14;
                INI_HOT_RELOAD_GUARD(s_gmInitDone);
                if (!s_gmInitDone) {
                    s_gmInitDone = 1;
                    char ini[MAX_PATH];
                    GetArchDir(ini, MAX_PATH);
                    strcat(ini, "d2arch.ini");
                    s_gmStripOffY    = GetPrivateProfileIntA("GoldMilestoneStrip", "OffsetY",      50, ini);
                    s_gmHdrToGridY   = GetPrivateProfileIntA("GoldMilestoneStrip", "HeaderGapY",   18, ini);
                    s_gmColW         = GetPrivateProfileIntA("GoldMilestoneStrip", "ColumnWidth",  75, ini);
                    s_gmRowH         = GetPrivateProfileIntA("GoldMilestoneStrip", "RowHeight",    14, ini);
                }
                fnFont(0);
                int curY = g_collGoldGoalY + s_gmStripOffY;
                wchar_t hdr2[32]; Coll_AsciiToWide("Gold Milestones:", hdr2, 32);
                fnText(hdr2, g_collGoldGoalX, curY, 4, 1);
                curY += s_gmHdrToGridY;

                /* Three difficulty groups arranged as vertical columns. */
                static const int diffStarts[4] = { 0, 7, 12, 17 };
                static const char* diffLabels[3] = { "Normal", "Nightmare", "Hell" };
                int colBaseX = g_collGoldGoalX - s_gmColW;
                for (int d = 0; d < 3; d++) {
                    int colX = colBaseX + d * s_gmColW;
                    int rowY = curY;
                    wchar_t wlbl[16];
                    Coll_AsciiToWide(diffLabels[d], wlbl, 16);
                    fnText(wlbl, colX, rowY, 4, 1);
                    rowY += s_gmRowH;
                    for (int i = diffStarts[d]; i < diffStarts[d + 1]; i++) {
                        BOOL fired = Bonus_IsGoldMilestoneFired(i);
                        uint64_t thr = Bonus_GetGoldMilestoneThreshold(i);
                        char asc[24];
                        const char* mark = fired ? "+" : "-";
                        if (thr >= 1000000ull) {
                            if (thr % 1000000ull == 0)
                                _snprintf(asc, sizeof(asc) - 1, "%s %lluM",
                                          mark, (unsigned long long)(thr / 1000000ull));
                            else
                                _snprintf(asc, sizeof(asc) - 1, "%s %.1fM",
                                          mark, (double)thr / 1000000.0);
                        } else {
                            _snprintf(asc, sizeof(asc) - 1, "%s %lluK",
                                      mark, (unsigned long long)(thr / 1000ull));
                        }
                        asc[sizeof(asc) - 1] = 0;
                        wchar_t wbuf[24];
                        Coll_AsciiToWide(asc, wbuf, 24);
                        fnText(wbuf, colX, rowY, fired ? 4 : 7, 1);
                        rowY += s_gmRowH;
                    }
                }
            }
        }
    }
}

/* Tooltip overlay — drawn last so it sits above everything */
static void Coll_RenderTooltip(int mx, int my) {
    if (s_hoverSlotIdx < 0) return;
    const CollectionItem* item = Coll_GetItemForSlot(s_hoverSlotIdx);
    if (!item || !item->displayName) return;

    BOOL collected = Coll_IsSlotCollected(s_hoverSlotIdx);

    /* Compose 4 lines of text:
     *   line1: item name
     *   line2: status (Collected: <date>) or "Not yet collected"
     *   line3: drop source
     *   line4: required level */
    wchar_t line1[80], line2[80], line3[120], line4[40];
    Coll_AsciiToWide(item->displayName, line1, 80);

    if (collected) {
        uint32_t ts = g_collState.timestamps[s_hoverSlotIdx];
        if (ts > 0) {
            time_t tt = (time_t)ts;
            struct tm* tm = localtime(&tt);
            char dbuf[40];
            if (tm) {
                _snprintf(dbuf, sizeof(dbuf) - 1, "Collected %04d-%02d-%02d",
                          tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
            } else {
                _snprintf(dbuf, sizeof(dbuf) - 1, "Collected");
            }
            dbuf[sizeof(dbuf) - 1] = 0;
            Coll_AsciiToWide(dbuf, line2, 80);
        } else {
            Coll_AsciiToWide("Collected", line2, 80);
        }
    } else {
        Coll_AsciiToWide("Not yet collected", line2, 80);
    }

    if (item->dropSource && item->dropSource[0]) {
        Coll_AsciiToWide(item->dropSource, line3, 120);
    } else {
        line3[0] = 0;
    }

    char lbuf[24];
    _snprintf(lbuf, sizeof(lbuf) - 1, "Required Level: %d", item->reqLevel);
    lbuf[sizeof(lbuf) - 1] = 0;
    Coll_AsciiToWide(lbuf, line4, 40);

    /* Tooltip box geometry — all 4 values INI-tunable via [Collection]
     * TooltipW/H/OffsetX/OffsetY so the user can move the box away
     * from the cursor (default offset was +12/+12 which puts the box
     * directly under the cursor and obscures the item being inspected).
     * Increased default offset to +20/+24 so the cursor sits clear of
     * the box's top-left corner. Box clamps to screen bounds.
     * g_screenW/H are file-scope statics from d2arch_input.c. */
    static int loadedTip = 0;
    static int s_tipW = 290, s_tipH = 80, s_tipOffX = 20, s_tipOffY = 24;
    INI_HOT_RELOAD_GUARD(loadedTip);
    if (!loadedTip) {
        loadedTip = 1;
        char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
        s_tipW    = GetPrivateProfileIntA("Collection", "TooltipW",       290, ini);
        s_tipH    = GetPrivateProfileIntA("Collection", "TooltipH",        80, ini);
        s_tipOffX = GetPrivateProfileIntA("Collection", "TooltipOffsetX",  20, ini);
        s_tipOffY = GetPrivateProfileIntA("Collection", "TooltipOffsetY",  24, ini);
    }
    int boxW = s_tipW;
    int boxH = s_tipH;
    int boxX = mx + s_tipOffX;
    int boxY = my + s_tipOffY;
    if (boxX + boxW > g_screenW) boxX = g_screenW - boxW - 8;
    if (boxY + boxH > g_screenH) boxY = g_screenH - boxH - 8;
    if (boxX < 0) boxX = 0;
    if (boxY < 0) boxY = 0;

    /* Backdrop */
    for (int i = 0; i < 5; i++) fnRect(boxX, boxY, boxX + boxW, boxY + boxH, 0, 5);
    fnRect(boxX,            boxY,            boxX + boxW,     boxY + 1,        4, 5);
    fnRect(boxX,            boxY + boxH - 1, boxX + boxW,     boxY + boxH,     4, 5);
    fnRect(boxX,            boxY,            boxX + 1,        boxY + boxH,     4, 5);
    fnRect(boxX + boxW - 1, boxY,            boxX + boxW,     boxY + boxH,     4, 5);

    /* Text */
    if (fnText && fnFont) {
        fnFont(1);
        fnText(line1, boxX + 8, boxY + 18, collected ? 4 : 1, 0);
        fnFont(0);
        fnText(line2, boxX + 8, boxY + 36, collected ? 4 : 6, 0);
        if (line3[0])
            fnText(line3, boxX + 8, boxY + 52, 7, 0);
        fnText(line4, boxX + 8, boxY + 68, 7, 0);
    }
}

/* 1.9.0 Phase 5.1 — search box geometry. Single horizontal field
 * placed at the top of the Sets pages. INI-tunable via [Collection]
 * SearchX/Y/W/H. Defaults push it well left of the prior position
 * (210 -> 170 -> 130) so it stops overlapping the page title and
 * the title can sit naturally to the left without crowding. */
static int g_collSearchX = 130;
static int g_collSearchY = 48;
static int g_collSearchW = 180;
static int g_collSearchH = 18;
static void Coll_LoadSearchBoxIni(void) {
    static int loaded = 0;
    INI_HOT_RELOAD_GUARD(loaded);
    if (loaded) return;
    loaded = 1;
    char ini[MAX_PATH]; GetArchDir(ini, MAX_PATH); strcat(ini, "d2arch.ini");
    g_collSearchX = GetPrivateProfileIntA("Collection", "SearchX", 130, ini);
    g_collSearchY = GetPrivateProfileIntA("Collection", "SearchY",  48, ini);
    g_collSearchW = GetPrivateProfileIntA("Collection", "SearchW", 180, ini);
    g_collSearchH = GetPrivateProfileIntA("Collection", "SearchH",  18, ini);
}

/* Hit-test for the search box. Used by both the click-to-focus
 * logic and the keyboard handler in d2arch_main.c (extern). */
BOOL Coll_PointInSearchBox(int x, int y) {
    Coll_LoadSearchBoxIni();
    return (x >= g_collSearchX && x < g_collSearchX + g_collSearchW &&
            y >= g_collSearchY && y < g_collSearchY + g_collSearchH);
}

/* Render the search box. ONLY on the two Sets pages (g_editorPage 3+4)
 * — Runes/Gems/Specials/Gold pages don't use the filter so showing
 * the field there was just dead UI overlapping the title. */
static void Coll_RenderSearchBox(int mouseX, int mouseY, BOOL clicked) {
    Coll_LoadSearchBoxIni();
    int x = g_collSearchX, y = g_collSearchY;
    int w = g_collSearchW, h = g_collSearchH;

    /* Click-to-focus / click-elsewhere-to-blur. */
    if (clicked) {
        if (Coll_PointInSearchBox(mouseX, mouseY)) {
            s_collSearchFocus = TRUE;
        } else {
            /* Click outside the box → drop focus. */
            s_collSearchFocus = FALSE;
        }
    }

    /* Backdrop: dim black with gold border (focused) or grey border. */
    int borderColor = s_collSearchFocus ? 4 : 5;  /* 4=gold, 5=grey */
    if (fnRect) {
        for (int i = 0; i < 4; i++)
            fnRect(x, y, x + w, y + h, 0, 5);
        fnRect(x,         y,         x + w,     y + 1,     borderColor, 5);
        fnRect(x,         y + h - 1, x + w,     y + h,     borderColor, 5);
        fnRect(x,         y,         x + 1,     y + h,     borderColor, 5);
        fnRect(x + w - 1, y,         x + w,     y + h,     borderColor, 5);
    }

    /* Text contents — placeholder if empty + not focused, otherwise
     * the typed buffer + an underscore caret when focused. */
    if (fnText && fnFont) {
        char render[48];
        if (s_collSearchBuf[0] == 0 && !s_collSearchFocus) {
            _snprintf(render, sizeof(render) - 1, "Click to search...");
        } else {
            _snprintf(render, sizeof(render) - 1, "%s%s",
                      s_collSearchBuf,
                      s_collSearchFocus ? "_" : "");
        }
        render[sizeof(render) - 1] = 0;
        wchar_t wRender[48];
        int k = 0;
        for (; k < 47 && render[k]; k++)
            wRender[k] = (wchar_t)(unsigned char)render[k];
        wRender[k] = 0;
        fnFont(0);
        int textColor = (s_collSearchBuf[0] == 0 && !s_collSearchFocus) ? 5 : 0;
        fnText(wRender, x + 4, y + h - 4, textColor, 0);
    }
}

void Coll_RenderF1Page(int mouseX, int mouseY, BOOL clicked) {
    /* g_editorPage is a file-scope static from d2arch_render.c.
     * Visible here because d2arch_render.c is included before this
     * file in the unity build (see d2arch.c). */

    /* Reload layout from d2arch.ini [Collection] section every frame.
     * Lets the user tune coordinates without restarting D2 — just edit
     * the INI, close the F1 book, re-open it, and changes show up. */
    Coll_LoadIniLayout();

    /* 1.9.0 Phase 5.1 — search box at top of page. ONLY on Sets
     * pages (3+4). Other Collection tabs (Runes/Gems/Specials/Gold)
     * don't use text filtering so the field would just overlap the
     * page title. Drop focus when leaving the Sets pages too so
     * stale typed input doesn't carry across. */
    if (g_editorPage == 3 || g_editorPage == 4) {
        Coll_RenderSearchBox(mouseX, mouseY, clicked);
    } else {
        s_collSearchFocus = FALSE;
    }

    /* Reset hover state at start of frame */
    s_hoverSlotIdx = -1;

    /* Draw panel frame + per-page title */
    wchar_t title[64];
    int totalCollected = 0, totalSlots = COLL_NUM_SLOTS;
    for (int i = 0; i < COLL_NUM_SLOTS; i++)
        if (Coll_IsSlotCollected(i)) totalCollected++;

    switch (g_editorPage) {
        case 3:  Coll_AsciiToWide("Collection - Sets (1/2)", title, 64); break;
        case 4:  Coll_AsciiToWide("Collection - Sets (2/2)", title, 64); break;
        case 5:  Coll_AsciiToWide("Collection - Runes",       title, 64); break;
        case 6:  Coll_AsciiToWide("Collection - Gems",        title, 64); break;
        case 7:  Coll_AsciiToWide("Collection - Specials & Gold", title, 64); break;
        default: title[0] = 0; break;
    }
    Coll_DrawPanelFrame(title);

    /* Total counter centered directly under the title — stacked, not
     * side-by-side. Avoids overlap and the right-edge cutoff issue. */
    if (fnText && fnFont) {
        char buf[40];
        _snprintf(buf, sizeof(buf) - 1, "Total: %d / %d", totalCollected, totalSlots);
        buf[sizeof(buf) - 1] = 0;
        wchar_t wbuf[40];
        Coll_AsciiToWide(buf, wbuf, 40);
        fnFont(0);
        fnText(wbuf, g_collTotalX, COLL_TOTAL_Y,
                     (totalCollected == totalSlots) ? 4 : 7, 1);
    }

    switch (g_editorPage) {
        case 3:  Coll_RenderSetsPage(0, 16, mouseX, mouseY); break;
        case 4:  Coll_RenderSetsPage(16, 32, mouseX, mouseY); break;
        case 5:  Coll_RenderRunesPage(mouseX, mouseY); break;
        case 6:  Coll_RenderGemsPage(mouseX, mouseY); break;
        case 7:  Coll_RenderSpecialsAndGoldPage(mouseX, mouseY); break;
        default: break;
    }

    /* Tooltip last so it overlays everything */
    Coll_RenderTooltip(mouseX, mouseY);
}

/* Reset cel cache on character change so a different char's collection
 * page doesn't carry over stale loaded cels. */
void Coll_ResetCelCache(void) {
    for (int i = 0; i < COLL_NUM_SLOTS; i++) s_celCache[i] = NULL;
    s_celCacheInit = TRUE;
}

#endif /* D2ARCH_COLLECTIONS_C */
