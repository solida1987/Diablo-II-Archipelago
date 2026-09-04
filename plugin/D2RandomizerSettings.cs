using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;

namespace LauncherV2.Plugins.DiabloII;

//
// The Diablo II randomization options the standalone game reads from
// d2arch.ini [settings].
// apworld exposes — when the user launches standalone (no AP), the launcher
// writes the chosen values here and the mod's LoadAPSettings reads them
// straight back (d2arch_ap.c, the if (!f) "AP disconnected — using
// d2arch.ini [settings] as source" branch).

// Defaults mirror the mod's GetPrivateProfileIntA fallbacks exactly, so an
// absent ini and a freshly-defaulted dialog produce identical worlds.
//
public sealed class D2RandomizerSettings
{
    // --- Seed ---
    // Master seed. 0 = the mod derives a fresh seed per character
    // (GetTickCount + name); non-zero = reproducible world (read as
    // [settings] ShuffleSeed by LoadAPSettings, used as g_standaloneSeed).
    public long Seed { get; set; } = 0;

    // --- Goal ---
    // 0 = Full Normal, 1 = Full Nightmare, 2 = Full Hell, 3 = Collection.
    public int Goal { get; set; } = 2;

    // --- Game mode (two independent toggles, 1.8.0 model) ---
    public bool SkillHunting { get; set; } = true;
    public bool ZoneLocking  { get; set; } = false;

    // --- Stash isolation (2.x one-chest redesign) ---
    // ON (default) = the chest is ISOLATED to this seed: only characters in the
    // same AP seed share it.
    // across all your AP + standalone games.
    // StashIsolated; the seed key comes from SeedKey (AP) / ShuffleSeed (standalone).
    public bool StashIsolated { get; set; } = true;

    // 2.x — Monster Revive Trap.
    // "trapped"; killing one spawns 8 hostile copies of it at the death spot ~1s
    // later. The mod reads [settings] MonsterTrap (standalone) / monster_trap
    // (AP slot_data). OFF by default.
    public bool MonsterTrap { get; set; } = false;

    // --- Quest / progression check categories ---
    public bool QuestHunting         { get; set; } = true;
    public bool QuestKillZones       { get; set; } = true;
    public bool QuestExploration     { get; set; } = true;
    public bool QuestWaypoints       { get; set; } = true;
    public bool QuestLevelMilestones { get; set; } = true;

    // --- Skill pool ---
    // Vanilla class skills — also the slider's ceiling.
    // wave is NOT part of this number: ticking ExperimentalSpells adds the
    // whole new pack ON TOP of whatever the slider says, so the slider keeps
    // meaning exactly what it always meant ("how many of the normal skills").
    public const int SkillPoolVanilla = 210;
    // Room for vanilla + every experimental skill the mod ships.
    public const int SkillPoolMax     = 260;
    public int SkillPoolSize  { get; set; } = SkillPoolVanilla;
    // Off by default, so an untouched profile keeps the full 210 it always had.
    public bool SkillPoolLimited { get; set; } = false;
    public int StartingSkills { get; set; } = 6;      // 0..20

    // Experimental skill wave (the "Test N" spells, mod ids 357+).
    // default: with it off the pool is exactly what it always was.
    public bool ExperimentalSpells { get; set; } = false;

    // The pool size actually written to the mod.
    
    // The slider counts NORMAL skills only; morph and passive pools scale from it.
    public int EffectiveSkillPoolSize => SkillPoolLimited ? SkillPoolSize : SkillPoolVanilla;

    // --- Filler distribution (relative weights) ---
    public bool TrapsEnabled { get; set; } = true;

    // High runes, perfect gems, grand charms and the big point items are held
    // back for the later acts and difficulties; their low-tier counterparts
    // come early. Off restores the flat roll, where a Vex is exactly as likely
    // in Blood Moor as in Hell Act 5.
    public bool TieredFillerDepth { get; set; } = true;
    public int  TrapPct      { get; set; } = 15;
    public int  GoldPct      { get; set; } = 30;
    public int  StatPtsPct   { get; set; } = 15;
    public int  SkillPtsPct  { get; set; } = 15;
    public int  ResetPtsPct  { get; set; } = 25;
    public int  LootPct      { get; set; } = 18;

    // --- Shuffles ---
    public bool MonsterShuffle     { get; set; } = false;
    // Super-unique (named mini-boss) shuffle — done via SuperUniques.txt swap
    // in the launcher (always-killable, no cinematic boss).
    public bool SuperUniqueShuffle { get; set; } = false;
    // Act-boss shuffle — Andariel/Duriel/Mephisto/Diablo/Baal.
    // (cosmetic only; each boss keeps its own behaviour slot so quests/cinematics
    // still resolve correctly).
    public bool ActBossShuffle     { get; set; } = false;
    public bool ShopShuffle        { get; set; } = false;
    public bool EntranceShuffle    { get; set; } = false;

    // --- Requirements ---
    public bool SkillLevelReqs { get; set; } = true;
    // hard cap on points per skill.
    // uncapped mod behaviour.
    // run can be "no level gate but still capped at 20" or vice versa.
    public int  SkillMaxLevel  { get; set; } = 20;
    // ON (default) = vanilla level requirements kept.
    // LEVEL requirement (base + magic/rare affix + set/unique), so any character
    // can equip anything regardless of level.
    // "ItemLevelReqs" so level and stat reqs can be toggled independently.
    public bool ItemLevelReqs  { get; set; } = true;
    // ON (default) = vanilla strength/dexterity requirements kept.
    // every item's STR/DEX requirement.
    public bool ItemStatsReqs  { get; set; } = true;

    // --- XP ---
    public int XPMultiplier { get; set; } = 0;   // 0..100: 0 = standard XP, each +1 = +100%

    // --- Reward amounts (each gold/XP reward rolls a random value in [min,max]) ---

    // --- Class filter (which classes the skill pool may draw from) ---
    public bool ClassFilter    { get; set; } = false;
    public bool ClsAmazon      { get; set; } = true;
    public bool ClsSorceress   { get; set; } = true;
    public bool ClsNecromancer { get; set; } = true;
    public bool ClsPaladin     { get; set; } = true;
    public bool ClsBarbarian   { get; set; } = true;
    public bool ClsDruid       { get; set; } = true;
    public bool ClsAssassin    { get; set; } = true;

    // --- Bonus check categories (1.9.0) ---
    public bool CheckShrines        { get; set; } = false;
    public bool CheckUrns           { get; set; } = false;
    public bool CheckBarrels        { get; set; } = false;
    public bool CheckChests         { get; set; } = false;
    public bool CheckSetPickups     { get; set; } = false;
    public bool CheckGoldMilestones { get; set; } = false;

    // --- Extra check categories (1.9.2) ---
    public bool CheckCowLevel        { get; set; } = false;
    public bool CheckMercMilestones  { get; set; } = false;
    public bool CheckHellforgeRunes  { get; set; } = false;
    public bool CheckNpcDialogue     { get; set; } = false;
    public bool CheckRunewordCrafting{ get; set; } = false;
    public bool CheckCubeRecipes     { get; set; } = false;

    // --- Display ---
    // The mod hides tier colours by default (show_tier_colors absent → 0).
    public bool ShowTierColors { get; set; } = false;

    // --- Collection targets (only matter when Goal = Collection) ---
    // The mod reads these as bitmasks in d2arch_ap.c (the `g_apGoal == 3`
    // branch) and clears any target whose bit is 0.
    // in the SAME order the apworld lists them (apworld field names are aligned
    // with the DLL parser, so this order is authoritative).
    // everything (the standalone default).
    public static readonly string[] CollectionSetNames =
    {
        "Civerb's Vestments", "Hsarus' Defense", "Cleglaw's Brace", "Iratha's Finery",
        "Isenhart's Armory", "Vidala's Rig", "Milabrega's Regalia", "Cathan's Traps",
        "Tancred's Battlegear", "Sigon's Complete Steel", "Infernal Tools", "Berserker's Garb",
        "Death's Disguise", "Angelical Raiment", "Arctic Gear", "Arcanna's Tricks",
        "Natalya's Odium [Assassin]", "Aldur's Watchtower [Druid]", "Immortal King [Barbarian]",
        "Tal Rasha's Wrappings [Sorceress]", "Griswold's Legacy [Paladin]",
        "Trang-Oul's Avatar [Necromancer]", "M'avina's Battle Hymn [Amazon]", "The Disciple",
        "Heaven's Brethren", "Orphan's Call", "Hwanin's Majesty", "Sazabi's Grand Tribute",
        "Bul-Kathos' Children", "Cow King's Leathers", "Naj's Ancient Set", "McAuley's Folly",
    };
    public static readonly string[] CollectionRuneNames =
    {
        "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul",
        "Amn", "Sol", "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem",
        "Pul", "Um", "Mal", "Ist", "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber",
        "Jah", "Cham", "Zod",
    };
    public static readonly string[] CollectionSpecialNames =
    {
        "Key of Terror", "Key of Hate", "Key of Destruction", "Mephisto's Brain",
        "Diablo's Horn", "Baal's Eye", "Twisted Essence of Suffering",
        "Charged Essence of Hatred", "Burning Essence of Terror", "Hellfire Torch",
    };

    public bool[] CollectSets     { get; set; } = NewAllOn(32);
    public bool[] CollectRunes    { get; set; } = NewAllOn(33);
    public bool[] CollectSpecials { get; set; } = NewAllOn(10);
    public bool   CollectGems     { get; set; } = true;

    // --- Custom goal (only used when Goal = Custom) ---
    // Build-your-own win condition: the game completes when EVERY ticked
    // target is achieved (+ the optional gold target).
    // apworld + the DLL's CGT_LookupName table exactly, so the mod parses the
    // CSV the same way it parses an AP slot's custom_goal_targets_csv.
    public sealed record CustomGoalDef(string Group, string Token, string Display);

    public static readonly CustomGoalDef[] CustomGoalCatalog =
    {
        // Subsystems (10)
        new("Subsystems", "subsystem_skill_hunting",      "Unlock every skill in the pool"),
        new("Subsystems", "subsystem_collection",         "Complete the Collection"),
        new("Subsystems", "subsystem_hunt_quests",        "All hunting quests"),
        new("Subsystems", "subsystem_kill_zone_quests",   "All kill-zone quests"),
        new("Subsystems", "subsystem_exploration_quests", "All exploration quests"),
        new("Subsystems", "subsystem_waypoints",          "All waypoints"),
        new("Subsystems", "subsystem_level_milestones",   "All level milestones"),
        new("Subsystems", "subsystem_story_normal",       "All story quests (Normal)"),
        new("Subsystems", "subsystem_story_nightmare",    "All story quests (Nightmare)"),
        new("Subsystems", "subsystem_story_hell",         "All story quests (Hell)"),
        // Act bosses × difficulty (15)
        new("Act bosses", "kill_andariel_normal",    "Andariel (Normal)"),
        new("Act bosses", "kill_andariel_nightmare", "Andariel (Nightmare)"),
        new("Act bosses", "kill_andariel_hell",      "Andariel (Hell)"),
        new("Act bosses", "kill_duriel_normal",      "Duriel (Normal)"),
        new("Act bosses", "kill_duriel_nightmare",   "Duriel (Nightmare)"),
        new("Act bosses", "kill_duriel_hell",        "Duriel (Hell)"),
        new("Act bosses", "kill_mephisto_normal",    "Mephisto (Normal)"),
        new("Act bosses", "kill_mephisto_nightmare", "Mephisto (Nightmare)"),
        new("Act bosses", "kill_mephisto_hell",      "Mephisto (Hell)"),
        new("Act bosses", "kill_diablo_normal",      "Diablo (Normal)"),
        new("Act bosses", "kill_diablo_nightmare",   "Diablo (Nightmare)"),
        new("Act bosses", "kill_diablo_hell",        "Diablo (Hell)"),
        new("Act bosses", "kill_baal_normal",        "Baal (Normal)"),
        new("Act bosses", "kill_baal_nightmare",     "Baal (Nightmare)"),
        new("Act bosses", "kill_baal_hell",          "Baal (Hell)"),
        // Cow King + Pandemonium ubers (7)
        new("Cow King & Ubers", "kill_cow_king_normal",    "Cow King (Normal)"),
        new("Cow King & Ubers", "kill_cow_king_nightmare", "Cow King (Nightmare)"),
        new("Cow King & Ubers", "kill_cow_king_hell",      "Cow King (Hell)"),
        new("Cow King & Ubers", "kill_uber_mephisto",      "Uber Mephisto"),
        new("Cow King & Ubers", "kill_uber_diablo",        "Uber Diablo"),
        new("Cow King & Ubers", "kill_uber_baal",          "Uber Baal"),
        new("Cow King & Ubers", "hellfire_torch_complete", "Pandemonium full run (Hellfire Torch)"),
        // Famous super-uniques (10)
        new("Super-uniques", "kill_bishibosh",    "Bishibosh"),
        new("Super-uniques", "kill_corpsefire",   "Corpsefire"),
        new("Super-uniques", "kill_rakanishu",    "Rakanishu"),
        new("Super-uniques", "kill_griswold",     "Griswold"),
        new("Super-uniques", "kill_pindleskin",   "Pindleskin"),
        new("Super-uniques", "kill_nihlathak_su", "Nihlathak (boss)"),
        new("Super-uniques", "kill_summoner",     "The Summoner"),
        new("Super-uniques", "kill_radament",     "Radament"),
        new("Super-uniques", "kill_izual",        "Izual"),
        new("Super-uniques", "kill_council",      "Council member"),
        // Bulk object/check targets (12)
        new("Bulk checks", "all_shrines",           "All shrines"),
        new("Bulk checks", "all_urns",              "All urns"),
        new("Bulk checks", "all_barrels",           "All barrels"),
        new("Bulk checks", "all_chests",            "All chests"),
        new("Bulk checks", "all_set_pickups",       "All set pickups"),
        new("Bulk checks", "all_gold_milestones",   "All gold milestones"),
        new("Bulk checks", "all_cow_level_checks",  "All cow-level checks"),
        new("Bulk checks", "all_merc_milestones",   "All merc milestones"),
        new("Bulk checks", "all_hellforge_runes",   "All Hellforge & high runes"),
        new("Bulk checks", "all_npc_dialogue",      "All NPC dialogue"),
        new("Bulk checks", "all_runeword_crafting", "All runeword crafting"),
        new("Bulk checks", "all_cube_recipes",      "All cube recipes"),
    };

    // Enabled custom-goal target tokens (subset of CustomGoalCatalog tokens).
    public HashSet<string> CustomGoalTargets { get; set; } = new();
    // Optional lifetime-gold requirement on top of the targets.
    public long CustomGoalGold { get; set; } = 0;

    // lifetime gold the character must reach for a Collection goal.
    // Archipelago has always had this (collection_gold_target); standalone
    // had no counterpart, so Goal = Collection could not be set up the same
    // way in the two modes.
    public int CollectionGoldTarget { get; set; } = 1000000;

    private static bool[] NewAllOn(int n)
    {
        var a = new bool[n];
        for (int i = 0; i < n; i++) a[i] = true;
        return a;
    }

    private static string B(bool v) => v ? "1" : "0";

    // Pack a bool[] into an integer bitmask (bit i set when arr[i] true).
    private static ulong Pack(bool[] arr)
    {
        ulong m = 0;
        for (int i = 0; i < arr.Length && i < 64; i++)
            if (arr[i]) m |= 1UL << i;
        return m;
    }

    private static void Unpack(ulong mask, bool[] arr)
    {
        for (int i = 0; i < arr.Length && i < 64; i++)
            arr[i] = (mask & (1UL << i)) != 0;
    }

    // Every [settings] key the mod reads in standalone, in a stable order.
    // WriteStandaloneSettings writes exactly these; nothing else touches the
    // [settings] section, so unknown legacy keys are left untouched.
    public IEnumerable<KeyValuePair<string, string>> ToIniPairs()
    {
        yield return new("ShuffleSeed",          Seed.ToString(CultureInfo.InvariantCulture));
        yield return new("Goal",                 Goal.ToString(CultureInfo.InvariantCulture));

        yield return new("SkillHunting",         B(SkillHunting));
        yield return new("ZoneLocking",          B(ZoneLocking));

        yield return new("StashIsolated",        B(StashIsolated));
        yield return new("MonsterTrap",          B(MonsterTrap));

        yield return new("QuestHunting",         B(QuestHunting));
        yield return new("QuestKillZones",       B(QuestKillZones));
        yield return new("QuestExploration",     B(QuestExploration));
        yield return new("QuestWaypoints",       B(QuestWaypoints));
        yield return new("QuestLevelMilestones", B(QuestLevelMilestones));

        yield return new("SkillPoolSize",        EffectiveSkillPoolSize.ToString(CultureInfo.InvariantCulture));
        yield return new("SkillPoolLimited",     B(SkillPoolLimited));
        yield return new("StartingSkills",       StartingSkills.ToString(CultureInfo.InvariantCulture));

        yield return new("TrapsEnabled",         B(TrapsEnabled));
        yield return new("TieredFillerDepth",    B(TieredFillerDepth));
        yield return new("TrapPct",              TrapPct.ToString(CultureInfo.InvariantCulture));
        yield return new("GoldPct",              GoldPct.ToString(CultureInfo.InvariantCulture));
        yield return new("StatPtsPct",           StatPtsPct.ToString(CultureInfo.InvariantCulture));
        yield return new("SkillPtsPct",          SkillPtsPct.ToString(CultureInfo.InvariantCulture));
        yield return new("ResetPtsPct",          ResetPtsPct.ToString(CultureInfo.InvariantCulture));
        yield return new("LootPct",              LootPct.ToString(CultureInfo.InvariantCulture));

        yield return new("MonsterShuffle",       B(MonsterShuffle));
        yield return new("SuperUniqueShuffle",   B(SuperUniqueShuffle));
        // TEMPORARILY DISABLED — see BetaHub #13. Emitted as false whatever the
        // saved settings say, so an older settings file cannot switch it back
        // on behind the player's back. Restore B(ActBossShuffle) to re-enable.
        yield return new("ActBossShuffle",       B(false));
        // Back-compat: the DLL reads ActBossShuffle (falling back to BossShuffle).
        yield return new("BossShuffle",          B(false));
        yield return new("ShopShuffle",          B(ShopShuffle));
        yield return new("EntranceShuffle",      B(EntranceShuffle));

        // the launcher now applies monster/boss/shop shuffle + skill/item
        // requirements via the seed-bound data files (D2DataFiles).
        // the DLL to skip its own runtime monster/boss shuffle so they don't double.
        yield return new("LauncherDataShuffle",  "1");

        yield return new("SkillLevelReqs",       B(SkillLevelReqs));
        yield return new("SkillMaxLevel",        SkillMaxLevel.ToString(CultureInfo.InvariantCulture));
        yield return new("ExperimentalSpells",   B(ExperimentalSpells));
        yield return new("ItemLevelReqs",        B(ItemLevelReqs));
        yield return new("ItemStatsReqs",        B(ItemStatsReqs));

        yield return new("XPMultiplier",         XPMultiplier.ToString(CultureInfo.InvariantCulture));

        yield return new("ClassFilter",          B(ClassFilter));
        yield return new("ClsAmazon",            B(ClsAmazon));
        yield return new("ClsSorceress",         B(ClsSorceress));
        yield return new("ClsNecromancer",       B(ClsNecromancer));
        yield return new("ClsPaladin",           B(ClsPaladin));
        yield return new("ClsBarbarian",         B(ClsBarbarian));
        yield return new("ClsDruid",             B(ClsDruid));
        yield return new("ClsAssassin",          B(ClsAssassin));

        yield return new("CheckShrines",         B(CheckShrines));
        yield return new("CheckUrns",            B(CheckUrns));
        yield return new("CheckBarrels",         B(CheckBarrels));
        yield return new("CheckChests",          B(CheckChests));
        yield return new("CheckSetPickups",      B(CheckSetPickups));
        yield return new("CheckGoldMilestones",  B(CheckGoldMilestones));

        yield return new("CheckCowLevel",        B(CheckCowLevel));
        yield return new("CheckMercMilestones",  B(CheckMercMilestones));
        yield return new("CheckHellforgeRunes",  B(CheckHellforgeRunes));
        yield return new("CheckNpcDialogue",     B(CheckNpcDialogue));
        yield return new("CheckRunewordCrafting",B(CheckRunewordCrafting));
        yield return new("CheckCubeRecipes",     B(CheckCubeRecipes));

        yield return new("show_tier_colors",     B(ShowTierColors));

        // Collection targets (Goal=Collection only).
        // chunks the mod reads: sets 32-bit (Lo/Hi), runes 48-bit (Lo/Md/Hi),
        // specials 10-bit (single key), gems a flag.
        // these when Goal=Collection, so they're harmless under other goals.
        ulong sets  = Pack(CollectSets);
        ulong runes = Pack(CollectRunes);
        ulong specs = Pack(CollectSpecials);
        yield return new("CollSetsMaskLo",   ((int)(sets & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollSetsMaskHi",   ((int)((sets >> 16) & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollRunesMaskLo",  ((int)(runes & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollRunesMaskMd",  ((int)((runes >> 16) & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollRunesMaskHi",  ((int)((runes >> 32) & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollSpecialsMask", ((int)(specs & 0xFFFF)).ToString(CultureInfo.InvariantCulture));
        yield return new("CollGoalGems",     B(CollectGems));

        // Custom goal (Goal=Custom only).
        // optional gold target — the mod parses these exactly like an AP slot.
        yield return new("CustomGoalTargets", string.Join(",", CustomGoalTargets));
        yield return new("CustomGoalGold",    CustomGoalGold.ToString(CultureInfo.InvariantCulture));
        yield return new("CollectionGoldTarget", CollectionGoldTarget.ToString(CultureInfo.InvariantCulture));
    }

    // Rebuild a settings object from a [settings] reader.
    // returns the raw string value for a key, or null if absent — absent keys
    // fall back to the same defaults the mod uses, so the dialog always opens
    // on a coherent state even with an empty/partial ini.
    public static D2RandomizerSettings FromIni(Func<string, string?> read)
    {
        var d = new D2RandomizerSettings();

        int I(string key, int def)
        {
            string? raw = read(key);
            return raw != null && int.TryParse(raw.Trim(), NumberStyles.Integer,
                       CultureInfo.InvariantCulture, out int v) ? v : def;
        }
        long L(string key, long def)
        {
            string? raw = read(key);
            return raw != null && long.TryParse(raw.Trim(), NumberStyles.Integer,
                       CultureInfo.InvariantCulture, out long v) ? v : def;
        }
        bool Bl(string key, bool def) => I(key, def ? 1 : 0) != 0;

        d.Seed = L("ShuffleSeed", d.Seed);
        d.Goal = Math.Clamp(I("Goal", d.Goal), 0, 4);   // 4 = Custom

        d.SkillHunting = Bl("SkillHunting", d.SkillHunting);
        d.ZoneLocking  = Bl("ZoneLocking",  d.ZoneLocking);

        d.StashIsolated = Bl("StashIsolated", d.StashIsolated);
        d.MonsterTrap   = Bl("MonsterTrap",   d.MonsterTrap);

        d.QuestHunting         = Bl("QuestHunting",         d.QuestHunting);
        d.QuestKillZones       = Bl("QuestKillZones",       d.QuestKillZones);
        d.QuestExploration     = Bl("QuestExploration",     d.QuestExploration);
        d.QuestWaypoints       = Bl("QuestWaypoints",       d.QuestWaypoints);
        d.QuestLevelMilestones = Bl("QuestLevelMilestones", d.QuestLevelMilestones);

        d.SkillPoolSize  = Math.Clamp(I("SkillPoolSize",  d.SkillPoolSize), 1, SkillPoolVanilla);
        d.SkillPoolLimited = Bl("SkillPoolLimited", d.SkillPoolLimited);
        d.StartingSkills = Math.Clamp(I("StartingSkills", d.StartingSkills), 0, 20);

        d.TrapsEnabled = Bl("TrapsEnabled", d.TrapsEnabled);
        d.TieredFillerDepth = Bl("TieredFillerDepth", d.TieredFillerDepth);
        d.TrapPct      = Math.Clamp(I("TrapPct",     d.TrapPct),     0, 100);
        d.GoldPct      = Math.Clamp(I("GoldPct",     d.GoldPct),     0, 100);
        d.StatPtsPct   = Math.Clamp(I("StatPtsPct",  d.StatPtsPct),  0, 100);
        d.SkillPtsPct  = Math.Clamp(I("SkillPtsPct", d.SkillPtsPct), 0, 100);
        d.ResetPtsPct  = Math.Clamp(I("ResetPtsPct", d.ResetPtsPct), 0, 100);
        d.LootPct      = Math.Clamp(I("LootPct",     d.LootPct),     0, 100);

        d.MonsterShuffle     = Bl("MonsterShuffle", d.MonsterShuffle);
        // SuperUniqueShuffle is the renamed old "BossShuffle"; fall back to it so
        // existing inis keep working.
        d.SuperUniqueShuffle = Bl("SuperUniqueShuffle", Bl("BossShuffle", d.SuperUniqueShuffle));
        d.ActBossShuffle     = Bl("ActBossShuffle", d.ActBossShuffle);
        d.ShopShuffle        = Bl("ShopShuffle",     d.ShopShuffle);
        d.EntranceShuffle    = Bl("EntranceShuffle", d.EntranceShuffle);

        d.SkillLevelReqs = Bl("SkillLevelReqs", d.SkillLevelReqs);
        d.SkillMaxLevel  = Math.Clamp(I("SkillMaxLevel", d.SkillMaxLevel), 1, 99);
        d.ExperimentalSpells = Bl("ExperimentalSpells", d.ExperimentalSpells);
        d.ItemLevelReqs  = Bl("ItemLevelReqs",  d.ItemLevelReqs);
        d.ItemStatsReqs  = Bl("ItemStatsReqs",  d.ItemStatsReqs);

        d.XPMultiplier = Math.Clamp(I("XPMultiplier", d.XPMultiplier), 0, 100);

        d.ClassFilter    = Bl("ClassFilter",    d.ClassFilter);
        d.ClsAmazon      = Bl("ClsAmazon",      d.ClsAmazon);
        d.ClsSorceress   = Bl("ClsSorceress",   d.ClsSorceress);
        d.ClsNecromancer = Bl("ClsNecromancer", d.ClsNecromancer);
        d.ClsPaladin     = Bl("ClsPaladin",     d.ClsPaladin);
        d.ClsBarbarian   = Bl("ClsBarbarian",   d.ClsBarbarian);
        d.ClsDruid       = Bl("ClsDruid",       d.ClsDruid);
        d.ClsAssassin    = Bl("ClsAssassin",    d.ClsAssassin);

        d.CheckShrines        = Bl("CheckShrines",        d.CheckShrines);
        d.CheckUrns           = Bl("CheckUrns",           d.CheckUrns);
        d.CheckBarrels        = Bl("CheckBarrels",        d.CheckBarrels);
        d.CheckChests         = Bl("CheckChests",         d.CheckChests);
        d.CheckSetPickups     = Bl("CheckSetPickups",     d.CheckSetPickups);
        d.CheckGoldMilestones = Bl("CheckGoldMilestones", d.CheckGoldMilestones);

        d.CheckCowLevel         = Bl("CheckCowLevel",         d.CheckCowLevel);
        d.CheckMercMilestones   = Bl("CheckMercMilestones",   d.CheckMercMilestones);
        d.CheckHellforgeRunes   = Bl("CheckHellforgeRunes",   d.CheckHellforgeRunes);
        d.CheckNpcDialogue      = Bl("CheckNpcDialogue",      d.CheckNpcDialogue);
        d.CheckRunewordCrafting = Bl("CheckRunewordCrafting", d.CheckRunewordCrafting);
        d.CheckCubeRecipes      = Bl("CheckCubeRecipes",      d.CheckCubeRecipes);

        d.ShowTierColors = Bl("show_tier_colors", d.ShowTierColors);

        // Collection masks. -1 (or absent) on either half = "no override",
        // which the mod treats as all-on — so only unpack when a real mask was
        // written, otherwise keep the all-on defaults.
        int setsLo = I("CollSetsMaskLo", -1), setsHi = I("CollSetsMaskHi", -1);
        if (setsLo >= 0 && setsHi >= 0)
            Unpack((uint)(setsLo & 0xFFFF) | ((uint)(setsHi & 0xFFFF) << 16), d.CollectSets);
        int rLo = I("CollRunesMaskLo", -1), rMd = I("CollRunesMaskMd", -1), rHi = I("CollRunesMaskHi", -1);
        if (rLo >= 0 && rMd >= 0 && rHi >= 0)
            Unpack((uint)(rLo & 0xFFFF) | ((ulong)(uint)(rMd & 0xFFFF) << 16) | ((ulong)(uint)(rHi & 0xFFFF) << 32), d.CollectRunes);
        int specs = I("CollSpecialsMask", -1);
        if (specs >= 0)
            Unpack((uint)(specs & 0xFFFF), d.CollectSpecials);
        d.CollectGems = Bl("CollGoalGems", d.CollectGems);

        string? cgt = read("CustomGoalTargets");
        d.CustomGoalTargets = string.IsNullOrWhiteSpace(cgt)
            ? new HashSet<string>()
            : new HashSet<string>(cgt.Split(',',
                StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
        d.CustomGoalGold = L("CustomGoalGold", 0);
        d.CollectionGoldTarget = Math.Clamp(I("CollectionGoldTarget", d.CollectionGoldTarget), 0, 100000000);

        return d;
    }

    // Build the data-file-relevant settings from an AP slot_data object, so an AP
    // world randomizes its tables exactly like a standalone seed.
    // the txt patcher consumes are mapped (monster / super-unique / shop shuffle +
    // skill/item requirements); everything else keeps its default.
    // stays in the DLL (it reads boss_shuffle from slot_data itself).
    public static D2RandomizerSettings FromSlotData(JsonElement sd)
    {
        var d = new D2RandomizerSettings();
        if (sd.ValueKind != JsonValueKind.Object) return d;

        bool B(string key, bool def)
        {
            if (!sd.TryGetProperty(key, out var v)) return def;
            return v.ValueKind switch
            {
                JsonValueKind.True   => true,
                JsonValueKind.False  => false,
                JsonValueKind.Number => v.TryGetInt32(out int n) && n != 0,
                JsonValueKind.String => int.TryParse(v.GetString(), out int s) && s != 0,
                _                    => def,
            };
        }

        // SkillMaxLevel arrives as a number; accept a string too so a
        // hand-rolled slot_data can't silently fall back to the default.
        int N(string key, int def)
        {
            if (!sd.TryGetProperty(key, out var v)) return def;
            return v.ValueKind switch
            {
                JsonValueKind.Number => v.TryGetInt32(out int n) ? n : def,
                JsonValueKind.String => int.TryParse(v.GetString(), out int t) ? t : def,
                _                    => def,
            };
        }

        d.MonsterShuffle     = B("monster_shuffle", d.MonsterShuffle);
        // The apworld emits split keys (superunique_shuffle / act_boss_shuffle);
        // fall back to the legacy single boss_shuffle so older worlds still work.
        d.SuperUniqueShuffle = B("superunique_shuffle", B("boss_shuffle", d.SuperUniqueShuffle));
        d.ActBossShuffle     = B("act_boss_shuffle",   B("boss_shuffle", d.ActBossShuffle));
        d.ShopShuffle        = B("shop_shuffle", d.ShopShuffle);
        d.SkillLevelReqs     = B("skill_level_reqs", d.SkillLevelReqs);
        d.SkillPoolSize      = Math.Clamp(N("skill_pool_size", d.SkillPoolSize), 1, SkillPoolVanilla);
        d.SkillMaxLevel      = Math.Clamp(N("skill_max_level", d.SkillMaxLevel), 1, 99);
        d.ExperimentalSpells = B("experimental_spell_pool", d.ExperimentalSpells);
        d.ItemLevelReqs      = B("item_level_reqs", d.ItemLevelReqs);
        d.ItemStatsReqs      = B("item_stats_reqs", d.ItemStatsReqs);
        // 2.x one-chest: per-seed stash isolation, set in the player's YAML.
        d.StashIsolated      = B("stash_isolated", d.StashIsolated);
        // 2.x — Monster Revive Trap (AP reads monster_trap from ap_settings.dat;
        // mirrored here for completeness).
        d.MonsterTrap        = B("monster_trap",   d.MonsterTrap);
        return d;
    }
}
