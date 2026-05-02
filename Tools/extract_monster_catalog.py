"""Extract the SuperUnique + Normal monster catalogs from the research
markdown into C array literals ready to paste into d2arch_drawall.c.

Reads:  Research/CHECK_SOURCES_MONSTER_AUDIT_2026-05-02.md
Writes: Tools/Archipelago/src/_monster_catalog_generated.h

The header file is included by d2arch_drawall.c. Re-run this script if
the research markdown's tables are updated.
"""
import os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "Research", "CHECK_SOURCES_MONSTER_AUDIT_2026-05-02.md")
DST = os.path.join(REPO, "Tools", "Archipelago", "src", "_monster_catalog_generated.h")

with open(SRC, "r", encoding="utf-8") as f:
    text = f.read()

# ============================================================
# 1) SuperUniques: bounded by the "### A. SuperUniques" header and the
#    "### B. Act Bosses" header. Format inside that range:
#    "| <idx> | <name> | <spawn-from> | <zone> | <notes> |"
# ============================================================
su_start = re.search(r"^### A\. SuperUniques", text, flags=re.MULTILINE)
su_end   = re.search(r"^### B\. Act Bosses",  text, flags=re.MULTILINE)
if not su_start or not su_end:
    print("ERROR: SU section bounds not found"); sys.exit(1)
su_text = text[su_start.start():su_end.start()]

su_lines = re.findall(
    r"^\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]*?)\s*\|\s*$",
    su_text, flags=re.MULTILINE,
)
sus = []
seen_su = set()
for idx_str, name, spawn, zone, notes in su_lines:
    idx = int(idx_str)
    if idx > 66: continue          # uber rows crash fnSpawnSuperUnique
    if idx in seen_su: continue    # belt-and-suspenders against duplicates
    seen_su.add(idx)
    if "Expansion header" in name: continue
    note = zone.split("(")[0].strip() or zone.strip()
    sus.append((idx, name.strip(), note))

# ============================================================
# 2) Normal monsters: parse every "#### <Family>" sub-section in Part 3 C.
#    Each entry can be either "<id> | <internal> | <display> | <ai>"
#    OR a range "<lo>-<hi> | <internalNN-MM> | <display> | <ai>".
# ============================================================
# Find Part 3 C section bounds
m_start = re.search(r"^### C\. Normal monsters", text, flags=re.MULTILINE)
m_end   = re.search(r"^### Estimated Normal-monster row count", text, flags=re.MULTILINE)
if not m_start or not m_end:
    print("ERROR: could not find Normal monsters section bounds")
    sys.exit(1)
normal_text = text[m_start.start():m_end.start()]

normals = []  # (hcIdx, display_with_family, family_short)
current_family = ""
for line in normal_text.splitlines():
    fam_match = re.match(r"^####\s+(.+?)(?:\s+\(MonType=.*?\))?(?:\s+—.*)?$", line)
    if fam_match:
        current_family = fam_match.group(1).strip()
        continue
    # Skip headers and separators
    if not line.startswith("|"): continue
    if "---" in line: continue
    if "hcIdx" in line: continue

    cells = [c.strip() for c in line.split("|")[1:-1]]
    if len(cells) < 3: continue
    hcidx_field = cells[0]
    internal    = cells[1] if len(cells) > 1 else ""
    display     = cells[2] if len(cells) > 2 else ""
    if not display or display == "—": continue

    # Range: "170-174" with internal "sk_archer1-5"
    rng_match = re.match(r"^(\d+)-(\d+)$", hcidx_field)
    if rng_match:
        lo, hi = int(rng_match.group(1)), int(rng_match.group(2))
        # Try to expand internal name
        inner_rng = re.match(r"^(.*?)(\d+)-(\d+)$", internal)
        for h in range(lo, hi + 1):
            if inner_rng:
                base = inner_rng.group(1)
                offset = h - lo
                start = int(inner_rng.group(2))
                inst = f"{base}{start + offset}"
            else:
                inst = internal
            # display might have "(Hell-tier)" or similar — keep as-is
            normals.append((h, f"{display} [{current_family}]"))
        continue

    # Comma list: "295" or "165-169"... or "692-693" — handled above
    # Single number
    single = re.match(r"^(\d+)$", hcidx_field)
    if single:
        h = int(single.group(1))
        normals.append((h, f"{display} [{current_family}]"))
        continue

    # Comma list: "446-449" already caught; also "699-702 | dkfig1-2, dkmag1-2 ..." — multi
    multi = re.match(r"^(\d+)-(\d+)$", hcidx_field)
    if multi:
        for h in range(int(multi.group(1)), int(multi.group(2)) + 1):
            normals.append((h, f"{display} [{current_family}]"))
        continue

# Dedupe by hcIdx, keeping first occurrence
seen = set()
deduped = []
for h, lbl in normals:
    if h in seen: continue
    seen.add(h)
    deduped.append((h, lbl))
deduped.sort(key=lambda x: x[0])

# ============================================================
# 3) Emit C header
# ============================================================
def csafe(s):
    """Escape a string for a C double-quoted literal."""
    return s.replace("\\", "\\\\").replace('"', '\\"')

with open(DST, "w", encoding="utf-8", newline="\n") as out:
    out.write("/* AUTO-GENERATED by Tools/extract_monster_catalog.py — DO NOT EDIT.\n")
    out.write(" * Source: Research/CHECK_SOURCES_MONSTER_AUDIT_2026-05-02.md\n")
    out.write(" * Re-run the script to refresh after the research markdown is updated.\n")
    out.write(" *\n")
    out.write(" * Two arrays:\n")
    out.write(" *   MONS_SU_FULL[]      — vanilla SuperUniques (idx 0..66, header row 42 skipped)\n")
    out.write(" *   MONS_NORMAL_FULL[]  — every player-killable MonStats row (de-duped, ban-list NOT applied)\n")
    out.write(" *\n")
    out.write(" * Both arrays use the existing `struct MonsCatalogEntry { int idx; const char* name; const char* note; };`\n")
    out.write(" * declared in d2arch_drawall.c. */\n\n")

    out.write(f"static const struct MonsCatalogEntry MONS_SU_FULL[] = {{\n")
    for idx, name, note in sorted(sus, key=lambda x: x[0]):
        out.write(f'    {{ {idx:3d}, "{csafe(name)}", "{csafe(note)}" }},\n')
    out.write("};\n")
    out.write(f"static const int MONS_SU_FULL_LEN = (int)(sizeof(MONS_SU_FULL)/sizeof(MONS_SU_FULL[0]));\n\n")

    out.write(f"static const struct MonsCatalogEntry MONS_NORMAL_FULL[] = {{\n")
    for h, lbl in deduped:
        out.write(f'    {{ {h:4d}, "{csafe(lbl)}", "" }},\n')
    out.write("};\n")
    out.write(f"static const int MONS_NORMAL_FULL_LEN = (int)(sizeof(MONS_NORMAL_FULL)/sizeof(MONS_NORMAL_FULL[0]));\n")

print(f"Wrote {DST}")
print(f"  SuperUniques: {len(sus)} entries")
print(f"  Normals:      {len(deduped)} entries")
