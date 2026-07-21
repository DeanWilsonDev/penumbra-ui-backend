# penumbra-ui-backend — `:hover`/`:active`/`:disabled` background-color should reach any `Box`, not just `Button`

> **Status:** Open — decision needed, not yet implemented. Flagged rather
> than silently picked in `docs/next_steps.md`'s 2026-07-21 entry (fixing
> `StyleApplier.cpp` against `penumbra`'s current `Button` API surfaced
> this as a real, no-longer-true assumption baked into the code) — this
> doc is the follow-up record.
> **Trigger:** `penumbra@dd1d6ab` ("Move interaction-state colors onto
> BoxStyle; give Box hover/pressed/disabled styling") moved
> `ColorBackgroundHovered`/`ColorBackgroundPressed`/`ColorBackgroundDisabled`
> off `Button` and onto `BoxStyle` itself, and gave `Box` its own
> `UpdateInteractionState`/`BackgroundForState` — this repo's own vendored
> `penumbra` pin was bumped to `dd1d6ab` in the same session as this doc.

---

## 0. Context

`LustreStyleApplier::Apply` (`src/PenumbraUiBackend/Lustre/StyleApplier.cpp:104-137`)
applies the universal `BoxStyle` slice to every widget via `AsBox` (any
`Box`, computed once at the top of the function), then applies
pseudo-class-scoped background colors through a second, narrower
`dynamic_cast`:

```cpp
// src/PenumbraUiBackend/Lustre/StyleApplier.cpp:115-137
auto* AsBox = dynamic_cast<Box*>(&Widget);
if (!AsBox) {
    return;
}
ApplyBoxStyle(AsBox->Style, Style);
ApplyLayout(*AsBox, Style);

// §2's "Pseudo-class-scoped variants": only background-color has a real
// field to receive a pseudo-scoped value today, and only on Button.
// :hover/:active/:disabled targeting anything else resolves into the IR
// (Lustre's own job) but has nowhere to land here yet -- stubbed, see
// penumbra-proto/docs/lustre_style_gaps_requirements.md §1.
if (auto* AsButton = dynamic_cast<Button*>(&Widget)) {
    if (Style.Hover && Style.Hover->BackgroundColor) {
        AsButton->Style.ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);
    }
    if (Style.Active && Style.Active->BackgroundColor) {
        AsButton->Style.ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);
    }
    if (Style.Disabled && Style.Disabled->BackgroundColor) {
        AsButton->Style.ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor);
    }
}
```

The comment citing "only on Button" and `lustre_style_gaps_requirements.md`
§1 is now stale. Those three fields live on `BoxStyle`
(`vendor/penumbra/include/Penumbra/Widgets/Styles.h:45-47`) as of
`dd1d6ab`, with an explicit doc comment on the struct itself stating the
intent plainly:

```cpp
// vendor/penumbra/include/Penumbra/Widgets/Styles.h:39-44
// Interaction-state background overrides -- universal (not Button-only) so
// Lustre's :hover/:active/:disabled selectors have somewhere to land on any
// classed element, matching how OnPressed/OnHovered/etc. on WidgetBase already
// aren't Button-exclusive (docs/lustre_style_gaps_requirements.md #1). Zero
// alpha (the default) means "no override for this state, keep ColorBackground"
// -- the same presence-flag convention GradientTop/ColorBackground use above.
```

This isn't just a data-shape move with no behavioral backing — `Box` itself
now actually *reads* these fields at both update and draw time, independent
of `Button`:

```cpp
// vendor/penumbra/src/Penumbra/Widgets/Box.cpp:253-261
Render::Color Box::BackgroundForState() const {
    switch (CurrentState) {
    case InteractionState::Hovered:  return Style.ColorBackgroundHovered.A != 0 ? Style.ColorBackgroundHovered : Style.ColorBackground;
    case InteractionState::Pressed:  return Style.ColorBackgroundPressed.A != 0 ? Style.ColorBackgroundPressed : Style.ColorBackground;
    case InteractionState::Disabled: return Style.ColorBackgroundDisabled.A != 0 ? Style.ColorBackgroundDisabled : Style.ColorBackground;
    default: return Style.ColorBackground;
    }
}
```

(`Box::UpdateInteractionState`, `Box.cpp:190-214`, drives `CurrentState`
for any `Box`, not just `Button` — `Button::BackgroundForState`,
`Button.cpp:64-68`, is the same shape, reading through the same
`Style.ColorBackground*` fields it inherited from `BoxStyle`.)

So today: a `.lustre` rule with a `:hover { background-color: ... }` block
targeting a plain classed `Box` (e.g. a `<Frame>`) resolves correctly on
the Lustre IR side (`ResolvedStyle.Hover`/`.Active`/`.Disabled` are
populated regardless of the target widget's C++ type — Lustre has no
concept of "Button" at all), but `StyleApplier.cpp`'s `Button`-only guard
silently drops it before it ever reaches `Box::Style`, even though the
receiving `Box` would render it correctly if it arrived. This is exactly
what `tests/LustreStyleApplierTests.cpp`'s
`TestHoverOverlayIsStubbedOnAPlainBox` currently asserts as today's
(intentional, but no longer well-justified) behavior.

## 1. Widen the pseudo-class guard to `Box`

### Proposed fix

```cpp
// src/PenumbraUiBackend/Lustre/StyleApplier.cpp — sketch, not exact
// (drop the `dynamic_cast<Button*>` re-check entirely; AsBox already
// covers Button, since Button : Box)
if (Style.Hover && Style.Hover->BackgroundColor) {
    AsBox->Style.ColorBackgroundHovered = ToPenumbraColor(*Style.Hover->BackgroundColor);
}
if (Style.Active && Style.Active->BackgroundColor) {
    AsBox->Style.ColorBackgroundPressed = ToPenumbraColor(*Style.Active->BackgroundColor);
}
if (Style.Disabled && Style.Disabled->BackgroundColor) {
    AsBox->Style.ColorBackgroundDisabled = ToPenumbraColor(*Style.Disabled->BackgroundColor);
}
```

`Button` needs no special case at all any more — it's a `Box`, and
`AsBox` is already computed above this block for the universal
`ApplyBoxStyle`/`ApplyLayout` calls. The `using Penumbra::Widgets::Button;`
line (`StyleApplier.cpp:106`) becomes dead and should be removed alongside
this change, same as the equivalent cleanup already done to
`PenumbraWidgetAdapter.cpp` in the same session (`AsButton`'s reset block
there was dropped once `AsBox->Style = BoxStyle{}` was confirmed to already
cover it).

### Required changes elsewhere

- `tests/LustreStyleApplierTests.cpp`'s `TestHoverOverlayIsStubbedOnAPlainBox`
  (currently asserting a plain `Box` does *not* receive the hover overlay)
  would need to flip to asserting it *does* — this is the one test whose
  premise this change directly inverts.
- `TestHoverOverlayReachesButtonOnlyOnButtonWidgets`'s name and framing
  ("...OnlyOnButtonWidgets") would need updating to reflect that `Button`
  is no longer special-cased — the assertion itself (a `Button` receives
  the hover overlay) stays true, just no longer for a `Button`-specific
  reason.
- `StyleApplier.cpp:122-126`'s comment block citing
  `lustre_style_gaps_requirements.md` §1 as still-open should be updated
  or removed — that gap is what `penumbra@dd1d6ab` closed on the Penumbra
  side; this fix is what closes it on this repo's side.

### What unblocks

A `.lustre` `:hover`/`:active`/`:disabled` rule targeting any classed
`Box`-based widget (a `<Frame>`, a `Checkbox`'s box base, etc.), not only a
`<Button>`, actually reaches Penumbra and renders — matching what
`BoxStyle`'s own doc comment states was the point of moving these fields
off `Button` in the first place.

## 2. Explicitly not requested

- **Extending pseudo-class-scoped styling beyond `background-color`** (e.g.
  a hover-scoped border color/width) — `BoxStyle` only has interaction-state
  overrides for background today (`Styles.h:45-47`); adding more would be a
  `penumbra`-side change, out of scope here.
- **Changing `ImageWidget`'s lack of `BoxStyle`** — noted as a separate,
  pre-existing gap in `StyleApplier.cpp:110-114`'s own comment; unrelated
  to this decision.
- **Auto-migrating `pharos-proto` or any other consumer** — this doc only
  covers the decision and fix shape in `penumbra-ui-backend` itself; a
  consumer relying on the current Button-only behavior (unlikely, since
  the gap meant plain-`Box` `:hover` rules were silently inert, not
  differently-styled) would need its own follow-up if one turns up.
