/* d2arch_preloads.c — Zone-locking gate preload data tables (1.8.0)
 *
 * Hardcoded data for the semi-random gated zone-locking system.
 * Source of truth: Research/zone_locking/ACT{1..5}_LOAD_{1..4}.md
 *
 * Contents:
 *   - g_actRegions[5]   = region zone-ID lists per act (same across preloads)
 *   - g_preloadGates[5][4][4] = gate-boss assignments per (act, preload, gate)
 *
 * Integration in d2arch.c include chain:
 *   AFTER d2arch_customboss.c (needs CustomBoss globals)
 *   BEFORE d2arch_zones.c     (zones.c calls preload-apply funcs)
 *
 * Per-character preload selection (15 values) comes from slot_data and
 * is stored in d2arch_input.c's g_actPreload[5][3] array, serialized in
 * d2arch_state_<char>.dat.
 */

#define PRELOAD_MAX_ZONES_PER_REGION  16
#define PRELOAD_MAX_GATES_PER_ACT      4
#define PRELOAD_MAX_PRELOADS_PER_ACT   4
#define PRELOAD_LABEL_MAX             48

/* ----------------------------------------------------------------
 * Act region definitions (identical across all preloads for a given act)
 *
 * R1 = always open (sphere 0)
 * R2 = unlocked by Act N Gate 1 Key
 * R3 = unlocked by Act N Gate 2 Key (+ Gate 1 Key implied)
 * R4 = unlocked by Act N Gate 3 Key
 * R5 = unlocked by Act N Gate 4 Key (contains act boss)
 *
 * Act 4 has only 3 regions (R1/R2/R3) since only 2 gates.
 * ---------------------------------------------------------------- */

typedef struct {
    int  num_gates;                                  /* 4 or 2 */
    int  r1[PRELOAD_MAX_ZONES_PER_REGION]; int r1n;
    int  r2[PRELOAD_MAX_ZONES_PER_REGION]; int r2n;
    int  r3[PRELOAD_MAX_ZONES_PER_REGION]; int r3n;
    int  r4[PRELOAD_MAX_ZONES_PER_REGION]; int r4n;
    int  r5[PRELOAD_MAX_ZONES_PER_REGION]; int r5n;
} ActRegionDef;

/* Act 1: 5 regions, 4 gates
 * R1: Town + Blood Moor + Cold Plains + Den of Evil + Cave L1/L2 + Burial Grounds + Crypt + Mausoleum
 * R2: Stony Field + Dark Wood + UG Passage L1/L2 + Tristram (portal)
 * R3: Black Marsh + Tamoe Highland + Hole L1/L2 + Pit L1/L2 + Forgotten Tower + Tower Cellar L1-L5 + Monastery Gate
 * R4: Outer Cloister + Barracks + Jail L1-L3 + Inner Cloister
 * R5: Cathedral + Catacombs L1-L4 (Andariel)
 */
static const ActRegionDef g_actRegions[5] = {
    {
        /* Act 1 */
        4,
        { 1, 2, 3, 8, 9, 13, 17, 18, 19 },               9,   /* R1 */
        { 4, 5, 10, 14, 38 },                            5,   /* R2 */
        { 6, 7, 11, 12, 15, 16, 20, 21, 22, 23, 24, 25, 26 }, 13, /* R3 */
        { 27, 28, 29, 30, 31, 32 },                      6,   /* R4 */
        { 33, 34, 35, 36, 37 },                          5    /* R5 */
    },
    {
        /* Act 2 */
        4,
        { 40, 41, 47, 48, 49, 50 },                      6,   /* R1 */
        { 42, 51, 55, 56, 57, 59, 60 },                  7,   /* R2 */
        { 43, 52, 53, 54, 62, 63, 64 },                  7,   /* R3 */
        { 44, 45, 58, 61, 65, 74 },                      6,   /* R4 */
        { 46, 66, 67, 68, 69, 70, 71, 72, 73 },          9    /* R5 */
    },
    {
        /* Act 3 */
        4,
        { 75, 76, 77, 84, 85 },                          5,   /* R1 */
        { 78, 79, 86, 87, 88, 89, 90, 91 },              8,   /* R2 */
        { 80, 81, 92, 93, 94, 95, 96, 97 },              8,   /* R3 */
        { 82, 83, 98, 99 },                              4,   /* R4 */
        { 100, 101, 102 },                               3    /* R5 */
    },
    {
        /* Act 4 — only 2 gates, so only R1/R2/R3 used */
        2,
        { 103, 104, 105 },                               3,   /* R1 */
        { 106, 107 },                                    2,   /* R2 */
        { 108 },                                         1,   /* R3 (Diablo) */
        { 0 },                                           0,   /* R4 unused */
        { 0 },                                           0    /* R5 unused */
    },
    {
        /* Act 5 */
        4,
        { 109, 110, 111 },                               3,   /* R1 */
        { 112, 113, 114 },                               3,   /* R2 */
        { 115, 116, 117, 118, 119, 120 },                6,   /* R3 */
        { 128, 129 },                                    2,   /* R4 */
        { 130, 131, 132 },                               3    /* R5 */
    }
};

/* Always-open zones (ignored by zone-locking regardless of preload):
 *   39    Moo Moo Farm (Cow Level)
 *   121-124  Nihlathak's Temple branch
 *   125-127  Pandemonium Event zones (Hell only)
 */
static const int g_alwaysOpenZones[] = {
    39, 121, 122, 123, 124, 125, 126, 127
};
static const int g_alwaysOpenCount = 8;

/* ----------------------------------------------------------------
 * Gate boss spawn definitions per (act, preload_id, gate_index)
 *
 * Format: { spawn_zone_id, base_SU_hcIdx, min_dist_tiles, label }
 * spawn_zone_id == 0 means "gate not used" (e.g. Act 4 only has 2 gates).
 *
 * Data sourced directly from Research/zone_locking/ACT{1..5}_LOAD_{1..4}.md
 * ---------------------------------------------------------------- */

typedef struct {
    int         spawn_zone;
    int         base_su;
    int         min_dist;
    const char* label;
} GateSpawnDef;

static const GateSpawnDef g_preloadGates[5][PRELOAD_MAX_PRELOADS_PER_ACT][PRELOAD_MAX_GATES_PER_ACT] = {
    /* ============================================================
     * ACT 1 — 4 preloads, 4 gates each
     * ============================================================ */
    {
        /* A1 Load 1 — "Beginner's Trial" (vanilla lore) */
        {
            { 8,  40, 40, "A1P1G1_Corpsefire" },        /* Den of Evil -> Corpsefire */
            { 4,   3, 50, "A1P1G2_Rakanishu" },         /* Stony Field -> Rakanishu */
            { 6,   4, 50, "A1P1G3_Treehead" },          /* Black Marsh -> Treehead WoodFist */
            { 28, 20, 30, "A1P1G4_TheSmith" },          /* Barracks -> The Smith */
        },
        /* A1 Load 2 — "Cave Dwellers" */
        {
            { 13,  0, 30, "A1P2G1_Bishibosh" },         /* Cave L2 -> Bishibosh */
            { 14,  1, 30, "A1P2G2_Bonebreak" },         /* UG Passage L2 -> Bonebreak */
            { 15,  7, 30, "A1P2G3_Pitspawn" },          /* Hole L2 -> Pitspawn Fouldog */
            { 31,  9, 30, "A1P2G4_Boneash" },           /* Jail L3 -> Boneash */
        },
        /* A1 Load 3 — "Cold & Shadow" */
        {
            { 17,  2, 30, "A1P3G1_Coldcrow" },          /* Burial Grounds -> Coldcrow */
            { 5,   4, 50, "A1P3G2_Treehead" },          /* Dark Wood -> Treehead */
            { 25,  6, 25, "A1P3G3_Countess" },          /* Tower Cellar L5 -> The Countess */
            { 27,  1, 40, "A1P3G4_Bonebreak" },         /* Outer Cloister -> Bonebreak */
        },
        /* A1 Load 4 — "Wild Hunt" */
        {
            { 19,  0, 25, "A1P4G1_BishiMausol" },       /* Mausoleum -> Bishibosh */
            { 10,  3, 40, "A1P4G2_RakanishuUG" },       /* UG Passage L1 -> Rakanishu */
            { 12,  7, 35, "A1P4G3_PitspawnPit" },       /* Pit L1 -> Pitspawn */
            { 32,  5, 35, "A1P4G4_Griswold" },          /* Inner Cloister -> Griswold */
        }
    },

    /* ============================================================
     * ACT 2 — 4 preloads, 4 gates each
     * ============================================================ */
    {
        /* A2 Load 1 — "Classic Quest Track" */
        {
            { 49, 10, 30, "A2P1G1_Radament" },          /* Sewers L3 -> Radament */
            { 60, 11, 25, "A2P1G2_Bloodwitch" },        /* Halls of Dead L3 -> Bloodwitch */
            { 64, 15, 30, "A2P1G3_Coldworm" },          /* Maggot Lair L3 -> Coldworm */
            { 61, 12, 25, "A2P1G4_Fangskin" },          /* CV Temple L2 -> Fangskin */
        },
        /* A2 Load 2 — "Desert Seekers" */
        {
            { 41, 11, 60, "A2P2G1_BloodwitchRW" },      /* Rocky Waste -> Bloodwitch */
            { 42, 14, 50, "A2P2G2_Leatherarm" },        /* Dry Hills -> Leatherarm */
            { 43, 16, 50, "A2P2G3_FireEye" },           /* Far Oasis -> Fire Eye */
            { 74, 19, 40, "A2P2G4_AncientKaa" },        /* Arcane Sanctuary -> Ancient Kaa */
        },
        /* A2 Load 3 — "Palace Path" */
        {
            { 47, 14, 35, "A2P3G1_LeatherarmSw" },      /* Sewers L1 -> Leatherarm */
            { 51, 13, 35, "A2P3G2_BeetleHarem" },       /* Harem L2 -> Beetleburst */
            { 54, 17, 30, "A2P3G3_DarkElderPC" },       /* Palace Cellar L3 -> Dark Elder */
            { 58, 12, 30, "A2P3G4_FangskinL1" },        /* CV Temple L1 -> Fangskin (lesser) */
        },
        /* A2 Load 4 — "Tomb Walker" */
        {
            { 48, 13, 35, "A2P4G1_BeetleSewers" },      /* Sewers L2 -> Beetleburst */
            { 55, 17, 30, "A2P4G2_DarkElderST" },       /* Stony Tomb L1 -> Dark Elder */
            { 63, 15, 35, "A2P4G3_ColdwormL2" },        /* Maggot L2 -> Coldworm (lesser) */
            { 65, 16, 35, "A2P4G4_FireEyeTun" },        /* Ancient Tunnels -> Fire Eye */
        }
    },

    /* ============================================================
     * ACT 3 — 4 preloads, 4 gates each
     * ============================================================ */
    {
        /* A3 Load 1 — "Jungle Classic" */
        {
            { 85, 21, 30, "A3P1G1_WebMage" },           /* Spider Cavern -> Web Mage */
            { 91, 22, 30, "A3P1G2_Endugu" },            /* Flayer Dungeon L3 -> Endugu */
            { 80, 25, 60, "A3P1G3_Icehawk" },           /* Kurast Bazaar -> Icehawk Riftwing */
            { 83, 24, 50, "A3P1G4_Sarina" },            /* Travincal -> Sarina */
        },
        /* A3 Load 2 — "Outdoor Hunter" */
        {
            { 76, 23, 60, "A3P2G1_StormtreeSF" },       /* Spider Forest -> Stormtree */
            { 79, 32, 50, "A3P2G2_WingedLK" },          /* Lower Kurast -> Winged Death */
            { 81, 33, 50, "A3P2G3_TormentorUK" },       /* Upper Kurast -> The Tormentor */
            { 82, 35, 50, "A3P2G4_Riftwraith" },        /* Kurast Causeway -> Riftwraith */
        },
        /* A3 Load 3 — "Temple Explorer" */
        {
            { 77, 19, 60, "A3P3G1_KaaMarsh" },          /* Great Marsh -> Ancient Kaa */
            { 90, 34, 25, "A3P3G2_TaintSwamp" },        /* Swampy Pit L3 -> Taintbreeder */
            { 95, 33, 25, "A3P3G3_TormentFane" },       /* Disused Fane -> The Tormentor */
            { 99, 25, 25, "A3P3G4_IcehawkRel" },        /* Disused Reliquary -> Icehawk */
        },
        /* A3 Load 4 — "Jungle Ruin" */
        {
            { 84, 35, 25, "A3P4G1_RiftCave" },          /* Spider Cave -> Riftwraith */
            { 88, 34, 35, "A3P4G2_TaintFD1" },          /* Flayer Dungeon L1 -> Taintbreeder */
            { 94, 23, 25, "A3P4G3_StormTemple" },       /* Ruined Temple -> Stormtree */
            { 98, 32, 25, "A3P4G4_WingedFane" },        /* Ruined Fane -> Winged Death */
        }
    },

    /* ============================================================
     * ACT 4 — 3 preloads, 2 gates each (last 1-2 gate slots unused)
     * ============================================================ */
    {
        /* A4 Load 1 — "Classic Descent" */
        {
            { 105, 32, 50, "A4P1G1_WingedPlains" },     /* Plains of Despair -> Winged Death */
            { 107, 33, 35, "A4P1G2_TormentRiver" },     /* River of Flame -> Tormentor */
            { 0, 0, 0, "" },                            /* unused */
            { 0, 0, 0, "" }
        },
        /* A4 Load 2 — "Outer Hunt" */
        {
            { 104, 19, 50, "A4P2G1_KaaSteppes" },       /* Outer Steppes -> Ancient Kaa */
            { 106, 24, 50, "A4P2G2_SarinaCity" },       /* City of Damned -> Sarina */
            { 0, 0, 0, "" },
            { 0, 0, 0, "" }
        },
        /* A4 Load 3 — "Mixed Path" */
        {
            { 105, 35, 50, "A4P3G1_RiftPlains" },       /* Plains of Despair -> Riftwraith */
            { 107, 34, 35, "A4P3G2_TaintRiver" },       /* River of Flame -> Taintbreeder */
            { 0, 0, 0, "" },
            { 0, 0, 0, "" }
        },
        /* A4 Load 4 — UNUSED (Act 4 only has 3 preloads) */
        {
            { 0, 0, 0, "" },
            { 0, 0, 0, "" },
            { 0, 0, 0, "" },
            { 0, 0, 0, "" }
        }
    },

    /* ============================================================
     * ACT 5 — 4 preloads, 4 gates each
     * ============================================================ */
    {
        /* A5 Load 1 — "Barbarian Siege" */
        {
            { 111, 42, 60, "A5P1G1_ShenkOvers" },       /* Frigid Highlands -> Shenk */
            { 113, 53, 35, "A5P1G2_Snapchip" },         /* Cryst Cavern L1 -> Snapchip */
            { 118, 59, 35, "A5P1G3_Frozenstein" },      /* Glacial Caves L1 -> Frozenstein */
            { 129, 46, 30, "A5P1G4_AxeDweller" },       /* Worldstone Keep L2 -> Axe Dweller */
        },
        /* A5 Load 2 — "Cold Depths" */
        {
            { 110, 48, 50, "A5P2G1_DacFarren" },        /* Bloody Foothills -> Dac Farren */
            { 114, 50, 25, "A5P2G2_EyebackPity" },      /* Cellar of Pity -> Eyeback */
            { 116, 56, 25, "A5P2G3_SharpToothEch" },    /* Echo Chamber -> Sharp Tooth Sayer */
            { 128, 47, 30, "A5P2G4_BonesawWK1" },       /* Worldstone L1 -> Bonesaw Breaker */
        },
        /* A5 Load 3 — "Demon Plateau" */
        {
            { 111, 49, 60, "A5P3G1_MegaflowFH" },       /* Frigid Highlands -> Megaflow */
            { 112, 51, 60, "A5P3G2_ThreashAP" },        /* Arreat Plateau -> Threash Socket */
            { 117, 55, 60, "A5P3G3_VinvearTW" },        /* Tundra Wastelands -> Vinvear Molech */
            { 129, 58, 30, "A5P3G4_BlazeWK2" },         /* Worldstone L2 -> Blaze Ripper */
        },
        /* A5 Load 4 — "Winter's Hunt" */
        {
            { 110, 57, 50, "A5P4G1_MagmaBF" },          /* Bloody Foothills -> Magma Torquer */
            { 113, 54, 35, "A5P4G2_AnodizedCC" },       /* Cryst Cavern L1 -> Anodized Elite */
            { 119, 52, 25, "A5P4G3_PindleGC2" },        /* Glacial Caves L2 -> Pindleskin */
            { 128, 46, 30, "A5P4G4_AxeWK1" },           /* Worldstone L1 -> Axe Dweller */
        }
    }
};

/* Number of preloads per act (Act 4 has 3, others have 4) */
static const int g_actPreloadCount[5] = { 4, 4, 4, 3, 4 };

/* ----------------------------------------------------------------
 * Preload API
 * ---------------------------------------------------------------- */

/* Get gate spawn definition for (act, preload_id, gate_index).
 * Returns NULL if any index is out of range or gate is unused. */
static const GateSpawnDef* Preload_GetGate(int act, int preload_id, int gate_idx) {
    if (act < 1 || act > 5) return NULL;
    if (preload_id < 0 || preload_id >= g_actPreloadCount[act - 1]) return NULL;
    if (gate_idx < 0 || gate_idx >= g_actRegions[act - 1].num_gates) return NULL;
    const GateSpawnDef* g = &g_preloadGates[act - 1][preload_id][gate_idx];
    if (g->spawn_zone == 0) return NULL;
    return g;
}

/* Extract the boss display name from a gate label.
 * Labels are "A{a}P{p}G{g}_BossName" — return everything after the first '_'.
 * Falls back to the full label if no underscore is found.
 * Returns a pointer into the original label (no allocation). */
static const char* Preload_BossNameFromLabel(const char* label) {
    if (!label) return "???";
    const char* p = label;
    while (*p && *p != '_') p++;
    if (*p == '_') return p + 1;
    return label;
}

/* Zone name table indexed by D2 area_id. NULL = unused/unnamed area.
 * Only the areas referenced by the gate-locking system are populated;
 * others are left NULL and resolved as "Area %d". */
static const char* g_zoneNames[136] = {
    /* 0   */ NULL,
    /* 1   */ "Rogue Encampment",
    /* 2   */ "Blood Moor",
    /* 3   */ "Cold Plains",
    /* 4   */ "Stony Field",
    /* 5   */ "Dark Wood",
    /* 6   */ "Black Marsh",
    /* 7   */ "Tamoe Highland",
    /* 8   */ "Den of Evil",
    /* 9   */ "Cave Level 1",
    /* 10  */ "Underground Passage L1",
    /* 11  */ "Hole Level 1",
    /* 12  */ "Pit Level 1",
    /* 13  */ "Cave Level 2",
    /* 14  */ "Underground Passage L2",
    /* 15  */ "Hole Level 2",
    /* 16  */ "Pit Level 2",
    /* 17  */ "Burial Grounds",
    /* 18  */ "Crypt",
    /* 19  */ "Mausoleum",
    /* 20  */ "Forgotten Tower",
    /* 21  */ "Tower Cellar L1",
    /* 22  */ "Tower Cellar L2",
    /* 23  */ "Tower Cellar L3",
    /* 24  */ "Tower Cellar L4",
    /* 25  */ "Tower Cellar L5",
    /* 26  */ "Monastery Gate",
    /* 27  */ "Outer Cloister",
    /* 28  */ "Barracks",
    /* 29  */ "Jail Level 1",
    /* 30  */ "Jail Level 2",
    /* 31  */ "Jail Level 3",
    /* 32  */ "Inner Cloister",
    /* 33  */ "Cathedral",
    /* 34  */ "Catacombs L1",
    /* 35  */ "Catacombs L2",
    /* 36  */ "Catacombs L3",
    /* 37  */ "Catacombs L4",
    /* 38  */ "Tristram",
    /* 39  */ "Moo Moo Farm",
    /* 40  */ "Lut Gholein",
    /* 41  */ "Rocky Waste",
    /* 42  */ "Dry Hills",
    /* 43  */ "Far Oasis",
    /* 44  */ "Lost City",
    /* 45  */ "Valley of Snakes",
    /* 46  */ "Canyon of the Magi",
    /* 47  */ "Sewers L1",
    /* 48  */ "Sewers L2",
    /* 49  */ "Sewers L3",
    /* 50  */ "Harem L1",
    /* 51  */ "Harem L2",
    /* 52  */ "Palace Cellar L1",
    /* 53  */ "Palace Cellar L2",
    /* 54  */ "Palace Cellar L3",
    /* 55  */ "Stony Tomb L1",
    /* 56  */ "Halls of the Dead L1",
    /* 57  */ "Halls of the Dead L2",
    /* 58  */ "Claw Viper Temple L1",
    /* 59  */ "Stony Tomb L2",
    /* 60  */ "Halls of the Dead L3",
    /* 61  */ "Claw Viper Temple L2",
    /* 62  */ "Maggot Lair L1",
    /* 63  */ "Maggot Lair L2",
    /* 64  */ "Maggot Lair L3",
    /* 65  */ "Ancient Tunnels",
    /* 66  */ "Tal Rasha's Tomb 1",
    /* 67  */ "Tal Rasha's Tomb 2",
    /* 68  */ "Tal Rasha's Tomb 3",
    /* 69  */ "Tal Rasha's Tomb 4",
    /* 70  */ "Tal Rasha's Tomb 5",
    /* 71  */ "Tal Rasha's Tomb 6",
    /* 72  */ "Tal Rasha's Tomb 7",
    /* 73  */ "Duriel's Lair",
    /* 74  */ "Arcane Sanctuary",
    /* 75  */ "Kurast Docks",
    /* 76  */ "Spider Forest",
    /* 77  */ "Great Marsh",
    /* 78  */ "Flayer Jungle",
    /* 79  */ "Lower Kurast",
    /* 80  */ "Kurast Bazaar",
    /* 81  */ "Upper Kurast",
    /* 82  */ "Kurast Causeway",
    /* 83  */ "Travincal",
    /* 84  */ "Spider Cave",
    /* 85  */ "Spider Cavern",
    /* 86  */ "Swampy Pit L1",
    /* 87  */ "Swampy Pit L2",
    /* 88  */ "Flayer Dungeon L1",
    /* 89  */ "Flayer Dungeon L2",
    /* 90  */ "Swampy Pit L3",
    /* 91  */ "Flayer Dungeon L3",
    /* 92  */ "Sewers L1 (A3)",
    /* 93  */ "Sewers L2 (A3)",
    /* 94  */ "Ruined Temple",
    /* 95  */ "Disused Fane",
    /* 96  */ "Forgotten Reliquary",
    /* 97  */ "Forgotten Temple",
    /* 98  */ "Ruined Fane",
    /* 99  */ "Disused Reliquary",
    /* 100 */ "Durance of Hate L1",
    /* 101 */ "Durance of Hate L2",
    /* 102 */ "Durance of Hate L3",
    /* 103 */ "Pandemonium Fortress",
    /* 104 */ "Outer Steppes",
    /* 105 */ "Plains of Despair",
    /* 106 */ "City of the Damned",
    /* 107 */ "River of Flame",
    /* 108 */ "Chaos Sanctuary",
    /* 109 */ "Harrogath",
    /* 110 */ "Bloody Foothills",
    /* 111 */ "Frigid Highlands",
    /* 112 */ "Arreat Plateau",
    /* 113 */ "Crystalline Passage",
    /* 114 */ "Frozen River",
    /* 115 */ "Glacial Trail",
    /* 116 */ "Drifter Cavern",
    /* 117 */ "Frozen Tundra",
    /* 118 */ "Ancients' Way",
    /* 119 */ "Icy Cellar",
    /* 120 */ "Arreat Summit",
    /* 121 */ "Nihlathak's Temple",
    /* 122 */ "Halls of Anguish",
    /* 123 */ "Halls of Pain",
    /* 124 */ "Halls of Vaught",
    /* 125 */ "Abaddon",
    /* 126 */ "Pit of Acheron",
    /* 127 */ "Infernal Pit",
    /* 128 */ "Worldstone Keep L1",
    /* 129 */ "Worldstone Keep L2",
    /* 130 */ "Worldstone Keep L3",
    /* 131 */ "Throne of Destruction",
    /* 132 */ "Worldstone Chamber",
    /* 133..135 unused */
    NULL, NULL, NULL,
};

/* Get the human-readable name for a D2 area_id.
 * Falls back to "Area %d" via the caller's buffer if unknown. */
static const char* Preload_ZoneName(int areaId) {
    if (areaId <= 0 || areaId >= 136) return NULL;
    return g_zoneNames[areaId];
}

/* Check if a zone is "always open" (portal-only, town, Cow Level, etc.) */
static BOOL Preload_IsAlwaysOpen(int areaId) {
    for (int i = 0; i < g_alwaysOpenCount; i++) {
        if (g_alwaysOpenZones[i] == areaId) return TRUE;
    }
    /* Towns are also always open */
    if (areaId == 1 || areaId == 40 || areaId == 75 ||
        areaId == 103 || areaId == 109) return TRUE;
    return FALSE;
}

/* Look up which region (1..5) a zone belongs to, within a given act.
 * Returns 0 if zone not in any region of that act. */
static int Preload_GetZoneRegion(int act, int areaId) {
    if (act < 1 || act > 5) return 0;
    const ActRegionDef* ar = &g_actRegions[act - 1];
    for (int i = 0; i < ar->r1n; i++) if (ar->r1[i] == areaId) return 1;
    for (int i = 0; i < ar->r2n; i++) if (ar->r2[i] == areaId) return 2;
    for (int i = 0; i < ar->r3n; i++) if (ar->r3[i] == areaId) return 3;
    for (int i = 0; i < ar->r4n; i++) if (ar->r4[i] == areaId) return 4;
    for (int i = 0; i < ar->r5n; i++) if (ar->r5[i] == areaId) return 5;
    return 0;
}
