/* Object traps — traps that put real world objects around the player.
 *
 * Every other trap we ship changes a stat or a state. These change the room:
 * a wall of barrels between you and the door, a floor of urns to wade through,
 * a chest that is not a chest. The delivery path is the one the Treasure Cow's
 * hoard already proved works, generalised here so both features call the same
 * code rather than keeping two copies of an RVA and a prolog guard.
 *
 * What is NOT proven, and is deliberately not claimed anywhere below: whether
 * a runtime-spawned object stamps room collision the way a DRLG-placed one
 * does. If it does not, these read as scenery rather than obstacles. The log
 * line reports how many objects actually appeared so the difference is visible
 * from a session log instead of having to be guessed at.
 */

static BOOL IsTown(DWORD area);
static int  GetCurrentArea(void);
static void ShowNotify(const char* text);
static DWORD Quests_PackCode(const char* code);

/* D2Game.0x6FCBB6C0, ImageBase 0x6FC30000 -> RVA 0x8B6C0. */
typedef void* (__fastcall *ObjTrap_AllocUnit_t)(int nUnitType, int nClassId,
                                                int nX, int nY,
                                                void* pGame, void* pRoom,
                                                char a7, int nMode, DWORD a9);
#define OBJTRAP_ALLOCUNIT_RVA 0x8B6C0
static const BYTE OBJTRAP_ALLOCUNIT_PROLOG[] = {
    0x83, 0xEC, 0x10, 0x53, 0x55, 0x8B, 0xD9, 0x56
};
#define OBJTRAP_UNIT_OBJECT 2

/* objects.txt ids, read out of the shipped file rather than remembered.
 * Barrel 11 is the OperateFn 7 row — the one that explodes. Barrel 7 is the
 * ordinary one. Exploding chest 454 shares OperateFn 30 with the Act 3 trap. */
#define OBJ_BARREL_EXPLODING  11
#define OBJ_BARREL_PLAIN       7
#define OBJ_CHEST_EXPLODING  454
static const int OBJ_URNS[]   = { 4, 9, 52, 94 };
static const int OBJ_CHESTS[] = { 5, 6, 87, 88 };

/* A 1x1 item with no use whatsoever. Picked over the Player Ear, which is a
 * better joke but carries spawnable=0 in misc.txt and so may never be made. */
#define OBJTRAP_JUNK_CODE "elx"
#define OBJTRAP_JUNK_COUNT 20

/* The queues these drain from are declared in d2arch_quests.c next to the
 * other trap counters, because the gameloop has to be able to fill them and it
 * is included before this file. Queued on the AP thread, drained on the game
 * thread — spawning needs a live player unit and a room. */

/* Resolve once. A prolog mismatch means a D2Game we do not know, and the right
 * answer there is to do nothing loudly rather than call into the middle of an
 * unrelated function. */
static ObjTrap_AllocUnit_t ObjTrap_GetAllocUnit(void) {
    static ObjTrap_AllocUnit_t s_fn = NULL;
    static BOOL s_resolved = FALSE;
    if (s_resolved) return s_fn;
    s_resolved = TRUE;

    if (!hD2Game) {
        Log("OBJTRAP: hD2Game is NULL\n");
        return NULL;
    }
    BYTE* p = (BYTE*)((DWORD)hD2Game + OBJTRAP_ALLOCUNIT_RVA);
    __try {
        for (int i = 0; i < (int)sizeof(OBJTRAP_ALLOCUNIT_PROLOG); i++) {
            if (p[i] != OBJTRAP_ALLOCUNIT_PROLOG[i]) {
                Log("OBJTRAP: prolog MISMATCH at +0x%X byte %d (got %02X, "
                    "want %02X) — object traps disabled on this build\n",
                    OBJTRAP_ALLOCUNIT_RVA, i, p[i], OBJTRAP_ALLOCUNIT_PROLOG[i]);
                return NULL;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("OBJTRAP: EXCEPTION reading prolog at +0x%X\n", OBJTRAP_ALLOCUNIT_RVA);
        return NULL;
    }
    s_fn = (ObjTrap_AllocUnit_t)p;
    Log("OBJTRAP: SUNIT_AllocUnitData resolved at %p (prolog OK)\n", s_fn);
    return s_fn;
}

/* Scatter `count` objects around the player, rings widening as they fill.
 *
 * ids[] is cycled rather than picked at random so a mixed batch is guaranteed
 * to contain every type in it — a random pick over 6 chests can easily produce
 * six of the same one, which for the false-treasure trap means the trap does
 * not happen at all.
 *
 * Returns how many objects the game actually created. A blocked subtile makes
 * the call return NULL rather than fail, so the retry just steps outward.
 */
/* Is this spot free enough to drop a solid object on?
 *
 * The ring spawner used to ask only "does a room contain this point", which is
 * a question about bookkeeping, not about the floor. In a corridor one tile
 * wide — Maggot Lair, the Sewers, Arcane Sanctuary — a chest three tiles away
 * lands IN the corridor and walls the player in. Maegis lost a character to
 * exactly that; it was never bad luck, the placement simply never looked.
 *
 * We do not read the collision grid ourselves. D2Common exports the same check
 * the engine uses when it places things, so we ask it the question it already
 * knows how to answer, with the engine's own "can something spawn here" mask.
 * Resolving an ordinal is the pattern used elsewhere for quest state and
 * waypoints; guessing at the grid's memory layout is how crashes get written.
 *
 * Fails OPEN: if the export cannot be resolved we place as before rather than
 * silently dropping the cow's entire hoard. A missing reward is a bug report;
 * a missing guard is the status quo we already lived with. */
#define COLLIDE_WALL_F     0x0001
#define COLLIDE_ITEM_F     0x0200
#define COLLIDE_OBJECT_F   0x0400
#define COLLIDE_DOOR_F     0x0800
#define COLLIDE_NO_PATH_F  0x1000
#define COLLIDE_PET_F      0x2000
#define COLLIDE_MASK_SPAWN (COLLIDE_WALL_F | COLLIDE_ITEM_F | COLLIDE_OBJECT_F \
                            | COLLIDE_DOOR_F | COLLIDE_NO_PATH_F | COLLIDE_PET_F)

typedef unsigned short (__stdcall *CollisionCheckSizeXY_t)(
    void* pRoom, int nX, int nY, unsigned int nSizeX, unsigned int nSizeY,
    unsigned short nMask);

static CollisionCheckSizeXY_t ObjTrap_GetCollisionCheck(void) {
    static CollisionCheckSizeXY_t s_fn = NULL;
    static BOOL s_tried = FALSE;
    if (!s_tried) {
        s_tried = TRUE;
        HMODULE h = GetModuleHandleA("D2Common.dll");
        if (h) s_fn = (CollisionCheckSizeXY_t)GetProcAddress(h, (LPCSTR)10120);
        Log("OBJTRAP: COLLISION_CheckMaskWithSizeXY (D2Common #10120) %s\n",
            s_fn ? "resolved" : "NOT FOUND - placing without a floor check");
    }
    return s_fn;
}

/* sizeX/sizeY come from objects.txt; a chest is 2x1 or 1x3, and checking only
   its origin tile would still let the other half sit across the corridor. */
static BOOL ObjTrap_SpotIsFree(DWORD pRoom, int x, int y,
                               int sizeX, int sizeY) {
    CollisionCheckSizeXY_t fn = ObjTrap_GetCollisionCheck();
    if (!fn) return TRUE;
    if (sizeX < 1) sizeX = 1;
    if (sizeY < 1) sizeY = 1;
    {
        unsigned short hit = 0;
        __try {
            hit = fn((void*)pRoom, x, y, (unsigned int)sizeX,
                     (unsigned int)sizeY, (unsigned short)COLLIDE_MASK_SPAWN);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return TRUE;   /* fail open, see above */
        }
        return hit == 0;
    }
}

static int ObjTrap_SpawnRing(const int* ids, int idCount, int count,
                             int baseDist, const char* what) {
    ObjTrap_AllocUnit_t fnAlloc = ObjTrap_GetAllocUnit();
    if (!fnAlloc || idCount <= 0 || count <= 0) return 0;

    void* pGame = (void*)g_cachedPGame;
    if (!pGame) { Log("OBJTRAP %s: no pGame\n", what); return 0; }
    void* pPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pPlayer) { Log("OBJTRAP %s: no server player\n", what); return 0; }

    int spawned = 0;
    __try {
        DWORD pPath = *(DWORD*)((DWORD)pPlayer + 0x2C);
        if (!pPath) { Log("OBJTRAP %s: no pPath\n", what); return 0; }
        DWORD pRoom = *(DWORD*)(pPath + 0x1C);
        if (!pRoom) { Log("OBJTRAP %s: no pRoom\n", what); return 0; }
        int px = (int)*(unsigned short*)(pPath + 0x02);
        int py = (int)*(unsigned short*)(pPath + 0x06);

        static const int dx8[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
        static const int dy8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
        int startDir = rand() & 7;

        for (int i = 0; i < count; i++) {
            int oid = ids[i % idCount];
            void* obj = NULL;
            for (int ring = 0; ring < 4 && !obj; ring++) {
                int dir  = (startDir + i * 3 + ring) & 7;
                int dist = baseDist + (i / 8) * 2 + ring;
                int x = px + dx8[dir] * dist;
                int y = py + dy8[dir] * dist;
                if (x < 0 || y < 0) continue;
                /* Room and coordinates are a PAIR: a ring point can cross a
                 * room border, and D2Common ASSERTS on the mismatch instead of
                 * returning NULL (UnitRoom.cpp:44 — Room_ForPoint's whole
                 * reason to exist, defined in d2arch_gameloop.c). No
                 * containing room -> this point is skipped, the ring loop
                 * simply tries the next direction. */
                DWORD ptRoom = Room_ForPoint(x, y);
                if (!ptRoom) continue;
                /* 3.7.3 — and is there actually floor to stand it on, without
                   sealing the way out? Two tiles wide covers the biggest chest
                   we place, so a spot that passes here fits any of them. */
                if (!ObjTrap_SpotIsFree(ptRoom, x, y, 2, 2)) continue;
                __try {
                    obj = fnAlloc(OBJTRAP_UNIT_OBJECT, oid, x, y,
                                  pGame, (void*)ptRoom, 1, 0, 0);
                } __except(EXCEPTION_EXECUTE_HANDLER) { obj = NULL; }
                if (obj) spawned++;
            }
        }
        Log("OBJTRAP %s: %d/%d objects placed around (%d,%d)\n",
            what, spawned, count, px, py);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("OBJTRAP %s: EXCEPTION during spawn\n", what);
    }
    return spawned;
}

/* Fill the backpack with things that do nothing.
 *
 * No cleanup is needed when the pack is full: fnCI with bDroppable=1 puts the
 * overflow on the floor, which is the behaviour the loot-delivery fix already
 * relies on. So the trap self-limits instead of silently eating items. */
static int ObjTrap_FillBackpack(void) {
    void* pGame = (void*)g_cachedPGame;
    if (!pGame || !hD2Game) return 0;
    void* pPlayer = CustomBoss_GetServerPlayer((DWORD)pGame);
    if (!pPlayer) { Log("OBJTRAP junk: no server player\n"); return 0; }

    typedef void* (__fastcall *CreateItem_t)(void*, void*, DWORD, int, BYTE, int);
    CreateItem_t fnCI = (CreateItem_t)((DWORD)hD2Game + 0x65DF0);

    DWORD code = Quests_PackCode(OBJTRAP_JUNK_CODE);
    if (!code) { Log("OBJTRAP junk: bad code %s\n", OBJTRAP_JUNK_CODE); return 0; }

    int made = 0;
    for (int i = 0; i < OBJTRAP_JUNK_COUNT; i++) {
        void* it = NULL;
        __try { it = fnCI(pGame, pPlayer, code, 1, 2, 1); }
        __except(EXCEPTION_EXECUTE_HANDLER) { it = NULL; }
        if (it) made++;
    }
    Log("OBJTRAP junk: %d/%d junk items delivered\n", made, OBJTRAP_JUNK_COUNT);
    return made;
}

/* Drain one queued object trap per tick.
 *
 * One per tick, not one of each: three traps arriving together would otherwise
 * put thirty-odd objects down in a single frame, and the ring placement has no
 * idea the others exist. Spreading them keeps each trap readable as its own
 * event.
 *
 * Town is skipped rather than dropped — the trap stays queued and fires when
 * the player next leaves. A barrel field in the Rogue Encampment is not a
 * trap, it is a bug report. */
/* Is any object of this class ALREADY placed in the player's room chain?
 *
 * The monster trap learned this lesson long ago ("no recent same-area kill —
 * skipping (avoid unloaded-monster crash)") — spawn only what the client has
 * beviseligt loaded HERE. The object traps never got that guard: they hardcode
 * objects.txt ids from all five acts, and an act-foreign object has no cached
 * texture -> D2Glide asserts (glTextureCache.cpp:193, tTempItem — Marco's
 * second crash type this round). DRLG-placed objects in the nearby rooms ARE
 * the proof of what is loaded, so that is what we scan. */
static BOOL ObjTrap_ClassSeenNearby(int classId) {
    __try {
        DWORD pClient = *(DWORD*)(g_cachedPGame + 0x88);
        if (pClient <= 0x10000) return FALSE;
        DWORD pUnit = *(DWORD*)(pClient + 0x174);
        if (pUnit <= 0x10000) return FALSE;
        DWORD pPath = *(DWORD*)(pUnit + 0x2C);
        if (!pPath) return FALSE;
        DWORD anchor = *(DWORD*)(pPath + 0x1C);
        if (!anchor) return FALSE;

        DWORD rooms[21];
        int roomCount = 0;
        rooms[roomCount++] = anchor;
        DWORD* ppNear = *(DWORD**)(anchor + 0x24);
        int nNear = *(int*)(anchor + 0x28);
        if (nNear > 20) nNear = 20;
        if (ppNear) {
            for (int r = 0; r < nNear && roomCount < 21; r++) {
                DWORD nr = ppNear[r];
                if (nr && nr != anchor) rooms[roomCount++] = nr;
            }
        }
        for (int ri = 0; ri < roomCount; ri++) {
            DWORD unit = *(DWORD*)(rooms[ri] + 0x2C);
            int chain = 0;
            while (unit && chain++ < 200) {
                DWORD type  = *(DWORD*)(unit + 0x00);
                DWORD txtId = *(DWORD*)(unit + 0x04);
                if (type == OBJTRAP_UNIT_OBJECT && (int)txtId == classId)
                    return TRUE;
                unit = *(DWORD*)(unit + 0xE8);   /* next-in-room, same link ScanMonsters walks */
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return FALSE;
}

/* Keep only ids proven loaded nearby. Returns how many survived. */
static int ObjTrap_FilterSeen(const int* ids, int n, int* out) {
    int kept = 0;
    for (int i = 0; i < n; i++)
        if (ObjTrap_ClassSeenNearby(ids[i])) out[kept++] = ids[i];
    return kept;
}

static void ObjTraps_Tick(void) {
    if (!g_pendingTrapBarrels && !g_pendingTrapUrns &&
        !g_pendingTrapTreasure && !g_pendingTrapJunk) return;

    /* The backpack trap needs no room and no space, so it is the one that can
     * fire anywhere — including town, where it is at its most annoying. */
    if (g_pendingTrapJunk > 0) {
        int n = ObjTrap_FillBackpack();
        g_pendingTrapJunk--;
        if (n > 0) ShowNotify("Trap: your pack is full of junk!");
        return;
    }

    if (IsTown((DWORD)GetCurrentArea())) return;

    /* Every ring trap runs its candidate list through the seen-nearby filter
     * first: only object classes DRLG already placed around the player are
     * beviseligt loaded, everything else risks the texture-cache assert. An
     * empty result consumes the trap silently (log only) — a trap that cannot
     * fire safely HERE is a dud, not a crash. */
    if (g_pendingTrapBarrels > 0) {
        static const int ids[] = { OBJ_BARREL_EXPLODING, OBJ_BARREL_EXPLODING,
                                   OBJ_BARREL_PLAIN };
        int seen[3];
        int nSeen = ObjTrap_FilterSeen(ids, 3, seen);
        g_pendingTrapBarrels--;
        if (nSeen == 0) { Log("OBJTRAP barrels: no barrel class loaded nearby — trap fizzles\n"); return; }
        int n = ObjTrap_SpawnRing(seen, nSeen, 10, 2, "barrels");
        if (n > 0) ShowNotify("Trap: barrels!");
        return;
    }
    if (g_pendingTrapUrns > 0) {
        int seen[8];
        int nSeen = ObjTrap_FilterSeen(OBJ_URNS,
                                       (int)(sizeof(OBJ_URNS)/sizeof(OBJ_URNS[0])),
                                       seen);
        g_pendingTrapUrns--;
        if (nSeen == 0) { Log("OBJTRAP urns: no urn class loaded nearby — trap fizzles\n"); return; }
        int n = ObjTrap_SpawnRing(seen, nSeen, 12, 2, "urns");
        if (n > 0) ShowNotify("Trap: urns everywhere!");
        return;
    }
    if (g_pendingTrapTreasure > 0) {
        /* Two of the six are the exploding row. Written as literals because a
         * static initialiser cannot read OBJ_CHESTS[]; the values are the same
         * objects.txt ids listed there. Cycling puts the two at fixed places in
         * the ring, which is fine — the player cannot tell the rows apart by
         * looking, and that is the whole trap. */
        static const int ids[] = { 5, OBJ_CHEST_EXPLODING, 6, 87,
                                   OBJ_CHEST_EXPLODING, 88 };
        int seen[8];
        int nSeen = ObjTrap_FilterSeen(ids, 6, seen);
        g_pendingTrapTreasure--;
        if (nSeen == 0) { Log("OBJTRAP false treasure: no chest class loaded nearby — trap fizzles\n"); return; }
        int n = ObjTrap_SpawnRing(seen, nSeen, 6, 3, "false treasure");
        if (n > 0) ShowNotify("Trap: treasure... maybe.");
        return;
    }
}
