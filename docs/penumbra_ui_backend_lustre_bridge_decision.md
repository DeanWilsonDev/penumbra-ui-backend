# penumbra-ui-backend — the Lustre style bridge

> **Status:** Implemented, wired into both the mount path (`Walker.cpp`) and the
> reconcile-time class-change path (`PenumbraWidgetAdapter.cpp`), and tested end to end
> against real Penumbra widgets (`Box`, `Button`, `Checkbox`, `Label`) built from real
> `IrisComponent` trees — not just structural assertions on the applier in isolation.

---

## What this is

`IStyleApplier`/`LustreStyleApplier` (`include/PenumbraUiBackend/Lustre/StyleApplier.h`)
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

Kept as an independent CMake target (`penumbra_ui_backend_lustre`) rather than merged
into `penumbra_ui_backend` itself, because it genuinely doesn't need anything from
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
- Properties left unset by a given `ResolvedStyle` are never written by `Apply()` itself —
  correct for merging multiple matched rules *within* one resolve (a base rule plus a
  more-specific nested one), but wrong for re-styling on a later class change, where a
  property the old class set and the new one doesn't should revert, not linger. That reset
  is the caller's job, not `Apply()`'s — see "Wiring into the mount and reconcile paths"
  below for where it actually happens.

## Wiring into the mount and reconcile paths

Both call sites resolve style through `PenumbraUiBackend::Lustre::ResolveStyle()`
(`include/PenumbraUiBackend/Lustre/StyleResolution.h`), not `Lustre::Resolver::Resolve()`
directly — `Resolve()` takes one `Unbounded` flag applied to both cascade layers in a
single call, which can't express §1.2's asymmetric rule (global.lustre unbounded, a
component's own file bounded to its own root) when both layers are supplied together.
`ResolveStyle()` composes two `Resolve()` calls (global alone, unbounded; component alone,
bounded) and merges them, component fields winning, without needing any change to `lustre`
itself.

**Mount (`Walker.cpp`):** `BuildWidgetTreeInternal` builds a `Lustre::IStyleTarget`
(`WalkerStyleElement`) for every node as part of its existing recursive descent — no
separate tree walk. Each node's `WalkerStyleElement` is a real local variable on the call
stack, alive for exactly as long as its descendants are being built, with a `Parent`
pointer to its own caller's `WalkerStyleElement` — descendant-selector ancestry falls out
of the recursion for free. `Node` (the argument to whichever `BuildWidgetTree()` call
started this recursion) is always treated as the component-root boundary; correct for
every real caller today (only ever invoked at a whole mount's root, directly or via
`iris::ResolveSlots`), but not detectable from `IrisComponent` alone if a future caller
ever composed more than one component's worth of content into a single `BuildWidgetTree`
call without going through `<Slot>`.

**Reconcile (`PenumbraWidgetAdapter.cpp`):** `ApplyPropDiff` re-resolves and re-applies
style whenever `Diff.ClassName` is set. Two problems specific to reconcile time, both
solved here rather than left as gaps:

- *Stale properties.* Before re-applying, `ResetStyleableFields` resets the widget's own
  `BoxStyle` (and `Button`/`Label`'s extra fields) to their defaults, so a property the
  new class doesn't set actually reverts instead of keeping the old class's value.
- *Ancestry without a recursive call stack.* Unlike mount, there's no natural place to
  keep a `WalkerStyleElement`-style chain alive — `ApplyPropDiff` fires standalone,
  potentially long after the widget was built. `PenumbraWidget` gained a `Parent_` pointer
  (set in `InsertChildAt`/`AdoptChildrenFromRawTree`, cleared in `RemoveChildAt`) so the
  *wrapper* tree itself can be walked on demand; `BuildReconcileStyleChain` builds a small
  parallel chain of `ReconcileStyleElement` objects with stable addresses (a
  `std::vector<std::unique_ptr<...>>`, since `Lustre::IStyleTarget::Parent()` must return
  a pointer valid for the whole `Resolve()` call, ruling out a plain recursive helper that
  would return a pointer to its own stack frame).

**Known limitation of the reconcile path specifically:** `PrimitiveTag()` there
(`InferPrimitiveTag`) is inferred from the live widget's C++ type via `dynamic_cast`, not
carried over from the original `IrisElementTag` (which isn't preserved anywhere once a
widget is built). `Frame` and `Grid` both build to a plain `Box`
(`Walker.cpp`'s own `BuildGrid` comment), so a primitive-element selector like `grid { }`
only re-matches correctly at mount time; on a later class change, every plain `Box` reads
as `"Frame"`. Class-selector rules — the overwhelmingly common case, and every worked
example in `lustre_core_spec.md` — are unaffected.

Both `Context.Style`/`Context.StyleApplier` (`Walker.h`'s `BuildContext`) are nullable; a
caller that never sets them gets exactly the pre-wiring behavior, unchanged, with zero
`lustre` calls made anywhere in the built binary's actual execution path.

## What's explicitly out of scope here

- `width`/`height`/`transform` — stubbed in Lustre's own IR already
  (`penumbra-proto/docs/lustre_style_gaps_requirements.md` §2–3); nothing in Penumbra can
  execute them yet, so there's nothing for this applier to do with them.
- `ImageWidget` — the one Penumbra widget type that isn't a `Box` subclass, so it has no
  `BoxStyle` at all; a pre-existing Penumbra gap, not something this file can paper over.
- A future `.lustre` hot-reload path (re-resolving and re-applying style for an entire
  already-mounted tree when a file on disk changes, not just on a class change) — needs a
  whole-tree walk entry point (`iris::RegisterRoot`/`GetRoot`, tracked in
  `iris/docs/lustre_hotreload_iris_requirements.md`) that doesn't exist yet. Everything in
  this doc covers per-widget resolution (mount, and a single widget's own class changing),
  not a bulk re-style pass.
