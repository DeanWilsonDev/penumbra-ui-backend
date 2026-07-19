# iris-penumbra-backend

The Penumbra backend for [Iris](https://github.com/DeanWilsonDev/iris): the code that walks a
parsed, props-resolved `IrisComponent` IR tree and builds a real
[Penumbra](https://github.com/DeanWilsonDev/penumbra-proto) widget tree from it, via Penumbra's
fluent `Builder` API (`Box::Builder`, `Label::Builder`, etc.).

## Why this is a separate repo

Iris's core (preprocessor + IR + runtime library) is backend-agnostic by design — it's meant to
support more than one backend over time (Penumbra now, an Umbra Engine/Nyx backend deferred).
Penumbra is a general-purpose retained-mode widget library with no inherent reason to know Iris
exists either. Neither should have to pull in the other's build just to compile on its own, so
the code that bridges them — this repo — is its own thing: it vendors both `iris` and
`penumbra-proto` as git submodules and depends on both, while neither of them depends on it or
on each other.

## Status

This is Stage 2 of Iris's roadmap (see `iris`'s `docs/iris_handoff.md` §6). The walker is
implemented: `IrisPenumbraBackend::BuildWidgetTree()` (`include/IrisPenumbraBackend/Walker.h`)
takes a single `IrisComponent` IR node and recursively builds the equivalent real Penumbra
widget tree via each Core primitive's own fluent `Builder`. It's a one-shot tree build only —
no diffing, no identity tracking (`key` never reaches `IrisComponent`; it's stripped by Iris's
preprocessor before codegen, so there's nothing here for a `key`-based live-widget map to key
off of — that's a Stage 3 reconciler concern layered on top of this, not part of it).

Mapping (`docs/iris_core_spec.md` §3.1, cross-checked against the real Penumbra source):

| `IrisElementTag` | Penumbra widget | Notes |
| --- | --- | --- |
| `Frame` | `Box` | via `Box::Builder` |
| `Inline` | `InlineContainer` | real wrapping inline-flow, distinct from `Text`/`Label` |
| `Grid` | `Box` (`LayoutMode::HorizontalStack`) | stub — Penumbra has no real grid layout yet |
| `Image` | `ImageWidget` | leaf; `LoadFrom()` called separately via `BuildContext` |
| `Text` | `Label` | `FontBackend`/`Font` set from `BuildContext` (no Builder method for either) |
| `Slot` | `nullptr` during this static build | see "`<Slot>` wiring" below — resolved separately, afterward |
| `None` | `nullptr` | the `<Slot>`-returned-`nullptr` sentinel — "no widget here" |

`BuildContext` (`Walker.h`) carries the resources Penumbra's own `Builder`s don't expose: a
font backend/handle for `Label`, and an image backend/SDL renderer for `ImageWidget::LoadFrom`.
Both are optional — a widget still builds successfully with either left null, useful for
structural tests that don't need a real font/SDL context.

Verified against the full pipeline, not just in isolation: a real `.iris` component
(`HealthBar`, with a `<Frame class="...">` wrapping a `<Text>{props.label}</Text>`) compiled
through `iris`'s own `iris_cc` CLI, `#include`d, called, and the resulting `IrisComponent` fed
through `BuildWidgetTree` — producing a real `Box`/`Label` tree with the class name, child
count, and interpolated text all correct. See `tests/WalkerTests.cpp` for the structural test
suite (`IrisElementTag::None` skipping, event-prop wiring, the `<Grid>` stub, nested recursion,
etc.).

**Stage 3's `Umbra::IWidget` adapter is also implemented**: `PenumbraWidget`
(`include/IrisPenumbraBackend/PenumbraWidgetAdapter.h`) wraps a real `Penumbra::Widgets::
WidgetBase` to satisfy Iris's backend-agnostic reconciler contract, so `iris::ReconcileWidget`/
`ReconcileChildren` (in the `iris` repo) can update a real Penumbra widget tree in place —
verified against real `Box`/`Label` objects, not a mock, including that a same-tag-same-key
update reuses the literal same `Box*` object rather than rebuilding it. See
`docs/iris_penumbra_backend_adapter_decision.md` for the ownership design (a dual owning/
attached mode was needed to reconcile "the reconciler needs a stable identity" against "real
Penumbra ownership has to live in exactly one `Box::Children` vector at a time") and
`tests/PenumbraWidgetAdapterTests.cpp`.

**Resolved (in the `iris` repo, not here):** a separate, serious defect surfaced while
verifying the adapter against real generated `.iris` output — a `<Slot>` callable capturing an
`iris::Signal<T>` local by reference (every `docs/iris_core_spec.md` example's own pattern)
read freed stack memory the instant the declaring component function returned, confirmed with
AddressSanitizer. Fixed there via `IRIS_SIGNAL(Type, Name, InitExpr)`
(`iris`'s `docs/iris_signal_lifetime_decision.md`) — state declarations now bind to
heap-allocated storage instead of a stack local. Re-verified end to end against this repo's own
real Penumbra widgets under AddressSanitizer with zero errors after the bump.

**`<Slot>` is now wired in, for both callable shapes**: `BuildWidgetTree` treats a `<Slot>` child
exactly like `None` during the static build (contributes nothing); a new `iris::ResolveSlots()`
(`iris`'s `include/Iris/SlotResolution.h`) then walks the just-built tree and the source
`IrisComponent` tree in lockstep, and for each `<Slot>` found, attaches a `SlotState` to its
exact position and performs its initial mount. A `SlotSiblingGroup` shared by every `<Slot>`
sibling under the same static parent recomputes each slot's absolute position fresh on every
reconcile, so a list-returning `<Slot>`'s own growth/shrinkage (and a sibling toggling to/from
`None`) correctly shifts whatever comes after it. Every subsequent `iris::Tick()`-triggered
reconcile updates the real position(s) in place — pure `Umbra::IWidget`, so this logic is
entirely backend-agnostic and lives in `iris`'s own runtime; the only change needed here was
`BuildWidgetTree`'s own `Slot` case. Verified against real `Box`/`Label` objects, not a mock: a
live `iris::Signal` update reaching a real `Box::Children` vector end to end, including under
AddressSanitizer + UndefinedBehaviorSanitizer with zero errors (`tests/SlotWiringTests.cpp` here;
`iris`'s own `tests/SlotResolutionTests.cpp` and `tests/cimmerian/SlotSiblingGroupTests.cpp`
cover the list-sibling cases directly). Still deliberately deferred: nested `<Slot>` discovery.
Full design: `iris`'s `docs/iris_slot_stage2_wiring_decision.md` and
`docs/iris_slot_list_wiring_decision.md`.

**The Lustre style bridge is now implemented**: `IStyleApplier`/`LustreStyleApplier`
(`include/IrisPenumbraBackend/Lustre/StyleApplier.h`, its own independent CMake target,
`iris_penumbra_backend_lustre`) apply a `Lustre::ResolvedStyle` (from the `lustre` repo — the
output of Lustre's parser/cascade/variable resolver, `lustre/docs/lustre_core_spec.md` §3) onto
a real Penumbra widget's style fields: the universal box-model slots for any `Box`-derived
widget, `:hover`/`:active`/`:disabled` overlays onto `Button`'s real interaction-state fields
(the one widget type with them today), and `color`/`font-family`/`font-size` onto `Label`, with
font-handle resolution cached by `(path, size)`. Deliberately its own target and its own vendored
submodule (`vendor/lustre`) rather than folded into `iris_penumbra_backend` — it needs nothing
from `iris` (it operates on plain `Penumbra::Widgets::WidgetBase&`, never `IrisComponent`), so a
consumer that doesn't use Lustre doesn't pull it in. See
`docs/iris_penumbra_backend_lustre_bridge_decision.md` for why this lives here rather than in a
fourth repo, and what the `IStyleApplier` interface does and doesn't buy in terms of swapping in
a future, non-Lustre styling language. Not yet wired into `Walker.cpp`'s mount path or
`PenumbraWidgetAdapter::ApplyPropDiff`'s class-change path — the applier is implemented and
tested in isolation (`tests/LustreStyleApplierTests.cpp`), but nothing calls it automatically yet.

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
./build/tests/iris_penumbra_backend_tests
```
