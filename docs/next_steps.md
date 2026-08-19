# penumbra-ui-backend — Next Steps

> A handoff pointer, not a spec — kept short deliberately. Refreshed at
> the end of each work session; supersedes its own previous contents
> rather than accumulating history (the individual gap/spec docs are the
> durable record).
> Last updated: 2026-08-19.

## Done (2026-08-19): widened `BuildContext::LifecycleHost` from `Penumbra::Application*` to `Penumbra::LifecycleRegistry*`

**Cross-repo ask from `pharos-proto`, filed and implemented same day.** `vendor/penumbra` bumped
`e449d11` → `f4db86f` ("Factor IWidgetLifecycle registration/ticking out of Application into a
standalone LifecycleRegistry") — no nested-submodule re-init needed, `penumbra` itself has no
submodules. Confirmed `f4db86f` was origin/main's actual HEAD before pinning to it.

Implemented exactly the three-step fix this entry originally proposed, all confirmed correct
against the landed `penumbra` source before touching anything:

1. `Walker.h`: `LifecycleHost` retyped `Penumbra::Application*` → `Penumbra::LifecycleRegistry*`;
   `#include "Penumbra/Application.h"` swapped for `#include "Penumbra/LifecycleRegistry.h"` —
   confirmed `Application` had no other use anywhere else in the header first (`grep -n
   Application`, one include + one field, nothing else).
2. `Walker.cpp`: `RegisterLifecycleIfPresent`'s local `Host` pointer retyped to match; no other
   change, since `RegisterLifecycle`/`UnregisterLifecycle` exist unchanged on the new type.
3. `tests/WalkerTests.cpp`: **four** tests needed the fixture swap, not three as originally
   estimated (`TestLifecycleRegistersOnBuildWhenLifecycleHostIsSet`,
   `TestLifecycleUnregistersWhenTheBuiltWidgetIsDestroyed`,
   `TestNoComponentInstanceMeansNoRegistrationEvenWithALifecycleHost`,
   `TestNestedNonSlotComponentInvocationAlsoRegistersItsOwnLifecycle`) — grep undercounted because
   two of them share a line-wrapped declaration. All four now construct a bare
   `Penumbra::LifecycleRegistry Host;` directly. This turned out load-bearing, not just simpler:
   `Application::Tick` became `private` in the `f4db86f` refactor (Run()'s own frame loop is now
   the only caller; external ticking goes through `LifecycleRegistry::Tick`, which stayed public),
   so the two tests calling `Host.Tick(...)` would no longer have compiled against an
   `Application`-typed fixture at all.

   Checked the "independent coverage" caveat directly rather than assuming: `penumbra-proto`'s
   vendored checkout has no `tests/` directory and no test target anywhere in its
   `CMakeLists.txt` — so nothing outside this repo exercises `Application::RegisterLifecycle`/
   `UnregisterLifecycle`'s own forward onto its owned `LifecycleRegistry`. Added one new test,
   `TestApplicationRegisterLifecycleStillForwardsToItsOwnLifecycleRegistry` (`tests/
   WalkerTests.cpp`), exercising `Penumbra::Application`'s own public API directly (unrelated to
   `BuildContext`/`Walker` otherwise) — `RegisterLifecycle`/`GetLifecycleRegistry().Tick`/
   `UnregisterLifecycle` all still forward correctly post-refactor. New
   `FakePenumbraLifecycle` (implements `Penumbra::IWidgetLifecycle`, distinct from the existing
   `FakeLifecycle` which implements `Umbra::IWidgetLifecycle`) backs it.

Full rebuild clean + `penumbra_ui_backend_tests` (219 assertions, up from 215 — 4 new from the
added test, 0 net change in test *count* elsewhere — 0 failures) + `test_iris` (251 passed) +
`test_lustre` (42 passed) clean.

**What this unblocks:** `pharos-proto/src/main.cpp` can now construct its own
`Penumbra::LifecycleRegistry` alongside its hand-rolled `PlatformWindow`/`Renderer`, call
`.Tick(deltaSeconds)` once per frame, and set `appContext.LifecycleHost`/`NyxHost` — letting the
real `pharos` binary's `<InspectorPanel />` mount sync live for the first time, and letting
`pharos-proto` delete `inspector_panel.cpp`'s hand-rolled `buildInspectorPanel()`/`panel.sync()`
mechanism entirely (bar its own visual-test host, which mounts it directly), matching what
`inspector_panel_native.cpp` already did for `pharos_nyx_bootstrap`. That wiring itself is
`pharos-proto`'s own follow-up — nothing further needed here. Bumping `pharos-proto`'s own vendored
`penumbra-ui-backend` pin to pick this up is also that repo's follow-up.

## Done (2026-08-18): a Nyx-authored `OnMount`/`OnTick` can now reach its own component's `ref`'d widgets via `GetRef`

**Implemented, closing the ask below.** `vendor/iris` bumped `1349183` → `efc6c1e` first —
the pinned commit predated `NyxLifecycleAdapter`/the whole `.irisx` reserved-name
`OnMount`/`OnUnmount`/`OnTick` wiring entirely (added in iris-proto's `69dba6d`; `efc6c1e`
on top fixes `IrisNyxDriver::GetFileScope` re-pushing globals into a cached file scope,
cited by this ask's own "one real open design question" section). No nested-submodule
re-init needed (`libs/amanuensis`/`libs/cimmerian`/`libs/umbra-interfaces` all unchanged
between those two commits). This surfaced a real gap in how this doc's own ask was
verified before implementation started: without the pin bump, the new regression test
below crashed on a null `Instance->Lifecycle` (the reserved-name detection simply didn't
exist yet at the old pin) — worth remembering for any future ask grounded in a sibling
repo's real source: confirm the *vendored* pin actually has what the sibling repo's HEAD
has, not just that the sibling repo's own checkout does.

**The "one real open design question" is resolved in favor of per-instance scoping, not
global** — confirmed against real source, not the "global is likely sufficient" lean the
original ask below tentatively reasoned toward:

- A global `GetRef` (on `Runtime.Globals()`) would be one flat binding shared by every
  concurrently-mounted lifecycle-bearing component. The ask itself names
  Explorer/Atlas/Inspector/Toolbar as eventual `GetRef` consumers, all mounted at once in
  the same app tree — the last `BuildWidgetTree` call to (re)register it would silently
  win for every other already-mounted component's own later `OnTick` calls too, a real
  cross-component ref collision (ref names are only unique *within* one `BuildWidgetTree`
  call, per `RefMap`'s own doc comment, not across separately-built sibling components).
- Per-instance scoping needs **zero new iris-proto/nyx-proto API** — confirmed by reading
  both repos' real source, not assumed: `Iris::NyxDriverState` (`IrisNyxDriver.h`) is
  already public, `RenderScope` is the exact Environment
  `BuildFreeFunctionLifecycleAdapter`'s own `Scope.context.env->FindOwn("OnTick")` check
  already reads (`IrisNyxDriver.cpp`), and `nyx::runtime::Environment::Define` (already
  public) creates a binding "in this scope only." Nyx variable lookup is late (walks the
  live parent chain at call time, not a snapshot taken when a closure was declared), so
  defining `GetRef` into that same live Environment object at build time is visible to
  that instance's `OnMount`/`OnTick` with no ordering hazard, and with no cross-instance
  collision at all, since every instance gets its own Environment.
- Confirmed a real, deliberate scope boundary rather than trying to close it: Model 2
  (class-based) components dispatch lifecycle hooks via
  `Interpreter::TryCallInstanceMethod` against the *file-level* shared
  interpreter/registry (`BuildClassLifecycleAdapter`), never through any per-instance
  Environment — there is no analogous scope to define `GetRef` into for that model today.
  A class-based `OnTick` calling `GetRef` gets an ordinary Nyx-level "undefined variable"
  error, not a wrong-widget bug. Model 1 (free-function) is the only authoring style every
  `.irisx` example in this ecosystem has used so far, including the concrete target
  (`InspectorPanel.irisx`), so this isn't a blocker for the ask that motivated it.

**Implemented** (`include/PenumbraUiBackend/Walker.h`, `src/PenumbraUiBackend/
Walker.cpp`):

- `BuildContext` gained `nyx::host::NyxRuntime* NyxHost{nullptr}` — same optional-resource
  convention every other field already follows.
- `WidgetRefHandle` (anonymous namespace, `Walker.cpp`): the four operations the original
  ask named as the validated minimum bar — `SetText`/`SetIconName` (via `dynamic_cast` to
  `Label`/`IconWidget`), `SetColor` (four 0-255 channel ints, since Nyx has no native color
  literal), and `SetVisible` (`WidgetBase::SetIsVisible` directly, no cast needed). A
  mismatched call (e.g. `SetIconName` on a plain `Box`) is a silent no-op, matching this
  walker's existing "malformed/mismatched input is simply not applied" tolerance.
- `EnsureWidgetRefTypeRegistered`: registers the `WidgetRef` host type against
  `Context.NyxHost` exactly once (cached by `NyxRuntime*`), reusable across every
  component's own `GetRef` closure — `HostObject` method dispatch resolves entirely
  through `host->descriptor` at call time (confirmed against `nyx-proto`'s
  `Interpreter::CallInstanceMethod`), never through any Environment/`Globals()` lookup, so
  one registration is visible from every returned handle regardless of which component's
  own `RenderScope` built it.
- `RegisterGetRefIfPresent`: the actual per-instance wiring described above, called at the
  same per-node point `RegisterLifecycleIfPresent` already fires in
  `BuildWidgetTreeInternal` — and *before* it, so `GetRef` is already reachable from inside
  the Nyx-authored `OnMount` body that `RegisterLifecycleIfPresent`'s own
  `Host->RegisterLifecycle` call triggers immediately afterward. Gated on
  `Node.Instance->DriverState` being non-null (exclusively set by `IrisNyxDriver`, per
  `ComponentInstance::DriverState`'s own doc comment, so this never misinterprets a
  C++-authored `.iris` component's driver state as a Nyx one). Handles are cached per ref
  name inside the closure (not reallocated every call) to stay bounded under a real
  per-frame `OnTick`.

New regression coverage in `tests/WalkerTests.cpp`, going through the real `.irisx` ->
`IrisIrDocument` -> `Component` pipeline end to end via `IrisNyxDriver::MountRoot` (a real
on-disk fixture, `TempProject`, mirroring `iris-proto`'s own `IrisNyxDriverTests.cpp` test
helper exactly — not a hand-built `Component` tree, since `GetRef` needs a real
`NyxDriverState`/`Environment` behind `Node.Instance->DriverState`):
`TestNyxOnTickCanLookUpAndMutateARefdWidgetViaGetRef` (a `ref="label"` `<Text>` node's real
built `Label` is mutated by an `auto OnTick = ...` local calling
`GetRef("label").SetText/SetColor/SetVisible(...)`, verified via `Instance->Lifecycle
->OnTick(...)`, not a mock) and `TestNoNyxHostMeansNoGetRefCapabilityEvenWithARealNyxLifecycle`
(`Context.NyxHost` left null means `GetRef` doesn't exist at all — the same real fixture's
`OnTick` call fails at the Nyx level, caught inside `NyxRuntime::EvaluateInScope`, and never
reaches the real `Label`). Full rebuild + `penumbra_ui_backend_tests` (215 assertions, 0
failures) + `test_iris` (251 passed, up from 183 — the new pin's own `IrisNyxDriverTests.cpp`
lifecycle coverage) + `test_lustre` (42 passed) clean.

**What this unblocks**: `pharos-proto`'s `InspectorPanel.irisx` can now move its ~90 lines
of hand-written per-frame row-sync C++ (`inspector_panel_native.cpp`'s `panel.sync`) into a
real `OnTick` declared directly in the `.irisx` file, reaching its own `ref`'d
`Label`/`IconWidget`/`Box` rows via `GetRef(name)` — once `pharos-proto` also passes a real
`nyx::host::NyxRuntime*` (its own `IrisNyxDriver::Runtime()`) as `BuildContext::NyxHost`.
That wiring, and the actual `InspectorPanel.irisx` migration itself, is `pharos-proto`'s own
follow-up — nothing further needed here.

The original ask, kept for context on what was actually requested:

## (resolved, see above) a Nyx-authored `OnMount`/`OnTick` has no way to reach its own component's `ref`'d widgets

**Not this repo's idea — a cross-repo ask from `pharos-proto`.** Its `pharos_nyx_bootstrap`
app is mid-way through eliminating its own hand-written `*_native.cpp` orchestration
(`docs/nyx_native_app_goal.md` there: "the Nyx-native app should require no additional C++
files to work at all... all its logic belongs in Nyx"). `InspectorPanel.irisx` is the next
target: it should become a real invoked component (`<InspectorPanel />` in `App.irisx`,
already unblocked by this repo's own "swap a live real widget" entry above) whose per-frame
row-sync logic — currently ~90 lines of hand-written C++ in `inspector_panel_native.cpp`
(`panel.sync`, called every frame) that reads a selected node and writes `Text`/`IconName`/
`ColorText`/`SetIsVisible` onto 21 rows' worth of `ref`'d `Label`/`IconWidget`/`Box` widgets
— should instead live as an `OnTick` declared directly inside `InspectorPanel.irisx` itself,
using the already-landed `.irisx` lifecycle primitive (this repo's own "reconciler-side
wiring" entry below, `iris-proto`'s `iris::RegisterLifecycle`/`NyxLifecycleAdapter`).

**Confirmed, from real source in three repos, this doesn't work today — not a hunch:**

- `Umbra::IWidgetLifecycle::OnMount()`/`OnTick(const TickInfo&)` (`penumbra-proto`,
  `include/Umbra/IWidgetLifecycle.h`, and `Penumbra::IWidgetLifecycle` mirroring it) take no
  parameter that could carry a widget/ref handle — `OnTick`'s only payload is `DeltaSeconds`.
- `Iris::NyxLifecycleAdapter::HookInvoker` (`iris-proto`, `include/Iris/
  NyxLifecycleAdapter.h`) calls into Nyx by reconstructing `"OnTick(<args>)"` as literal
  source text (`NumericLiteralText` in `IrisNyxDriver.cpp`) and re-parsing it — only numeric
  literals can cross this boundary at all; there's no way to marshal an opaque host object
  (a ref lookup, a widget handle) through it even if `OnMount`/`OnTick` gained a parameter.
- **This repo's own `BuildContext` (`include/PenumbraUiBackend/Walker.h`) has zero field for
  reaching the Nyx side at all** — not `nyx::host::NyxRuntime`, not `Iris::IrisNyxDriver`,
  nothing. Confirmed by reading the whole struct: `FontBackend`/`ImageBackend`/`IconBackend`/
  `Focus`/`Clipboard`/`Style`/`StyleApplier`/`LifecycleHost`, and that's the complete list.
  So even though this repo is the one that owns `RefMap` (`Walker.h:45`) and is the one that
  actually triggers `OnMount()` (via `LifecycleHost->RegisterLifecycle(...)`, confirmed
  calling it internally per this repo's own lifecycle entry below), it currently has no way
  to hand anything back to the Nyx side that produced the component being built.

**The one fact that makes this tractable, not just aspirational — also confirmed against real
source, not assumed:** `BuildWidgetTreeInternal`'s existing per-node recording point (where
`RegisterLifecycleIfPresent` — and therefore `OnMount()` — already fires, `Walker.cpp:591-601`)
runs *after* `Built` is fully constructed, which for any non-leaf node means every descendant
(including every one of that subtree's own `ref`s) was already recursed into and recorded in
`*OutRefs` first. **By the time a component's `OnMount` fires, every `ref` belonging to that
component's own subtree is already sitting in `*OutRefs`.** This is a pure wiring gap, not a
sequencing one — nothing needs to be reordered, something just needs a way to reach the map
that's already correctly populated at exactly the right moment.

### Proposed shape (a grounded starting point, not a spec — real design questions below)

Mirror `LifecycleHost`'s own exact convention: a new optional `BuildContext` field (something
like `nyx::host::NyxRuntime* NyxHost{nullptr}` — this repo already transitively depends on
`nyx-proto` via `iris-proto`'s `Iris/Component.h`, so this is a new `#include` + field, not a
new dependency edge). When set, at the same point `RegisterLifecycleIfPresent` already fires,
register (or refresh) a Nyx-callable ref lookup — e.g. `GetRef(name: string)` returning a
small host-object handle with typed setters: `SetText(string)`, `SetIconName(string)`,
`SetColor(...)`, `SetVisible(bool)`, dispatched by `dynamic_cast`ing the looked-up
`WidgetBase*` to `Label`/`IconWidget`/`Box`. Those four operations aren't a guess — they're
exactly the full set `pharos-proto`'s own `inspector_panel_native.cpp` hand-writes today, so
they're a real, validated minimum bar, not a speculative API surface.

**One real open design question, not pre-decided here — worth resolving against real source
before implementing, not guessed:** should `GetRef` be registered once, globally, on the
driver's shared `Runtime_.Globals()` (capturing a stable pointer into the same live `RefMap`
that keeps growing as the walk proceeds — `std::unordered_map` element references stay valid
across insertions, so this is safe even though `*OutRefs` isn't finished growing yet when an
early component's `OnMount` fires), or does it need to be bound per-instance onto that
specific `ComponentInstance`'s own `NyxDriverState::RenderScope` Environment (`iris-proto`,
`IrisNyxDriver.h`)? The global-registration option looks sufficient at first read — `ref`
names are already unique across one whole `BuildWidgetTree` call (`RefMap` is one flat map),
and `iris-proto`'s `IrisNyxDriver::GetFileScope` now refreshes a cached file's own interpreter
from `Runtime_.Globals()` on every reuse (`efc6c1e`, `pharos-proto`'s recent UAF fix) — meaning
a freshly-registered `GetRef` reliably becomes visible to an already-mounted `.irisx` file's
own scope, which wasn't true before that fix landed. If that holds up under real
implementation, this may need **no `iris-proto` change at all** — but confirm it against real
source rather than assuming, since this repo doesn't currently touch `IrisNyxDriver` directly
anywhere (only `Iris::Component`/`ComponentInstance`), and this would be the first place it
did.

**What this explicitly is not**: `pharos-proto` hand-rolling this per-panel (another
`RegisterFunction("GetRef", ...)` written directly in its own `main.cpp` or
`inspector_panel_native.cpp`) would just move the C++ orchestration around, not eliminate it
— its own `docs/nyx_native_app_goal.md` says as much directly ("making the C++ orchestration
more correct is not progress toward this goal"). This needs to be a framework-owned,
`BuildContext`-opt-in capability here, the same way `LifecycleHost` itself already is, so
`InspectorPanel.irisx` (and eventually Explorer/Atlas/Toolbar too) gets it for free once
wired up, with zero per-panel C++ on the consuming app's side.

---

## Done (2026-08-17, third pass): swap a live real widget when a reconciled `<Native>` re-renders

**Not this repo's idea — a cross-repo ask from `pharos-proto`, and a direct follow-on to the
lifecycle wiring done earlier this session (previous entry below).** Matching ask filed in
`iris-proto/docs/next-steps.md` (same date) — that repo owns the trigger side (does a
re-rendering `<Slot>` even reach a `<Native>` leaf inside it and re-invoke its builder at all
today); this is this repo's own piece (once it does, or once it's made to, what does
`penumbra-ui-backend` need to do with the freshly-built widget).

**`iris-proto`'s side landed first (`aea7286`): `<Native>` already reaches this repo's own
`Component::NativeBuilder` correctly on a key change** — no reconciler change was needed there,
just three new tests proving it. That settled the open design question below (`Component::Key`
identity matching does already reach `<Native>` nodes), so this repo's own piece proceeded on
that basis.

**This repo's side: `PenumbraWidget::ReplaceRawWidget`** (`include/PenumbraUiBackend/
PenumbraWidgetAdapter.h`, `src/PenumbraUiBackend/PenumbraWidgetAdapter.cpp`) — swaps a wrapper's
live widget in place (same real parent, same slot) without losing the wrapper's own identity or
tree position. Three cases, each handled distinctly: a `Box` parent uses the existing
`Box::ReplaceChild`, handing the old widget back intact; a `SplitPanel` parent uses
`SetFirst`/`SetSecond` (no `ReplaceChild`-equivalent exists there today — the old widget is
destroyed inline, not handed back, a real upstream `penumbra` gap this stands in for — see the
method's own header comment); the mount root (`Parent_ == nullptr`) swaps `OwnedWidget_`
directly. `Tags`/`Refs` are re-seeded for the new subtree the same way `WrapExistingTree`'s
initial wrap already does.

**A real, live bug found and fixed along the way, not part of the original ask:**
`InsertChildAt`/`RemoveChildAt` only ever checked `dynamic_cast<Box*>`, which silently
*succeeds* for a `SplitPanel` (`SplitPanel : Box`) and would have written into its
inherited-but-unused `Box::Children` vector instead of its real First/Second slots — the widget
would have been genuinely invisible and unreachable, not just misplaced. Both now check for
`SplitPanel` first.

**Verified: full rebuild clean, full test suite 0 failures**, including 5 new tests covering all
three `ReplaceRawWidget` cases plus the `InsertChildAt`/`RemoveChildAt` `SplitPanel` fix
(`TestRemoveThenInsertOnASplitPanelParentSwapsTheFirstPaneWithoutTouchingBoxChildren`,
`TestReplaceRawWidgetOnABoxParentHandsBackTheOldWidgetIntact`,
`TestReplaceRawWidgetOnASplitPanelParentSwapsTheFirstPane`/`...SecondPane`,
`TestReplaceRawWidgetOnTheMountRootSwapsItsOwnWidget`,
`TestReplaceRawWidgetSeedsTagsAndRefsForTheNewSubtree`).

**What unblocks:** `pharos-proto`'s `ExplorerPanel`/`AtlasPanel`/`InspectorPanel` can now become
real invoked Iris components (`<ExplorerPanel />` in `App.irisx`) instead of `<Native
build={...} />` splices, with rebuild-on-data-change driven by the reconciler through this
method rather than hand-rolled teardown/rebuild in `pharos-proto`'s own `main.cpp`. That
conversion is `pharos-proto`'s own follow-up, not this repo's.

The original write-up, kept for context on what was actually asked:

**The concrete pain:** `pharos-proto`'s `pharos_nyx_bootstrap` app has four panels spliced into
`App.irisx` via `<Native build={...} />`. Reloading a fixture or switching lens needs three of
them torn down and rebuilt against new data — today done entirely by hand in `pharos-proto`'s
own `nyx_app/main.cpp` (`loadFixtureFromPath()`/`switchLens()`): detach the live `SplitPanel`
children (`GRootSplit->SetFirst(nullptr)`), reset/rebuild the C++ panel objects, re-splice the
new widgets in (`GRootSplit->SetFirst(std::move(...))`). That's a reconciler's job, not an app's.

**Grounding, from this repo's own real source:** `BuildWidgetTree` (`include/PenumbraUiBackend/
Walker.h:84-86`) is explicit today: "Stage 2 only: a one-shot tree build, no diffing, no
identity tracking... matching by key across two trees is Stage 3's reconciler's job." `BuildNative`
(`src/PenumbraUiBackend/Walker.cpp`, confirmed earlier this session at lines 450-459) calls
`Component::NativeBuilder->Build()` exactly once, `DetachOwnership()`s the resulting `WidgetBase`
from its `PenumbraWidget` wrapper, and hands it off — nothing about that call site is set up to
be invoked a second time for the same live tree position, nor to know what to do with a second
result if it were (there's no "replace this already-mounted child" operation anywhere in this
repo today; callers like `SplitPanel::SetFirst`/`SetSecond` are the closest primitive, and
they're not driven by anything here automatically).

### What's needed, once `iris-proto`'s side answers whether/how `<Native>` re-invocation happens

A way for this repo to be told "this `<Native>` node's builder just produced a new widget for
a position that already has a live one mounted," and swap it in — presumably threading through
whatever real Penumbra parent widget owns that position (a `SplitPanel`, in `pharos-proto`'s own
case) via its existing `SetFirst`/`SetSecond`-style API, but driven automatically by this repo's
own reconciliation pass rather than by the consuming app calling it by hand. Exact shape is an
open design question here, not pre-decided by this entry — depends heavily on what `iris-proto`
finds about whether `Component::Key`-based identity matching already reaches `<Native>` nodes.

### What unblocks

Once both halves land, a component like `pharos-proto`'s `ExplorerPanel` can own its own
"rebuild when the underlying data changes" behavior entirely — the actual target of that repo's
own Phase 3 write-up (`pharos-proto/docs/next_steps.md`), which today still has to hand-roll
teardown/rebuild/re-splice in C++ because this capability doesn't exist yet.

---

## Done this session (2026-08-17, second pass): reconciler-side wiring for a framework-owned component lifecycle system

**Not this repo's idea — a cross-repo ask from `pharos-proto`,** wanting
`pharos_nyx_bootstrap`'s app-level `OnUpdate` (hand-sequencing ~15 named host calls every
frame) eventually replaced by each mounted component owning its own update logic, run
automatically by the framework. Matching asks were filed in `iris-proto`/`nyx-proto`'s
own docs; this was this repo's own piece — the "reconciler" layer that sees both a live
`Component`/`ComponentInstance` (Iris side) and a live `Penumbra::Application*`
(Penumbra side) at the same time.

`iris-proto` landed its half first (`Umbra::IWidgetLifecycle* Lifecycle`, a passive
non-owning field on `ComponentInstance`; `iris::RegisterLifecycle(...)`, an
ambient-current-instance free function mirroring `IRIS_SIGNAL`'s `DeclareSignal`,
`Iris/ComponentInstance.h`) — `vendor/iris` pin bumped `ad3b6b6` → `1349183`
("Implement iris::RegisterLifecycle for framework-owned per-component update"). No
nested-submodule re-init needed (`libs/amanuensis`/`libs/cimmerian`/`libs/
umbra-interfaces` all already at the right commit).

Two real wrinkles found while implementing against the landed API, neither guessed in
advance:

1. **`Component::Instance` isn't root-only.** `MountComponentInstance` wraps *every*
   component invocation `Codegen.h` emits for a `<Name .../>` call, not just the
   outermost mount root — a plain nested `<ChildComponent .../>` used as an ordinary
   static child (no `<Slot>` involved) gets its own `Instance` inline in the same
   `Component` tree one `BuildWidgetTree` call recurses through. Registration therefore
   happens at the same per-node point `PrimitiveTagMap`/`RefMap` already record at
   (`BuildWidgetTreeInternal`, `Walker.cpp`), not once at `BuildWidgetTree`'s outer
   entry — confirmed with a dedicated test (`TestNestedNonSlotComponentInvocationAlso
   RegistersItsOwnLifecycle`, see below).
2. **`Umbra::IWidgetLifecycle` and `Penumbra::IWidgetLifecycle` are two distinct
   classes**, not one type shared across namespaces — identical virtual signatures,
   deliberately mirrored (`Penumbra::IWidgetLifecycle`'s own doc comment), no
   inheritance relationship. `Penumbra::Application::RegisterLifecycle`/
   `UnregisterLifecycle` want the `Penumbra::` one specifically, so a small local
   `UmbraLifecycleBridge` (`Walker.cpp`, anonymous namespace) forwards each call —
   this repo's normal bridging job, not a framework gap on either side.

**Implemented** (`include/PenumbraUiBackend/Walker.h`, `src/PenumbraUiBackend/
Walker.cpp`):

- `BuildContext` gained `Penumbra::Application* LifecycleHost{nullptr}` — same
  optional-resource convention every other field already follows (null skips
  registration entirely, exactly pre-wiring behavior).
- `BuildWidgetTreeInternal`'s existing per-node recording point (right where
  `OutTags`/`OutRefs` get populated) gained a call to `RegisterLifecycleIfPresent`:
  when `Context.LifecycleHost && Node.Instance && Node.Instance->Lifecycle` are all
  non-null, it wraps the pointer in an `UmbraLifecycleBridge`, calls
  `LifecycleHost->RegisterLifecycle(...)` (which already calls `OnMount()` internally,
  `Application.cpp`), and sets `Built->OnDestroyed` — `WidgetBase`'s existing, generic
  "this widget is being torn down" hook (`WidgetBase.h:65`), unused anywhere in this
  repo before now — to unregister (`UnregisterLifecycle` already calls `OnUnmount()`
  internally) when the real widget is actually destroyed. No new member storage
  anywhere: the `OnDestroyed` lambda's own capture (a `shared_ptr`, not `unique_ptr` —
  `std::function` needs a copyable target, same wrinkle
  `TestNativeUnwrapsAPenumbraWidgetToItsRealWidgetBase` already worked around) owns the
  bridge for exactly as long as it needs to live. This means `ComponentInstance` never
  needed a destructor or a registry reference of its own — `iris-proto`'s own decision,
  confirmed to compose cleanly with this repo's side.

New regression coverage in `tests/WalkerTests.cpp`:
`TestLifecycleRegistersOnBuildWhenLifecycleHostIsSet`,
`TestLifecycleUnregistersWhenTheBuiltWidgetIsDestroyed`,
`TestNoLifecycleHostMeansNoRegistrationEvenWithALiveInstance`,
`TestNoComponentInstanceMeansNoRegistrationEvenWithALifecycleHost`,
`TestNestedNonSlotComponentInvocationAlsoRegistersItsOwnLifecycle`. Full build +
`penumbra_ui_backend_tests` (183 assertions, 0 failures) clean.

**What this unblocks**: together with `iris-proto`'s landed half, a mounted `.irisx`
component's lifecycle hooks now get registered/unregistered automatically as part of
the normal build/mount/unmount flow, once a consumer passes a real
`Penumbra::Application*` as `BuildContext::LifecycleHost` — no hand-written
`Build*IfNeeded`/teardown sequencing needed on the consuming app's side. Wiring an
actual `BuildContext::LifecycleHost` through to a real running app (e.g.
`pharos_nyx_bootstrap`'s `IrisNyxDriver`-consuming mount code) is that consumer's own
follow-up, not this repo's — nothing further needed here for this ask.

---

## Session note (2026-08-17): stood by, no work started, stopped for fleet retirement

This session was part of a multi-repo coordination experiment (pharos-proto as
coordinator "main", with penumbra-proto/iris-proto/lustre/nyx-proto agents alongside
this repo's own agent). At session start: read this doc and `README.md`, confirmed
`git status` clean on `main`, up to date with `origin/main`, HEAD `24e3458` ("Wire
justify-content through StyleApplier onto Box::JustifyContentMode") — matching the
"Read first" section below, which already stated no open cross-repo asks or undecided
questions remained. Sent a standby confirmation to "main" and received the peer-agent
address roster, but no ask was ever queued for this repo during the session — nothing
was implemented, nothing was built or tested, and the working tree was never touched
beyond this note.

Stopped on explicit instruction from "main": this experimental fleet is being retired
so a fresh one can be spun up cleanly. Nothing was left in-flight. A fresh session can
treat the "Read first" section below as still fully current — it was true at both the
start and end of this session.

## Done this session (2026-08-10, second pass): `justify-content` wired through `StyleApplier`

`pharos-proto` asked directly for the main-axis-distribution gap `penumbra`'s own
`docs/next_steps.md` cross-referenced (`Box::JustifyContentMode`, "What this unblocks")
to be resolved. `penumbra` (bumped `43cd669` → `e449d11`, "Add Box::JustifyContentMode:
main-axis space-distribution for stack layouts") landed the `Box`-algorithm half; `lustre`
already shipped the CSS-parsing half (`Lustre::Justify`/`ResolvedStyle::JustifyContent`,
2026-08-03) — the only piece missing here was the `StyleApplier` mapping between them.

`StyleApplier.cpp`'s `ApplyLayout()` gained a `ToPenumbraJustify` conversion (mirrors
`ToPenumbraCrossAlign` exactly) and a `Style.JustifyContent` branch writing into
`Box::JustifyContentMode`, same "optional field, present only when the resolved style
actually set it" convention every other property in this function already follows.

New regression coverage in `tests/LustreStyleApplierTests.cpp`:
`TestJustifyContentReachesBox` (`justify-content: space-between` reaches
`Box::JustifyContentMode`) and `TestNoJustifyContentSetLeavesItUntouched` (absence leaves
whatever the widget already had, same "never clobber an unset property" contract
`TestNoDisplaySetLeavesLayoutUntouched` already established for `display`). Full build +
`penumbra_ui_backend_tests` (174 assertions, up from 163 — this pass's two plus whatever
else landed since the count was last written down here, 0 failures) clean.

**What this unblocks**: `pharos-proto`'s `ThreeZoneRow` (`src/ui/layout_helpers.h`) can now
be replaced by an ordinary `<Frame class="...">` with `display: stack; flex-direction: row;
justify-content: space-between;` — the last of the two structural blockers on
`DropdownTrigger`'s `<Native>` migration (the `ViewportWidget` half already closed via
`AtlasContentStrip.iris`/`FixedLeadingStack`). That migration itself is `pharos-proto`'s
own follow-up, nothing further needed here.

## Done this session (2026-08-10): closed the `demo/` visual-check follow-up from the color/font inheritance change

The 2026-08-03 (second pass) inheritance entry below flagged one un-auto-fixed follow-up:
"this repo's own `demo/` should get a visual check" for the behavior change (an unstyled
leaf now inherits `color`/`font` from an ancestor instead of staying at its own default).
That check hadn't actually happened — `demo/main.cpp`'s own stylesheet set `color:
#FFFFFF` directly on `.bar-label`, so the running demo never exercised inheritance at all,
despite looking like a plausible place it would.

Verified two ways. First, ran the demo as committed (`.claude/skills/
run-penumbra-ui-backend`) for a baseline screenshot — correct, unregressed, but not a real
test of inheritance. Then moved `color: #FFFFFF` off `.bar-label` and onto the outer
`.health-bar` ancestor, rebuilt, and re-ran: the label text still rendered white, now
purely via inheritance through two nested `Frame` levels, at both wiring points this demo
targets — mount time (`Walker.cpp`'s build-time resolve) and reconcile time
(`PenumbraWidgetAdapter::ApplyPropDiff` re-resolving after the `.bar-normal`/
`.bar-critical` class toggle). No regression.

Made permanent (commit `c2874a8`, `demo/main.cpp`): `.health-bar` now carries `color:
#FFFFFF`, `.bar-label`'s rule (which had nothing else in it) was deleted outright, and a
new comment on the stylesheet explains why the label is deliberately left without its own
color rule. `penumbra_ui_backend_tests` (69 assertions, 0 failures) clean on the rebuilt
binary.

**What this closes**: the one action item the inheritance entry below left open for this
repo specifically. The `pharos-proto` heads-up in that same entry is still that repo's own
follow-up — nothing further to do on that front from here.

## Done this session (2026-08-03, third pass): sized (not designed) a real component-logic hot-reload gap in `iris`

A conversation about eventually plugging Lustre/Iris into a genuinely interpreted host
language (source re-executed live, Penumbra updating the drawing as it changes) surfaced
that `iris`'s own docs had already flagged this twice (`next-steps.md`'s "Live-widget root
registry, for Lustre's hot-reload" entry, `lustre_handoff.md` §3) without ever sizing it
against the real code. Checked directly: `vendor/iris/docs/iris_interpreted_host_hot_reload_gap.md`
(new, commit `ad3b6b6`, pushed) — three concrete blockers. `ComponentInstance`/`Signal` have
no identity across two runs of the same component (state would reset to `InitExpr` on every
reload); the reconciler's only entry point needs an owning widget pointer plus the exact
prior `Component` IR tree, neither of which exists outside a `SlotState`, so "diff a fresh
tree against whatever's live" doesn't mechanically work today even with `RegisterRoot`/
`GetRoot` in place; and Nyx (Iris's own planned scripting host), as currently specced, is
still a preprocessed-then-compiled host, not the re-executed-live semantics a genuinely
interpreted language implies. `vendor/iris` bumped `37fbcc3` → `ad3b6b6` to pick up the doc.
No code changed in `iris` or here — gap-only, deliberately not designed, per this
ecosystem's usual convention for something this size.

## Done this session (2026-08-03, second pass): CSS-style `color`/`font` inheritance

A follow-up question on the `<Split>` handle-color work below ("can the handle color be
controlled independently from a Label's text color?") surfaced that Lustre had no property
inheritance at all — `.card .card-title`-style nesting only ever affects *selector
matching*, never propagates an already-resolved ancestor's value down to an unset
descendant. Real CSS inherits `color`/`font` by default; implemented properly in `lustre`
itself (`https://github.com/DeanWilsonDev/lustre`, commit `7779d94`, pushed to `main`) —
new `Lustre::ResolveStyle()`, which also absorbs the two-layer cascade composition this
repo used to hand-roll itself (`StyleResolution.cpp`'s old `MergeInto` + two `Resolver::
Resolve()` calls), since inheritance needs each ancestor's fully-cascaded value and only
that composition ever produces one. `Resolver::Resolve()` itself untouched, no breaking
change. Full decision record, including the one non-obvious call (inheritance crosses
component boundaries, unlike descendant-selector matching) in
`docs/lustre_style_inheritance_decision.md`.

`vendor/lustre` pin bumped `0d8bfa5` → `7779d94`. `StyleResolution.cpp` simplified to a
thin pass-through (old `MergeInto` deleted, moved to `lustre` as canonical). New coverage:
`vendor/lustre`'s own `tests/InheritanceTests.cpp` (10 cases) plus two end-to-end cases in
this repo's `tests/StyleWiringTests.cpp` proving it through a real `BuildWidgetTree` call
(an `<Icon>` with no class inherits `color` from an ancestor `<Frame class="row">`;
`background-color` does not leak the same way). Full build + `penumbra_ui_backend_tests`
(0 failures) + `test_lustre` (41 passed, up from 29) + `test_iris` (138 passed) clean.

**What this unblocks**: any `pharos-proto` `.lustre` file can now set `color`/`font-family`/
`font-size` once on a wrapping element and have it reach unstyled `<Label>`/`<Icon>`/
`<Split>` descendants automatically, instead of needing an explicit rule on every leaf —
matching real CSS. **Blast radius, not auto-fixed**: this is a real behavior change for any
existing `.lustre` file relying on today's "unstyled leaf stays at its own default" —
flagged in the decision doc; this repo's own `demo/` should get a visual check, and
`pharos-proto` gets a heads-up since it vendors the same `lustre` pin.

## Done this session (2026-08-03): closed both open follow-ons from the `<Native>`/`<Split>` wiring gap

`docs/native_split_backend_wiring_gap.md`'s two "Explicitly not requested" items were the
last genuinely open work in this repo (everything else in this doc's history was already
marked Implemented). Both are now closed:

1. **`<Native>`-wrapped widget Lustre-styling semantics** — decided, no code change:
   `Walker.cpp`'s post-build Lustre-apply step already runs unconditionally for every
   built widget including `<Native>` ones, and that's kept as the intended behavior
   (consistency with every other tag, and it's the `class="..."` hook `iris`'s own
   proposed-API sketch wanted). Full reasoning in the new
   `docs/native_lustre_styling_decision.md`.
2. **`SplitPanelStyle`'s handle-color fields** (`ColorHandle`/`ColorHandleHovered`/
   `ColorHandleDragged`) — implemented. First-instinct approach was a new `handle-color`
   Lustre property; caught in review as exactly the kind of component-specific property
   this codebase has deliberately avoided elsewhere (`background-color`/`border-color`/
   `color` cover every case so far, including `<Icon>`'s color reusing plain `color` — see
   the 2026-07-23 second-pass entry below). A split handle is a foreground-ish decorative
   bar, not a second background or a border, so it reuses `color`/`TextColor` instead —
   **no `vendor/lustre` change needed at all**, since that property and its resolver
   support already existed.

   `StyleApplier.cpp`'s `Apply()` gained a `dynamic_cast<SplitPanel*>` branch (after the
   generic `AsBox` path, since `SplitPanel : Box`) resolving `Style.TextColor`/
   `Style.Hover->TextColor`/`Style.Active->TextColor` into the three handle-color fields
   via `SplitPanel::ApplyStyle`. Two wrinkles: `ApplyStyle` assigns the whole inherited
   `BoxStyle` slice wholesale rather than field-by-field, so the wiring seeds a local
   `SplitPanelStyle` from `AsBox->Style` and the widget's own current handle colors first
   to avoid clobbering either; and `ColorHandle`/`ColorHandleHovered`/`ColorHandleDragged`
   had no accessor at all (private, `Draw()`-only), so `vendor/penumbra`'s `SplitPanel.h`
   picked up three small read-only getters (commit `4a45581`, pushed to `main` on its
   GitHub remote — a clean fast-forward, `origin/main` was still at this repo's own prior
   pin).

New regression coverage in `tests/LustreStyleApplierTests.cpp`: `TestColorReachesASplitPanelHandle`,
`TestHoverAndActiveColorOverlaysReachASplitPanelHandle`,
`TestNoColorOverlayLeavesSplitPanelHandleAtDefaultAndKeepsBoxStyle`. Full build +
`penumbra_ui_backend_tests` (0 failures) + `test_lustre` (29 passed) + `test_iris` (138
passed) clean.

**What this unblocks**: `pharos-proto`'s root-layout `SplitPanel` (`src/main.cpp`) can now
express handle hover/drag feedback as a `.lustre` `color`/`:hover`/`:active` rule on its
`<Split>`, same as it would for any other widget's foreground color — the last of the two
follow-on gaps this doc's own history had left open. That styling is `pharos-proto`'s own
follow-up, nothing further needed here.


## Done this session (2026-07-23, third pass): shared `.lustre`-file-loading helper

Implemented the `LoadStylesheetFromFile` helper this doc had specced out (previous
revision, below the fold in git history): `PenumbraUiBackend/Lustre/StylesheetLoader.h`
+ `.cpp`, added to the existing `penumbra_ui_backend_lustre` target (no new CMake
target — same rationale as `StyleApplier`/`StyleResolution` already sharing it, this
is more of the same "Lustre bridge" surface). Does exactly the `ifstream`/
`ostringstream`/named-`std::string`/`Lustre::Parser::Parse()` sequence every consumer
this doc surveyed (`lens_toggle.cpp:40-67` et al.) hand-rolled, logging to stderr
(each line prefixed with the caller-supplied `LabelForErrors`) on either an unopenable
file or a parse error.

One correction versus the original spec: `Parser::Parse()` (`vendor/lustre/src/Lustre/
Parser.cpp`) always populates `ParseResult::Sheet`, even when `Errors` is non-empty —
it hands back the (possibly partial) tree regardless, so a caller can report every
error at once rather than stop at the first. The spec's `result.Sheet.has_value()`
check (copied from the hand-rolled call sites, which all had the same latent
assumption) is therefore not what actually gates the empty-`Stylesheet{}` fallback in
practice; only the can't-open-file path reliably hits it. Documented this on both the
header and the implementation rather than silently diverging from the ask, and added
`TestFileWithParseErrorsStillReturnsWithoutCrashing` to `tests/
StylesheetLoaderTests.cpp` to pin the actual (not assumed) behavior down.

Also `TestValidFileParsesIntoAPopulatedStylesheet`, `TestMissingFileReturnsAnEmptyStylesheet`.
New test file wired into `CMakeLists.txt`'s `penumbra_ui_backend_tests` target and
`tests/WalkerTests.cpp`'s `main()`. Full build + `penumbra_ui_backend_tests` (0
failures) + `test_lustre` (27 passed) + `test_iris` (124 passed) clean.

**What this unblocks**: `pharos-proto`'s six call sites (`lens_toggle.cpp`,
`json_path_field.cpp`, `load_button.cpp`, `color_filter_dropdown.cpp` ×2,
`inspector_panel.cpp` ×2) can each replace their ~25-line hand-rolled loader with one
call to `PenumbraUiBackend::Lustre::LoadStylesheetFromFile`. That migration is
`pharos-proto`'s own follow-up, nothing further needed here.

## Done this session (2026-07-23, fourth pass): `GetByRef` lookup on a mounted tree

`iris` landed its half (pin `c92388b` → `7830955`, "Add ref prop and iris_compile_directory
CMake helper"): `Component`/`ElementNode` each gained a `Ref` field paralleling `Key` exactly
(`std::optional<IrisPropValue>`), threaded through parsing/codegen/validation but deliberately
kept out of the reconciler's identity matching (`Reconciler.cpp` stays `Key`-only — `ref`
carries no reconciler meaning). No nested-submodule re-init needed this time (`libs/amanuensis`/
`libs/cimmerian`/`libs/umbra-interfaces` all unchanged). `iris`'s own doc explicitly left the
mount-result registry lookup as this repo's follow-up, not touching `MountFn`'s return type
(`Iris/SlotRuntime.h`'s `MountFn` still returns a plain `std::unique_ptr<Umbra::IWidget>`) — so
the lookup below is exposed as a `PenumbraWidget`-specific method, the same way
`GetPrimitiveTag()` already is, rather than a new `iris::MountResult` type.

Implemented the "collect ref-tagged nodes during the recursive walk" half this doc had
specced out:

1. **`Walker.h`/`Walker.cpp`**: new `RefMap` type (`unordered_map<std::string,
   WidgetBase*>`, ref name -> built widget), mirroring `PrimitiveTagMap`'s own "optional
   out-param, populated at the one point every built widget funnels through" convention
   exactly. `BuildWidgetTree` gained a trailing `RefMap* OutRefs = nullptr` parameter,
   threaded through every internal helper (`BuildWidgetTreeInternal`,
   `BuildAndAttachChildren`, `BuildFrame`/`BuildGrid`/`BuildInline`/`BuildScroll`) right
   alongside `OutTags`, since it needs to reach the same recursion point.
2. **`PenumbraWidgetAdapter.h`/`.cpp`**: `WrapExistingTree` gained a trailing `const
   RefMap* Refs = nullptr` parameter. Since `RefMap` is keyed by ref name but the wrap
   walk matches by raw `WidgetBase*` pointer as it constructs each wrapper, it inverts
   `Refs` into a local `WidgetBase* -> name` map once, then threads that (plus a
   `RegistryRoot` pointer) through `AdoptChildrenFromRawTree`'s existing recursion. The
   resulting `ref name -> PenumbraWidget*` registry lives only on the mount's root
   wrapper (`Parent_ == nullptr`) — the new `PenumbraWidget::GetByRef(std::string_view)`
   method walks up to that root via the existing `Parent_` chain first, so it's callable
   from any wrapper in the tree, not just the one `MakeMountFn` hands back. `MakeMountFn`
   now builds a `RefMap` alongside its existing `PrimitiveTagMap` and threads both
   through.

New regression coverage: `tests/WalkerTests.cpp` (`TestRefTaggedNodeIsRecordedInOutRefs`,
`TestNoRefLeavesOutRefsEmpty`) and `tests/PenumbraWidgetAdapterTests.cpp`
(`TestGetByRefFindsARefTaggedDescendant`, `TestGetByRefOnAnUnknownNameReturnsNull`,
`TestGetByRefIsCallableFromANonRootWrapper`). Full build + `penumbra_ui_backend_tests` (0
failures) + `test_lustre` (27 passed) + `test_iris` (129 passed, up from 124 — `iris`'s own
new `ref`-prop tests) clean.

**What this unblocks**: `pharos-proto`'s `lens_toggle.cpp:108-122` and
`inspector_panel.cpp`'s `buildInspectorRow` (116-177) can each replace their hand-walked
`GetChildAt(index)` chains — kept in sync with the `.iris` file's child order only by
convention, with no compiler error on drift — with `ref="..."` in the `.iris` file plus one
`GetByRef("...")` call on the mounted wrapper. That migration is `pharos-proto`'s own
follow-up, nothing further needed here.

## Done this session (2026-07-23, second pass): `<Icon>` color wired through `StyleApplier`

Bumped the `vendor/penumbra` pin `7fad4dc` → `2f21c55` ("Add IconColor param
to IIconBackend::DrawIcon, thread through IconWidget"), which added exactly
the fields this needed: `IconWidget::ColorLogical`/`ColorLogicalHovered`/
`ColorLogicalPressed`/`ColorLogicalDisabled`, resolved at draw time via
`ColorForState()` (`vendor/penumbra/src/Penumbra/Widgets/IconWidget.cpp`),
same fallback convention `Box::BorderForState()` uses for `border-color`.

**Landed in `StyleApplier.cpp`'s `Apply()` only — no `Walker.cpp` change,
and no new Lustre property.** `IconWidget` isn't a `Box` (same as
`ImageWidget`), so its new `dynamic_cast<IconWidget*>(&Widget)` branch has
to run *before* the existing `if (!AsBox) return;` early exit, not after it
alongside the `Label` branch. And rather than adding a dedicated
`icon-color`/`color` Iris prop, it reuses the same `color` Lustre property
`Label`'s `ColorText` branch already resolves from `Style.TextColor` —
`Style.Hover`/`Active`/`Disabled` are full recursive `ResolvedStyle` blocks,
so their own `TextColor` already carries the per-state overlay with no new
field needed anywhere. This is the CSS `color` (`currentColor`) convention:
one cascading foreground-color property covers both text and icons, not two
parallel ones.

New regression coverage in `tests/LustreStyleApplierTests.cpp`:
`TestColorReachesAnIconWidget`, `TestHoverActiveAndDisabledColorOverlaysReachAnIconWidget`,
`TestNoColorOverlayLeavesIconWidgetPerStateFieldsAtDefault` — plus a
one-line signature fix in `tests/WalkerTests.cpp`'s `FakeIconBackend` for
`IIconBackend::DrawIcon`'s new `Render::Color` parameter. Full build +
`penumbra_ui_backend_tests` (0 failures) + `test_iris` (124 passed) clean.

**What this unblocks**: `pharos-proto`'s `ColorFilterDropdown`'s
`DropdownTrigger`/`DropdownMenuRow` (`pharos-proto/src/ui/
color_filter_dropdown.cpp`) can now express their state-dependent icon
color as a `.lustre` `color`/`:hover`/`:active`/`:disabled` rule via
`<Icon>` instead of a hand-rolled `DrawContent` override — the last blocker
on those two widgets' `.iris`/`.lustre` migration. That migration is
`pharos-proto`'s own follow-up, nothing further needed here.

## Done previous session (2026-07-23): `border-color`'s `:hover`/`:active`/`:disabled` wiring in `StyleApplier`

Bumped the `vendor/penumbra` pin `89216b4` → `7fad4dc` ("Add
ColorBorder*/BorderForState and WidgetBase::OnDestroyed") to pick up
`BoxStyle::ColorBorderHovered`/`ColorBorderPressed`/`ColorBorderDisabled`
and `Box::BorderForState()`. No nested-submodule-init step was needed this
time — `penumbra` itself has no submodules, unlike the `lustre`/`iris`
bumps logged in earlier sessions below.

`StyleApplier.cpp`'s `Apply()` now has a per-state `border-color` block
right next to the existing `background-color` one, mirroring it exactly:
`Style.Hover->BorderColor` → `ColorBorderHovered`, `Style.Active->BorderColor`
→ `ColorBorderPressed`, `Style.Disabled->BorderColor` → `ColorBorderDisabled`
(same `ToPenumbraColor`/optional-presence convention, and — unlike the
gradient overlays — this one does get a `:disabled` variant, since
`ColorBorderDisabled` already existed as a flat color, not a gradient).
Checked `StyleResolution.cpp`'s `MergeInto`: no gap there — pseudo-class
overlays merge via a recursive `MergeOverlayBlock` call over the whole
`Hover`/`Active`/`Disabled` sub-struct, so `BorderColor` was already
covered as soon as the top-level field's `MergeInto` line existed (it did,
from the flat `border-color` property).

New regression coverage in `tests/LustreStyleApplierTests.cpp`:
`TestHoverActiveAndDisabledBorderColorOverlaysReachBoxStyle` (all three
overlays reach a plain `Box::Style`) and
`TestNoBorderColorOverlayLeavesPerStateBorderFieldsAtDefault` (absence
leaves the per-state fields at zero-alpha). Full build + `penumbra_ui_backend_tests`
clean (0 failures).

**What this unblocks**: `pharos-proto`'s `ColorFilterDropdown`'s
`DropdownTrigger` (`pharos-proto/src/ui/color_filter_dropdown.cpp`) can now
express its hover/open border-color swap as `:hover { border-color: ... }`
in a `.lustre` file instead of doing it by hand each frame — the last
blocker on that widget's `.iris`/`.lustre` migration. That migration is
`pharos-proto`'s own follow-up, nothing further needed here.

## Done previous session (2026-07-22): `<Input>`'s `onTextChange` wired end to end

`vendor/iris` bumped `42cc09c` → `c92388b` ("Add `<Input>`'s onTextChange event and
a live-widget root registry"), which itself needed `vendor/iris/libs/umbra-interfaces`
re-initialized (`git submodule update --init --recursive` from inside `vendor/iris`) to
pick up `Umbra::IrisPropDiff::OnTextChange` — a plain top-level `submodule update
--init --recursive` alone doesn't follow a nested submodule bump, same lesson the
previous `lustre`/`penumbra` bump already logged.

With the pin current, both build-time and reconciler-side wiring landed:

1. **Static build** (`Walker.cpp`): added `GetStringEventProp`, a sibling to
   `GetEventProp` that extracts `IrisPropValue`'s `std::function<void(std::string)>`
   alternative (`std::get_if` keyed on that exact variant member — the existing
   zero-arg `GetEventProp` can't reach it). `BuildInput` now calls it for
   `"onTextChange"` and assigns into `TextInput::OnTextChanged`, wrapped in a small
   lambda to adapt the by-value `std::string` callback to `OnTextChanged`'s
   `const std::string&` signature.
2. **Reconciler-side update path** (`PenumbraWidgetAdapter.cpp`'s `ApplyPropDiff`):
   added a `Diff.OnTextChange` branch using the same `dynamic_cast<TextInput*>` guard
   pattern the existing `<Text>`-only `Diff.Text` and `<Image>`-only `Diff.Src`
   branches already use (since `OnTextChanged` lives on `TextInput` specifically, not
   on `WidgetBase` like the other five event props above it).

New regression coverage: `tests/WalkerTests.cpp`
(`TestInputOnTextChangeReachesTextInputOnTextChanged` — prop reaches
`TextInput::OnTextChanged` and invoking it calls back into the original handler) and
`tests/PenumbraWidgetAdapterTests.cpp`
(`TestApplyPropDiffOnTextChangeReachesRealTextInput` — same, via `ApplyPropDiff` against
a real mounted `TextInput`, not just the static build). Full build + both test
binaries (`penumbra_ui_backend_tests`, `test_iris`) clean — 0 failures.

**What this unblocks**: `pharos-proto`'s toolbar JSON-path field ("Toolbar's
`TextInput` root not componentized", `pharos-proto/docs/next_steps.md`) can now be
compiled from a `.iris`/`.lustre` pair instead of hand-built — swapping in `<Input>`
no longer regresses live typing, since `onTextChange` (not just the initial-value-only
`text` prop) now reaches the real widget both at first build and across reconciles.
That componentization is `pharos-proto`'s own follow-up, nothing further needed here.

## Done previous session (2026-07-21, third pass): per-state gradient + box-shadow wired end to end

`penumbra` (bumped to `89216b4`) and `lustre` (bumped to `db6d4db`) landed the
two primitives the previous pass was blocked on — `BoxStyle::GradientTopHovered/
GradientBottomHovered/GradientTopPressed/GradientBottomPressed` and
`BoxStyle::ShadowColor/ShadowBlurRadiusLogical` on the `penumbra` side,
`ResolvedStyle::ShadowColor/ShadowBlurRadiusLogical` plus `box-shadow: <color>
<length>` shorthand parsing on the `lustre` side. `.gitmodules`/pins bumped;
`vendor/lustre`'s own nested submodules (`libs/amanuensis`,
`libs/amanuensis/external/cimmerian`) needed `git submodule update --init
--recursive` run from inside `vendor/lustre` after the bump, since a plain
top-level `submodule update --init --recursive` from this repo's root
re-pins `vendor/penumbra`/`vendor/lustre` back to `.gitmodules`' old SHAs
instead of picking up the new ones — bump the submodule's own HEAD first,
*then* init its nested submodules from inside it.

All three items from the previous "incoming ask" are now wired:

1. **`MergeInto` bug (fixed previous pass, `StyleResolution.cpp`)**:
   `BackgroundGradientStart`/`BackgroundGradientEnd`/`MaxWidthLogical`/
   `TextOverflowMode` now merge field-by-field like every other property.
   This pass added `ShadowColor`/`ShadowBlurRadiusLogical` to the same list
   (new fields from the `lustre` bump above) so they don't develop the
   identical latent gap.
2. **Per-state gradient overlays** (`StyleApplier.cpp`'s `Apply()`): `:hover`/
   `:active` `background-gradient-start`/`-end` now write into
   `BoxStyle::GradientTopHovered`/`GradientBottomHovered`/`GradientTopPressed`/
   `GradientBottomPressed`, mirroring the existing `ColorBackgroundHovered`/
   `Pressed` overlay treatment. No `Disabled` variant, per the original ask
   (consumers fall back to a flat disabled color, not a disabled gradient).
3. **`box-shadow`** (`StyleApplier.cpp`'s `ApplyBoxStyle()`): resolved
   `ShadowColor`/`ShadowBlurRadiusLogical` now write into the matching
   `BoxStyle` fields, same pair-presence convention as the gradient fields.

New regression tests in `tests/LustreStyleApplierTests.cpp` (hover/active
gradient overlays reach `Box::Style`, absence leaves zero-alpha default;
box-shadow reaches `Box::Style`, absence leaves zero blur radius) and
`tests/StyleWiringTests.cpp` (gradient survives a global+component merge).
Full build + `penumbra_ui_backend_tests` clean (0 failures).

**What this unblocks**: `pharos-proto`'s `GradientButton::Draw()`
(`gradient_button.cpp:8-43`) is now exactly what a plain `Button` styled via
Lustre's `:hover`/`:active` blocks plus a `box-shadow` property already does.
Deleting `GradientButton` and re-styling its panels via `.lustre` is
`pharos-proto`'s own follow-up, not this repo's — nothing further needed
here.

## Read first

Everything logged in this repo's own `docs/*_gap.md`/`*_decision.md` files is now
**implemented** — no open cross-repo asks or undecided questions remain as of this
session. The docs below are worth reading before touching `StyleApplier.cpp`/`Walker.cpp`
again, as background/precedent, not as a to-do list:

- `docs/lustre_style_inheritance_decision.md` — `color`/`font` inheritance, implemented
  in `lustre` itself (not just this repo), why it crosses component boundaries, and the
  blast-radius flag for existing `.lustre` files (this repo's own `demo/` visually
  checked and updated to exercise it, 2026-08-10, see above; `pharos-proto`'s heads-up
  is still that repo's own follow-up).
- `docs/native_split_backend_wiring_gap.md` — `<Native>`/`<Split>` build cases
  (`BuildNative`/`BuildSplit` in `Walker.cpp`), plus both of its own follow-on questions
  (closed this session, see above).
- `docs/native_lustre_styling_decision.md` — why Lustre styling applies generically to
  `<Native>`-wrapped widgets, no opt-out.
- `docs/pseudo_class_plain_box_decision.md` — why `:hover`/`:active`/`:disabled`
  background-color applies to any `Box`, not just `Button`; the precedent this session's
  `<Split>` handle-color reuse of `color` (not a new property) also follows.
- `docs/build_context_style_mismatch_gap.md` — a previous real
  `pharos-proto`-triggered bug in this same `StyleApplier`/`Walker`
  pairing, same shape of "compiles clean, fails silently downstream" risk
  worth keeping in mind when touching this file.
