# CJK fallback for UI text (size-matched, string-level)

## Summary

Loading a CJK-capable SD-card font already lets CrossPoint show CJK **book
content** correctly — but it did nothing for CJK in the **UI**: book titles in
the library, file names in the browser, list rows, and headers still showed
replacement boxes, because those are always drawn with the built-in Latin-only
flash fonts, independent of whatever SD font is loaded for the book text. This
PR closes that gap: any UI string containing a CJK codepoint is now
automatically routed to the user's already-selected SD-card font, at the same
point size as the surrounding Latin UI text. It also fixes a reader-size
regression that a naive implementation would otherwise introduce for CJK font
packs that ship small UI-only sizes.

## Background

CrossPoint already supports SD-card fonts with extended Unicode coverage, and
loading one is sufficient on its own to render CJK **book content** — the
reader's text-layout path already uses the loaded SD font family directly. The
renderer picks one physical point size per family (closest to the user's
reader-size setting: 12/14/16/18pt) and loads only that file to keep resident
memory down.

**UI chrome is a separate code path** and was unaffected by any of this: it
always draws with the built-in flash fonts (`SMALL_FONT_ID` @ 8pt,
`UI_10_FONT_ID` @ 10pt, `UI_12_FONT_ID` @ 12pt), which have no CJK glyphs at
all — regardless of which SD font, if any, is loaded for the book. So before
this PR, a user could select a CJK SD font, read CJK book content fine, and
still see boxes for a CJK book *title* on the library screen.

## What changed

### 1. String-level fallback routing (`GfxRenderer`)

- Added `fallbackFontMap_` (`std::map<int,int>`): primary UI font id ->
  size-matched fallback font id.
- Added `resolveTextFontId(fontId, text, style)`: walks the codepoints of
  `text`; if any is CJK (`utf8IsCjkBreakable`) and the primary font's active
  style can't render it (`EpdFontFamily::hasCodepoint`), returns the
  registered fallback id, else returns `fontId` unchanged.
- Wired into every text entry point that needs consistent metrics:
  `getTextWidth`, `drawText`, `getTextAdvanceX`, `drawTextRotated90CW`. Each
  resolves once up front and uses the resolved id for lookup, ascender calc,
  and advance-table fast path — so a string is never split across two fonts
  mid-render.
- Public API: `setFallbackFont(primaryId, fallbackId)` /
  `clearFallbackFonts()`.

Routing is **per string, not per glyph** — a mixed title like `三体 Vol.1`
renders entirely in the fallback font (including the Latin portion). This is
called out in the docs since a `Mono` fallback family will render the Latin
part at half/full width.

### 2. Interval-only glyph check (`EpdFont` / `EpdFontFamily`)

- Added `EpdFont::hasCodepoint(cp)`: binary search over the font's interval
  table. Unlike `getGlyph()`, it does **not** consult the on-demand SD miss
  handler or fall back to the replacement glyph — it answers "can this font's
  own table draw this codepoint" so the fallback decision doesn't trigger a
  disk load just to check.
- Added `EpdFontFamily::hasCodepoint(cp, style)` forwarding to the
  style-resolved `EpdFont`.

### 3. Size-matched fallback loading (`SdCardFontManager`, `SdCardFontSystem`)

- Refactored `SdCardFontManager::loadFamily` to share a new private
  `loadFile(file, familyName, renderer)` helper (alloc, disk load, id
  compute/collision check, register, insert) instead of inlining it.
- Added `loadFamilyExtraSize(family, renderer, pointSize)`: additively loads
  the family's `.cpfont` at an exact physical size (used for UI sizes 8/10/12)
  without touching the already-loaded reader-size font. Reuses an existing
  loaded size instead of double-loading if one already matches. Returns 0 if
  the family has no file at that size.
- `unloadAll` now calls `renderer.clearFallbackFonts()` before freeing the SD
  fonts the map points to, so no dangling fallback ids survive a family
  switch.
- `SdCardFontSystem::setupUiFallbacks(renderer)`: for each built-in UI size
  (8/10/12pt), calls `loadFamilyExtraSize` on the active family and, if a file
  exists, registers it via `renderer.setFallbackFont(uiFontId, sdFontId)`.
  Called after `loadFamily` succeeds in both `begin()` and `ensureLoaded()`.
  No-op (and logged at DBG) for any size the family doesn't ship.

### 4. Reader-size selection fix (`SdCardFontRegistry::findClosestReaderSize`)

Families that add 8/10pt UI-fallback files alongside the four reader sizes
(12/14/16/18) would otherwise shift the existing ordinal (index-based)
mapping used for custom font packs — e.g. a family with files at
`8,10,12,14,16,18` has 6 sizes, so `MEDIUM` (enum 1) would index to the
*second* size (10pt) instead of 14pt, making body text too small.

Fix: when a family provides all four standard reader sizes (12/14/16/18) for
the requested style, map the enum straight to `{12,14,16,18}` regardless of
how many extra sizes exist. Ordinal selection is now only used as a fallback
for families that *don't* have the full standard set (genuinely custom-built
packs like 10/12/14/16), and closest-match-by-delta remains the last resort
for packs with fewer than 4 sizes.

## Files touched

| File | Change |
|---|---|
| `lib/EpdFont/EpdFont.{h,cpp}` | `hasCodepoint()` interval-only lookup |
| `lib/EpdFont/EpdFontFamily.{h,cpp}` | `hasCodepoint()` forwarding |
| `lib/EpdFont/SdCardFontManager.{h,cpp}` | `loadFile()` extraction, `loadFamilyExtraSize()`, fallback-map clear on unload |
| `lib/EpdFont/SdCardFontRegistry.cpp` | `findClosestReaderSize` standard-4-sizes fast path |
| `lib/GfxRenderer/GfxRenderer.{h,cpp}` | `fallbackFontMap_`, `resolveTextFontId()`, wiring into draw/measure paths |
| `src/SdCardFontSystem.{h,cpp}` | `setupUiFallbacks()`, called from `begin()`/`ensureLoaded()` |
| `docs/sd-card-fonts.md` | New "CJK in the User Interface" section |

## Memory impact

Additive only when a CJK SD family is active and ships UI sizes: up to 3
extra small `.cpfont` files resident (8/10/12pt), on top of the one
reader-size file already loaded. No change for users without an SD font
selected, or whose family lacks 8/10/12pt files (those sizes silently keep
showing boxes, as before this change).

## Test plan

- [ ] Install a CJK family with reader sizes only (no 8/10/12) — verify UI
      still shows boxes for CJK (no crash, no regression) and body text sizes
      correctly.
- [ ] Install a CJK family with 8/10/12/12/14/16/18 (per `docs/sd-card-fonts.md`
      conversion example) — verify library titles, file browser rows, and
      headers render CJK at the right size, and reader body text at
      SMALL/MEDIUM/LARGE/EXTRA_LARGE still maps to 12/14/16/18pt.
  Note: the WIP `NotoSansMonoCJKhk-Regular.otf` in `lib/EpdFont/scripts/` is a
  local conversion source, not part of this PR's shipped changes.
- [ ] Switch SD font family while a CJK title is on screen — confirm no stale
      fallback font id (freed SD font) is referenced (`clearFallbackFonts()`
      ordering in `unloadAll`).
- [ ] Mixed Latin+CJK title (e.g. `三体 Vol.1`) — confirm whole string renders
      in the fallback font, not split per-glyph.
- [ ] No SD font selected — confirm UI CJK still shows boxes (fallback map
      empty, no behavior change).

## Out of scope

- CJK glyph coverage in the built-in flash fonts themselves (this stays a
  pure SD-fallback feature to avoid flash-size growth).
- Per-glyph (as opposed to per-string) fallback routing.
- Automatic UI-size font conversion for existing pre-built font packs in the
  `crosspoint-fonts` repo (tracked separately; packs need re-conversion with
  `--sizes 8,10,12,14,16,18` to benefit).
