# penumbra-ui-backend — Next Steps

> A handoff pointer, not a spec — kept short deliberately. Refreshed at
> the end of each work session; supersedes its own previous contents
> rather than accumulating history (the individual gap/spec docs are the
> durable record).
> Last updated: 2026-07-21.

## What's actually blocking downstream right now

**`StyleApplier.cpp` doesn't compile against `penumbra`'s current `Button`
API — breaks `pharos-proto`'s build entirely.**

`pharos-proto`'s `cmake/Dependencies.cmake` fetches this repo's *source*
only (via `FetchContent_Populate`, never running this repo's own
`CMakeLists.txt`) and compiles `src/PenumbraUiBackend/Lustre/StyleApplier.cpp`
directly against its own separately-fetched `penumbra` target, which tracks
`penumbra`'s real `main` (not this repo's `vendor/penumbra` submodule pin —
see "Also found" below). `penumbra@dd1d6ab` ("Move interaction-state colors
onto BoxStyle; give Box hover/pressed/disabled styling") removed
`Button::ColorBackgroundHovered/Pressed/Disabled` as direct fields on
`Button` (`include/Penumbra/Widgets/Button.h`) and moved them onto
`BoxStyle` (`include/Penumbra/Widgets/Styles.h:45-47`), inherited by
`ButtonStyle`, applied through `Box::Style` (`Box::Style` is a public
`BoxStyle`, `include/Penumbra/Widgets/Box.h:17`) rather than living
directly on `Button`.

`StyleApplier.cpp:126-135` still writes the old shape:

```cpp
if (auto* AsButton = dynamic_cast<Button*>(&Widget)) {
    if (Style.Hover && Style.Hover->BackgroundColor) {
        AsButton->ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);   // no longer a member
    }
    if (Style.Active && Style.Active->BackgroundColor) {
        AsButton->ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);  // no longer a member
    }
    if (Style.Disabled && Style.Disabled->BackgroundColor) {
        AsButton->ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor); // no longer a member
    }
}
```

Fails with `error: 'class Penumbra::Widgets::Button' has no member named
'ColorBackgroundHovered'` (and the two siblings), building
`penumbra_ui_backend_lustre` — confirmed on a clean `pharos-proto` checkout
of `main`, unrelated to any local pharos-proto change in flight.

**Minimal fix** — the three assignments become `AsButton->Style.Color...`
instead of `AsButton->Color...`:

```cpp
if (Style.Hover && Style.Hover->BackgroundColor) {
    AsButton->Style.ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);
}
if (Style.Active && Style.Active->BackgroundColor) {
    AsButton->Style.ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);
}
if (Style.Disabled && Style.Disabled->BackgroundColor) {
    AsButton->Style.ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor);
}
```

**Worth deciding, not just patching**: `dd1d6ab`'s whole point was making
these fields available on *any* `Box` via `BoxStyle`, not just `Button`
(so Lustre's `:hover`/`:active`/`:disabled` selectors can target a plain
classed `Box`, not only a `Button` — see `penumbra`'s
`docs/lustre_style_gaps_requirements.md` item 1, which this change closed).
`StyleApplier.cpp`'s `if (auto* AsButton = dynamic_cast<Button*>(&Widget))`
guard is now narrower than what `penumbra` actually supports — the minimal
fix above keeps today's `Button`-only behavior, but the dynamic_cast could
arguably become `dynamic_cast<Box*>` (i.e. reuse `AsBox`, already computed
above this block) to pass pseudo-class background colors through to any
styled `Box`, not just `Button`s. That's a real behavior decision, not a
compile fix — flagging it rather than picking silently.

## Also found while investigating the above

- **`.gitmodules`** still points `vendor/penumbra` at
  `https://github.com/DeanWilsonDev/penumbra-proto.git` — that repo was
  renamed to `penumbra` (old URL still redirects via GitHub today, but is
  a deprecated alias, not the canonical name). Worth updating alongside
  the fix above, same session.
- **`vendor/penumbra`'s own submodule pin** (`3201a17`, last bumped in
  `a184435` "Bump vendor/iris, vendor/lustre, vendor/penumbra pins") is
  well behind `penumbra`'s real `main` (`dd1d6ab` as of this writing) —
  this repo's own standalone build (via its vendored submodules) isn't
  exercising the same `penumbra` code `pharos-proto` actually compiles
  against. Re-bumping the submodule pin after the fix above would at
  least make this repo's own build and `pharos-proto`'s consumption of it
  agree again.

## Read first

- `docs/build_context_style_mismatch_gap.md` — a previous real
  `pharos-proto`-triggered bug in this same `StyleApplier`/`Walker`
  pairing, same shape of "compiles clean, fails silently downstream" risk
  worth keeping in mind when touching this file.
