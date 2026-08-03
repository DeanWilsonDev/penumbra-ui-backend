# penumbra-ui-backend / lustre — CSS-style `color`/`font` inheritance

> **Status:** Implemented, in `lustre` itself (`https://github.com/DeanWilsonDev/lustre`,
> commit `7779d94`, "Add CSS-style color/font inheritance (§1.7)"), consumed here via a
> `vendor/lustre` pin bump. This doc is the decision record for `penumbra-ui-backend`'s
> side of the work — the language-level spec addition lives in `lustre`'s own
> `docs/lustre_core_spec.md` §1.7.
> **Trigger:** a follow-up question on last session's `<Split>` handle-color work ("can the
> handle color be controlled independently from a Label's text color?") surfaced that
> Lustre had no property inheritance at all — every element resolved its style in total
> isolation; `.card .card-title`-style nesting only ever affected *selector matching*
> (which element a rule can reach), never *value propagation* from an already-resolved
> ancestor down to an unset descendant. Real CSS inherits `color`/`font` by default;
> Lustre didn't.

---

## Three scoping decisions

1. **Implemented in `lustre` itself, not bolted onto this repo.** `lustre`'s own
   `IStyleTarget` abstraction is explicitly designed to be backend/host-agnostic and
   reusable (`Resolver.h`'s own header comment). Inheritance built only as host-side glue
   in `penumbra-ui-backend` would mean any other consumer of the `lustre` library never
   gets it. This also meant moving the two-layer cascade composition (previously
   hand-rolled in this repo's `StyleResolution.cpp` as a `MergeInto` + two `Resolver::
   Resolve()` calls, since `Resolver::Resolve()` alone can't express "global unbounded,
   component bounded" in one call) into `lustre` as the new canonical
   `Lustre::ResolveStyle()`, since inheritance needs each ancestor's *fully-cascaded*
   value, and only that composition ever produces one. `Resolver::Resolve()` itself is
   untouched — this is additive, not a breaking change to `lustre`'s existing API/tests.

2. **Exactly `color` and `font` inherit** — CSS's own two classic inherited properties.
   Everything else (`background-color`, border/padding/margin, gradients, `box-shadow`,
   `display`/layout properties, `max-width`/`text-overflow`, `transform`) stays
   non-inherited, matching real CSS. This directly affects `<Icon>`'s color and last
   session's `<Split>` handle-color, since both already reuse `TextColor` — a nested
   `<Icon>`/`<Split>` with no `class` of its own now picks up an ancestor's `color`
   instead of staying at its zero-alpha default.

3. **No pseudo-state inheritance.** `:hover`/`:active`/`:disabled` overlays are resolved
   once, statically, per element — Lustre has no live per-frame recomputation of a
   widget's actual runtime interaction state, so a parent's `:hover { color: ... }`
   flowing dynamically into a child's own `:hover` the way real CSS would isn't something
   this architecture can do correctly. Only the base (non-pseudo) value inherits; a
   child's own overlay is entirely unaffected by inheritance, whether or not its base
   value was itself inherited.

## One non-obvious call: inheritance crosses component boundaries

`lustre_core_spec.md` §1.2's "component-boundary rule" ("a nested/descendant selector
cannot reach into a child component's internals") governs *selector* reach only.
Inheritance is a different mechanism — value propagation down an already-matched tree, not
selector matching — and real CSS/DOM inheritance has no notion of "component" at all: an
app's root layout setting `color` once should reach text nested arbitrarily deep within it,
the same way it would in a browser. So the inheritance walk climbs every real ancestor
(`IStyleTarget::Parent()`), never stopping at `IsComponentRoot()` the way descendant-selector
matching does. This is the more powerful, CSS-correct choice, consistent with the
precedent already set in `docs/native_lustre_styling_decision.md` (apply generically by
default, no restrictive special-casing) — flagged here since it's the one design call in
this work most likely to warrant a second look if it ever causes a surprise.

## What changed here

- `vendor/lustre` pin bumped `0d8bfa5` → `7779d94` (also picks up `da14913`'s
  `justify-content` property as a side effect of catching the pin up — not itself wired
  into `Walker.cpp`/`StyleApplier.cpp`, out of scope for this work).
- `src/PenumbraUiBackend/Lustre/StyleResolution.cpp` simplified to a thin pass-through to
  the new `::Lustre::ResolveStyle()` — the old `MergeInto` and two-call composition
  deleted (moved to `lustre` as the canonical implementation, see decision 1 above).
  `StyleResolution.h`'s public signature is unchanged, so `Walker.cpp:513` and
  `PenumbraWidgetAdapter.cpp:152` (the only two call sites) needed no changes at all.
- New end-to-end regression coverage in `tests/StyleWiringTests.cpp`:
  `TestColorInheritsFromAnAncestorFrameToANestedIconWithNoClass` (an `<Icon>` with no
  class of its own inherits `color` from an ancestor `<Frame class="row">`, proven
  through a real `BuildWidgetTree` call, not just a `lustre`-level unit test) and
  `TestBackgroundColorDoesNotInheritToANestedChildWithNoClass` (the same tree, proving
  `background-color` does *not* leak down). Full build + `penumbra_ui_backend_tests` (0
  failures) + `test_lustre` (41 passed, up from 29 — 10 new inheritance cases) +
  `test_iris` (138 passed) clean.

## Blast radius — flagged, not auto-fixed

This is a real behavior change for any existing `.lustre` file that has an unstyled
`<Label>`/`<Icon>`/`<Split>` nested inside a colored/fonted ancestor with no `class` of
its own — those elements now pick up the ancestor's `color`/`font` instead of silently
staying at the widget's own default. This is the intended, CSS-correct behavior (not a bug
to prevent), but it means:

- This repo's own `demo/` should get a visual check before calling this fully done.
- `pharos-proto` gets a heads-up that this changed, since it consumes the same `lustre`
  pin via its own vendoring — their own follow-up if anything looks different, per this
  ecosystem's established "ask the dependency, then stop" convention.

## Explicitly not requested

- **Extending inheritance to any property beyond `color`/`font`** — not proposed;
  everything else in CSS that would traditionally inherit (e.g. `text-align`, `visibility`)
  has no Lustre property yet to inherit in the first place.
- **Live/dynamic pseudo-state inheritance** — see decision 3 above; a real architectural
  change (per-frame recomputation of a widget's actual interaction state propagating to
  descendants) this doc deliberately doesn't attempt.
- **Wiring `justify-content`** — picked up incidentally by the pin bump; not itself part of
  this work, and not wired into `Walker.cpp`/`StyleApplier.cpp` here.
