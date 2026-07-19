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
| `Slot` | never reaches this walker | the Iris runtime resolves every `<Slot>` first |
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

**A separate, serious defect surfaced while verifying this against real generated `.iris`
output — not part of this repo, flagged here for visibility:** a `<Slot>` callable capturing an
`iris::Signal<T>` local by reference (the exact pattern every `docs/iris_core_spec.md` example
uses) reads freed stack memory the instant the declaring component function returns —
confirmed with AddressSanitizer. This is a Stage 3 foundational-design gap in the `iris` repo
itself (`iris::Signal`/component-lifetime model), not something this adapter caused or can fix.

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
./build/tests/iris_penumbra_backend_tests
```
