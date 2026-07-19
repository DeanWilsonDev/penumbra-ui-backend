# iris-penumbra-backend — the Penumbra `Umbra::IWidget` Adapter

> **Status:** Implemented and tested against a real Penumbra widget tree (not just a
> mock). Closes `docs/iris_stage3_implementation_decision.md`'s (in the `iris` repo)
> "What remains deliberately deferred" item on a real Penumbra `IWidget` adapter.

---

## What this is

`PenumbraWidget` (`include/PenumbraUiBackend/PenumbraWidgetAdapter.h`) implements
`Umbra::IWidget` by wrapping a real `Penumbra::Widgets::WidgetBase` — the bridge Iris's
Stage 3 reconciler (`iris::ReconcileWidget`/`ReconcileChildren`, in the `iris` repo)
needs to update a real Penumbra widget tree without ever naming a Penumbra type itself.

`MakeMountFn(BuildContext)` produces an `iris::MountFn` combining Stage 2's
`BuildWidgetTree` (builds a real `WidgetBase` subtree) with `WrapExistingTree` (wraps it
in matching `PenumbraWidget` identity nodes) — this is what the reconciler calls
whenever it needs a whole fresh subtree.

## The ownership problem, and why it needs a dual-mode design

`Umbra::IWidget`'s reconciler contract assumes a stable *identity* per tree position: a
matched pair (same tag, same key) reuses the *same* `IWidget` object across
reconciliation passes, never rebuilding it — that's the whole point (widget state like
scroll position, focus, and animation lives on the object, not its position in a list).

But real ownership of a Penumbra widget has to live in exactly one place at a time, and
that place has to be `Box::Children` (a real, public `std::vector<std::unique_ptr<
WidgetBase>>`) whenever the widget is actually attached under a real parent — Penumbra's
own layout/draw/hit-test passes walk that vector directly, so a widget genuinely has to
be there to render.

If `PenumbraWidget` tried to *also* separately own a `unique_ptr<WidgetBase>` for every
attached child, ownership would be duplicated — a double-free waiting to happen, or an
awkward synchronization problem with no clean answer.

**The resolution:** a `PenumbraWidget` is either

- **owning** — holds the real `unique_ptr<WidgetBase>` itself. True for a freshly built
  subtree's root, or any widget nothing else currently claims.
- **attached** — a non-owning view onto a widget whose real ownership already lives
  inside some other `Box`'s own `Children` vector. True for every non-root node in an
  already-built subtree, and for any child once `InsertChildAt` has placed it into a
  real parent.

`InsertChildAt` transitions a child from owning to attached (`DetachOwnership()` hands
its real `unique_ptr<WidgetBase>` over to the parent `Box::InsertChildAt`).
`RemoveChildAt` reverses that: it finds the widget's entry inside the parent `Box::
Children`, pulls real ownership back out via direct manipulation of that public field
(Penumbra's `Box` has no "remove and return ownership" method — only `RemoveChild`,
which destroys — so this reads/erases the vector entry directly), and hands it back to
the returned wrapper, which becomes owning again.

This is exactly the state a widget needs to be in before `ReconcileList`
(`iris`'s `Reconciler.cpp`) can `InsertChildAt` it somewhere else — which is exactly
what the reconciler's extract-then-reinsert list-diff strategy does on every pass. The
wrapper's C++ object identity (`PenumbraWidget*`) never changes across this dance, which
is the actual property the reconciler depends on — real Penumbra ownership moves; the
wrapper doesn't.

## `ApplyPropDiff`

Maps `Umbra::IrisPropDiff`'s fields onto real `WidgetBase`/subclass state:

| Field | Target |
| --- | --- |
| `ClassName`, `OnPress`/`OnRelease`/`OnHover`/`OnFocus`/`OnChange` | `WidgetBase`'s own public fields — universal across every Core primitive |
| `Text` | `Label::Text`, via `dynamic_cast` (no-op for any other widget type) |
| `Src` | `ImageWidget::FilePath`, plus a real `LoadFrom()` re-decode if an image backend/renderer were provided at mount |
| `Handle`, `Checked` | **Deliberately no-op.** `Umbra::TextureHandle` currently carries no data to swap (a stub — see `umbra-interfaces`), and `<Checkbox>` isn't a Core primitive (`docs/iris_core_spec.md` §3.1), so no Core primitive reaches either path today. Revisit both together if either becomes real. |

## Verification

`tests/PenumbraWidgetAdapterTests.cpp` — against **real** `Penumbra::Widgets::Box`/
`Label` objects, not a mock:

- `WrapExistingTree` produces a wrapper tree whose shape matches the real Penumbra
  tree's shape exactly (child counts, class names).
- `ApplyPropDiff` reaches real `WidgetBase` fields, including a real invoked callback.
- Reconciling a same-tag-same-key pair reuses the *literal same* `Box*` address across
  the update — proving the ownership dance above doesn't silently rebuild anything.
- Reconciling a structural add actually grows the real `Box::Children` vector, not just
  the wrapper's own bookkeeping.
- The full stack (`iris::Signal` → `IrisRuntime`/`Tick` → `SlotState` → reconciler →
  `PenumbraWidget` → real Penumbra `Box`) runs end to end with no error.

A further attempt to verify this against a genuine `.iris` → `iris_cc` →
generated-header → real-`Signal`-driven pipeline (not just hand-constructed
`IrisComponent` values) surfaced a separate, serious defect **unrelated to this
adapter**: a `<Slot>` callable that captures an `iris::Signal<T>` local by reference
(exactly the pattern every example in `docs/iris_core_spec.md` uses) was reading freed
stack memory the instant the declaring component function returned — confirmed with
AddressSanitizer, not a fluke. This was a Stage 3 foundational-design gap
(`iris::Signal`/component-lifetime model), not this adapter or this repo — and it's now
**resolved** in the `iris` repo (`docs/iris_signal_lifetime_decision.md`): state
declarations use `IRIS_SIGNAL(Type, Name, InitExpr)`, binding to heap-allocated storage
tied to the component's own mounted lifetime instead of a stack local. Re-running the
same real `.iris` → `iris_cc` → `IRIS_SIGNAL` → `iris::Mount` pipeline against this
adapter's real `PenumbraWidget`/Penumbra `Label` after the fix landed, under
AddressSanitizer, completes with zero errors.

## What remains deliberately deferred

- **`Handle`/`Checked` prop diffing** — no-op today (see the table above); revisit once
  either has a real Core-primitive path to it.
- **Nested `<Slot>` discovery** — unchanged from `docs/iris_stage3_implementation_
  decision.md`: `SlotState` still assumes the trees its callable produces contain no
  further `<Slot>` tags of their own.

**Done, since this doc was written:** wiring a `<Slot>`'s own root widget into the
static tree above it — `MakeMountFn`/`SlotState` are now driven from Stage 2's
`BuildWidgetTree` via `iris::ResolveSlots`, for both the single-`IrisComponent`- and
list-returning callable shapes. See the README's own `<Slot>` wiring section and `iris`'s
`docs/iris_slot_stage2_wiring_decision.md`/`docs/iris_slot_list_wiring_decision.md`.
