# iris-penumbra-backend — the Lustre style bridge

> **Status:** Implemented and tested against real Penumbra widgets (`Box`, `Button`,
> `Checkbox`, `Label`), not just structural assertions.

---

## What this is

`IStyleApplier`/`LustreStyleApplier` (`include/IrisPenumbraBackend/Lustre/StyleApplier.h`)
apply a `Lustre::ResolvedStyle` (`lustre/docs/lustre_core_spec.md` §3 — the output of
Lustre's parser/cascade/variable resolver) onto a real `Penumbra::Widgets::WidgetBase`,
mutating its `BoxStyle`/`ButtonStyle`-shaped fields in place. This is the piece
`lustre/docs/lustre_handoff.md` and `lustre_core_spec.md` both pointed at without writing:
"a bridge repo gains the code that reads that IR and calls `ApplyStyle()`."

## Why this lives here, as its own target — not folded into Walker/PenumbraWidgetAdapter,
## and not a fourth repo

Discussed and decided before implementation (not re-derived here in full): Iris and Lustre
must never depend on each other — one is structure, the other is style, and coupling them
would make a hypothetical future styling language (replacing Lustre) require changes to
Iris itself, which defeats the point of either being backend/host-agnostic.

That constraint is about the *core* packages (`iris`, `lustre`), not about this repo.
`iris-penumbra-backend` already exists specifically to know about two things that don't
know about each other (`iris` and `penumbra-proto`) and connect them for one concrete
backend. Lustre fits the same shape: a third backend-agnostic package that needs a
Penumbra-specific realization. Splitting that into its own repo (`lustre-penumbra-backend`)
was considered and rejected — it would need to reach into the same live `WidgetBase*`
objects `Walker.cpp`/`PenumbraWidgetAdapter.cpp` already build and own, creating either
duplicated plumbing or a dependency back onto this repo, i.e. not actually a clean split.
Repo count was the other explicit concern: a new repo per styling language doesn't scale,
where a new *target* inside this one repo does.

Kept as an independent CMake target (`iris_penumbra_backend_lustre`) rather than merged
into `iris_penumbra_backend` itself, because it genuinely doesn't need anything from
`iris` — it operates on plain `Penumbra::Widgets::WidgetBase&`, never `IrisComponent`.
A consumer that only needs the Walker (no styling yet, or a different styling approach)
doesn't have to pull in `lustre` at all.

## The swappability seam

`IStyleApplier` is the interface a replacement styling language's own applier would
implement, so a future swap touches only this one small file, never `Walker.cpp` or
`PenumbraWidgetAdapter.cpp`. The honest caveat: the interface's `Apply()` signature still
takes a `Lustre::ResolvedStyle` by name. The real contract being committed to is that
*shape* (background/border/box-model colors and lengths, a font request, pseudo-class
overlays) — not Lustre's grammar, cascade, or selectors, none of which this file touches
or includes. A future replacement's cheapest path is producing that same struct shape (or
this file being copied and re-pointed at a new one, if the shapes genuinely diverge) —
deliberately not generalized further than that today, since nobody has designed a second
styling language yet and speculating about its IR shape now would be guessing.

## What's implemented

- Dispatch by real widget type (`dynamic_cast` to `Button`/`Checkbox`/`Label`, falling
  back to the universal `Box` slice every widget type shares) — mirrors §2's
  "pseudo-class-scoped variants" note that only `Button` has real hover/press/disabled
  fields today.
- The universal box-model properties (`background-color`, `border-*`, `padding`,
  `margin`) apply to any `Box`-derived widget by mutating `Box::Style` directly.
- `:hover`/`:active`/`:disabled` overlays apply only to `Button::ColorBackground{Hovered,
  Pressed,Disabled}` — the one place Penumbra has real fields for them
  (`penumbra-proto/docs/lustre_style_gaps_requirements.md` §1). Targeting anything else is
  silently a no-op, matching Lustre's own "stubbed, not an error" treatment.
- `color`/`font-family`/`font-size` apply to `Label`, with `(path, size)` → `FontHandle`
  cached across `Apply()` calls (§2's font-request note).
- Properties left unset by a given `ResolvedStyle` are never written — repeated `Apply()`
  calls (e.g. on a class change) don't reset fields the new style doesn't mention.

## What's explicitly out of scope here

- `width`/`height`/`transform` — stubbed in Lustre's own IR already
  (`penumbra-proto/docs/lustre_style_gaps_requirements.md` §2–3); nothing in Penumbra can
  execute them yet, so there's nothing for this applier to do with them.
- `ImageWidget` — the one Penumbra widget type that isn't a `Box` subclass, so it has no
  `BoxStyle` at all; a pre-existing Penumbra gap, not something this file can paper over.
- Calling `Apply()` at the right moments (mount, class change, a future `.lustre`
  hot-reload) — that's `Walker.cpp`/`PenumbraWidgetAdapter.cpp`'s job to wire up, not yet
  done. This doc covers the applier existing and being correct in isolation, not its
  integration into the build/reconcile paths.
