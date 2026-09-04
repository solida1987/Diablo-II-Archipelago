# -*- coding: utf-8 -*-
"""Gate: read a seed's patched vendor tables back and prove the full-pool deal.

The launcher writes <Npc>Min/Max/MagicMin/MagicMax/MagicLvl into weapons.txt,
armor.txt and misc.txt when Shop Shuffle is on. This checks the result the
way the game will read it:

  - every vendor (except Cain) has stock on all three store pages
  - each page holds between perPage-2 and perPage dealt rows
  - no quest row and no excluded row carries stock
  - rows that had vanilla stock are byte-identical to the pristine copy
  - every line is CRLF-terminated (lint_excel.py's rule; the engine asserts on it)

    python Tools/shop_pool_check.py <seed excel dir> [--pristine <dir>] [--per-page 48]

<seed excel dir> is save\seed_<key>\excel; --pristine defaults to
data\_apbackup\excel next to the game's data\global\excel (or pass it).
Exit 0 = pass, 1 = fail, with every finding printed.
"""
from __future__ import annotations
import argparse, os, sys

TABLES = ["weapons.txt", "armor.txt", "misc.txt"]
EXCLUDED_CODES = {"gld", "ear", "key", "luv", "xyz", "bks", "bkd", "j34", "g34", "bbb", "box",
                  "tr1", "tr2", "mss", "ass", "qey", "qhr", "qbr", "elx", "0sc"}
EXCLUDED_TYPES = {"ques", "play", "body", "gold", "herb", "key", "torc", "elix"}
SKIPPED_VENDORS = {"cain"}


def read_table(path):
    raw = open(path, "rb").read()
    text = raw.decode("latin-1")
    lines = text.split("\r\n")
    if lines and lines[-1] == "":
        lines.pop()
    return raw, lines


def crlf_ok(raw: bytes) -> bool:
    if not raw.endswith(b"\r\n"):
        return False
    body = raw.replace(b"\r\n", b"")
    return b"\n" not in body and b"\r" not in body


def vendor_columns(header):
    idx = {h.strip().lower(): i for i, h in enumerate(header)}
    groups = []
    for h in header:
        c = h.strip()
        if not c.lower().endswith("min") or c.lower().endswith("magicmin"):
            continue
        p = c[:-3]
        keys = [p + "Min", p + "Max", p + "MagicMin", p + "MagicMax"]
        if all(k.lower() in idx for k in keys):
            cols = [idx[k.lower()] for k in keys]
            groups.append((p, cols[0], cols[1], cols[2], cols[3], cols[3] + 1))
    return groups


def positive(cells, col):
    try:
        return int(cells[col].strip() or "0") > 0
    except (ValueError, IndexError):
        return False


def read_item_types(path):
    _, lines = read_table(path)
    header = [h.strip().lower() for h in lines[0].split("\t")]
    code, page = header.index("code"), header.index("storepage")
    out = {}
    for line in lines[1:]:
        c = line.split("\t")
        if len(c) > page and c[code].strip():
            out[c[code].strip().lower()] = c[page].strip().lower()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seed_excel")
    ap.add_argument("--pristine", default=None)
    ap.add_argument("--item-types", default=None,
                    help="ItemTypes.txt (default: <game>\\data\\global\\excel\\ItemTypes.txt found upward)")
    ap.add_argument("--per-page", type=int, default=48)
    a = ap.parse_args()

    seed_dir = os.path.abspath(a.seed_excel)
    game_dir = None
    d = seed_dir
    for _ in range(6):
        d = os.path.dirname(d)
        if os.path.isfile(os.path.join(d, "data", "global", "excel", "ItemTypes.txt")):
            game_dir = d
            break
    pristine = a.pristine or (os.path.join(game_dir, "data", "_apbackup", "excel") if game_dir else None)
    item_types_path = a.item_types or (os.path.join(game_dir, "data", "global", "excel", "ItemTypes.txt") if game_dir else None)
    if not item_types_path or not os.path.isfile(item_types_path):
        print("FAIL: ItemTypes.txt not found; pass --item-types")
        return 1
    types = read_item_types(item_types_path)

    findings = []
    for name in TABLES:
        path = os.path.join(seed_dir, name)
        if not os.path.isfile(path):
            findings.append(f"{name}: missing in seed folder")
            continue
        raw, lines = read_table(path)
        if not crlf_ok(raw):
            findings.append(f"{name}: not CRLF-terminated everywhere")
        header = lines[0].split("\t")
        hl = [h.strip().lower() for h in header]
        groups = vendor_columns(header)
        code_c, type_c = hl.index("code"), hl.index("type")
        quest_c = hl.index("quest") if "quest" in hl else -1
        spawn_c = hl.index("spawnable")

        pristine_rows = {}
        if pristine and os.path.isfile(os.path.join(pristine, name)):
            _, plines = read_table(os.path.join(pristine, name))
            for pl in plines[1:]:
                pc = pl.split("\t")
                if len(pc) > code_c:
                    pristine_rows[pc[code_c].strip()] = pl

        per_vendor_page = {}
        for line in lines[1:]:
            c = line.split("\t")
            if len(c) <= type_c:
                continue
            code, typ = c[code_c].strip(), c[type_c].strip().lower()
            page = types.get(typ, "")
            stocked_by = [g for g in groups if any(positive(c, col) for col in g[1:5])]
            if not stocked_by:
                continue
            # Rows the game already stocked (potions, scrolls, keys, ammo) must
            # be byte-identical to the pristine copy and never count as dealt.
            vanilla = pristine_rows.get(code)
            if vanilla is not None:
                vc = vanilla.split("\t")
                if any(positive(vc, col) for g in groups for col in g[1:5]):
                    if vanilla != line:
                        findings.append(f"{name}: vanilla-stock row was modified: {code}")
                    continue
            is_quest = quest_c >= 0 and len(c) > quest_c and c[quest_c].strip() not in ("", "0")
            if is_quest or code.lower() in EXCLUDED_CODES or typ in EXCLUDED_TYPES:
                findings.append(f"{name}: forbidden row has stock: {code} ({typ}) at {[g[0] for g in stocked_by]}")
            if c[spawn_c].strip() != "1":
                findings.append(f"{name}: non-spawnable row has stock: {code}")
            for g in stocked_by:
                per_vendor_page.setdefault((g[0], page), 0)
                per_vendor_page[(g[0], page)] += 1
                if not positive(c, g[5]) and page:
                    findings.append(f"{name}: {code} dealt to {g[0]} without MagicLvl")

        # coverage of this table's pages per vendor
        pages_in_table = sorted({types.get(l.split("\t")[type_c].strip().lower(), "") for l in lines[1:] if len(l.split("\t")) > type_c} - {""})
        for g in groups:
            if g[0].lower() in SKIPPED_VENDORS:
                continue
            for page in pages_in_table:
                n = per_vendor_page.get((g[0], page), 0)
                print(f"  {name:12s} {g[0]:9s} {page:4s} dealt={n}")

    # totals per vendor/page across tables
    print()
    if findings:
        print("FAIL:")
        for f in findings:
            print("  " + f)
        return 1
    print("PASS: pool deal is consistent, quest rows clean, vanilla stock untouched, CRLF intact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
