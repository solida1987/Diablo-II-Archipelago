#ifndef D2ARCH_VERSION_H
#define D2ARCH_VERSION_H

/* Single source of truth for Diablo II Archipelago mod version. */

/* NOTE: rolled 2.9.9 -> 3.0.0 (MAJOR), NOT 2.9.10 ??? the in-game version-patch slot holds only 11 chars (see below): "Stable2.9.10" / "Stable2.10.0" are both 12 chars and would corrupt the adjacent format-string slot. */
#define D2ARCH_VERSION_MAJOR    3
#define D2ARCH_VERSION_MINOR    9
#define D2ARCH_VERSION_PATCH    21
/* No channel word any more ??? the version is just "v" and the numbers.
 *
 * "Beta", then "Stable", then "Unstable" each promised something about the
 * build that the number already says better, and the last rename was itself a
 * response to "Stable" overpromising. A bare version makes no claim at all.
 *
 * It also retires a constraint: the in-game slot holds 11 characters, and
 * "Unstable3.6.9" is 13, which is why there was a separate short form and why
 * the patch number had to roll over into MINOR after x.y.9. "v3.7.0" is six.
 * Both spellings are kept so the macros below stay the same shape as the
 * experimental line's. */
#define D2ARCH_VERSION_CHANNEL  "v"
#define D2ARCH_VERSION_CHANNEL_SHORT  "v"
#define D2ARCH_VERSION_STRING   "V3_7_5"  /* token id (keep underscores, upper) */

/* The user-visible version strings are DERIVED from MAJOR/MINOR/PATCH so they can never drift out of sync again (2.2.0 shipped showing "Stable2.1.0" because this one string was hand-bumped separately and missed). */
#define D2ARCH_STR2(x)  #x
#define D2ARCH_STR(x)   D2ARCH_STR2(x)
#define D2ARCH_VER_DOT  D2ARCH_STR(D2ARCH_VERSION_MAJOR) "." \
                        D2ARCH_STR(D2ARCH_VERSION_MINOR) "." \
                        D2ARCH_STR(D2ARCH_VERSION_PATCH)

/* No separating space: the channel is now the "v" prefix itself. */
#define D2ARCH_VERSION_DISPLAY  D2ARCH_VERSION_CHANNEL D2ARCH_VER_DOT  /* "v3.7.0" */

/* In-game version-patch string: D2's "v %d.%02d" format slot only holds ~11 chars + NUL (12 bytes). */
#define D2ARCH_VERSION_INGAME   D2ARCH_VERSION_CHANNEL_SHORT D2ARCH_VER_DOT  /* "U-3.6.3" */

/* the wide string USED TO BE a hand-written literal next to the three numbers, and it drifted: it read "Stable 3.4.2" while the numbers still said 3.3.1, so the in-game version patch, the log header, the bootstrap banner and the pipe handshake all announced 3.3.1 from 3.3.2 onwards. */
#define D2ARCH_WIDEN2(x)  L##x
#define D2ARCH_WIDEN(x)   D2ARCH_WIDEN2(x)
#define D2ARCH_WSTR2(x)   L#x
#define D2ARCH_WSTR(x)    D2ARCH_WSTR2(x)
#define D2ARCH_VERSION_DISPLAY_W  D2ARCH_WIDEN(D2ARCH_VERSION_CHANNEL)       \
                                  D2ARCH_WSTR(D2ARCH_VERSION_MAJOR) L"."      \
                                  D2ARCH_WSTR(D2ARCH_VERSION_MINOR) L"."      \
                                  D2ARCH_WSTR(D2ARCH_VERSION_PATCH)

/* And the 11-char in-game limit is now enforced rather than remembered: this fails the build instead of corrupting the adjacent format-string slot. */
typedef char d2arch_ingame_string_fits_the_patch_slot[
    (sizeof(D2ARCH_VERSION_INGAME) <= 12) ? 1 : -1];

#endif
