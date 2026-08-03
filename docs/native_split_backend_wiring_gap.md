# penumbra-ui-backend — `<Native>`/`<Split>` have no `Walker.cpp` build case yet

> **Status:** Implemented (commit `52f6f14`, "Wire `<Native>`/`<Split>` build cases into
> Walker.cpp"), per "Proposed fix" in both §1 and §2 below almost exactly as sketched —
> `BuildNative`/`BuildSplit` added, wired into `BuildWidgetTreeInternal`'s switch and
> `IrisTagToLustreTag`. `vendor/iris` bumped to `37fbcc3` (was still pinned to `bedaa15`,
> predating both tags, at the time this doc was originally written). 5 new
> `WalkerTests.cpp` cases; full `penumbra_ui_backend_tests` (163 assertions) and `iris`'s
> own `test_iris` (138/138) both pass, full top-level build clean. Both "Explicitly not
> requested" items below are now also resolved, in later sessions: the
> Lustre-styling-through-`<Native>` question by `docs/native_lustre_styling_decision.md`
> (documents the already-generic apply as intentional, no code change), and
> `SplitPanelStyle`'s handle-color fields by §2's own follow-up below.
> **Trigger:** `pharos-proto` asked whether it could proceed componentizing `TreeRow`
> (`src/ui/explorer_panel.cpp`), `DropdownTrigger`/`DropdownMenuRow`
> (`src/ui/color_filter_dropdown.cpp`), `ChevronSeparator` and the `ViewportWidget`-hosted
> treemap (`src/ui/atlas_panel.cpp`), and the app's root `SplitPanel`-based layout
> (`src/main.cpp`) — all five were blocked on `iris`-language gaps logged in `iris`'s own
> `docs/next-steps.md` on 2026-07-30. Checking this repo's actual current state (not just
> `iris`'s docs) found `<Native>`/`<Split>` landed upstream but with no backend-mapping
> half here yet — confirmed by reading `Walker.cpp` directly (`grep -n
> "IrisElementTag::" src/PenumbraUiBackend/Walker.cpp` returns only
> `Frame`/`Inline`/`Grid`/`Image`/`Icon`/`Text`/`Scroll`/`Input`), not assumed from the
> `iris`-side doc's own "required changes elsewhere" note.
> **Trigger:** `pharos-proto` asked whether it could proceed componentizing `TreeRow`
> (`src/ui/explorer_panel.cpp`), `DropdownTrigger`/`DropdownMenuRow`
> (`src/ui/color_filter_dropdown.cpp`), `ChevronSeparator` and the `ViewportWidget`-hosted
> treemap (`src/ui/atlas_panel.cpp`), and the app's root `SplitPanel`-based layout
> (`src/main.cpp`) — all five were blocked on `iris`-language gaps logged in `iris`'s own
> `docs/next-steps.md` on 2026-07-30. Checking this repo's actual current state (not just
> `iris`'s docs) found `<Native>`/`<Split>` landed upstream but with no backend-mapping
> half here yet — confirmed by reading `Walker.cpp` directly (`grep -n
> "IrisElementTag::" src/PenumbraUiBackend/Walker.cpp` returns only
> `Frame`/`Inline`/`Grid`/`Image`/`Icon`/`Text`/`Scroll`/`Input`), not assumed from the
> `iris`-side doc's own "required changes elsewhere" note.

---

## 0. Context

`BuildWidgetTreeInternal`'s switch (`src/PenumbraUiBackend/Walker.cpp:410-433`) has one
`case` per `IrisElementTag` value, each delegating to a small `BuildXxx` helper
(`BuildFrame`, `BuildScroll`, `BuildInput`, etc.) that constructs the matching real
Penumbra widget. `vendor/iris/include/Iris/IrisElementTag.h` (submodule now at `37fbcc3`,
picked up by `5ea27bf` + a fresh `git submodule update`) has two tags this switch has
never seen: `Native` and `Split`. Every other primitive this repo has wired
(`Icon`, `Scroll`, `Input`) followed the same shape when it landed — a real gap, not
speculative.

## 1. `<Native>` — needs to unwrap `Umbra::IWidget` back to a real `WidgetBase`

`Component::NativeBuilder` (`vendor/iris/include/Iris/Component.h:132-141`,
`Iris::MakeNativeBuilder`) is a `std::function<std::unique_ptr<Umbra::IWidget>()>` — it
returns the *backend-agnostic* interface type, not a `Penumbra::Widgets::WidgetBase`.
Every other `BuildXxx` helper in `Walker.cpp` returns `std::unique_ptr<WidgetBase>`
directly and funnels into `Box::Builder::child(std::move(ChildWidget))`
(`BuildAndAttachChildren`, `Walker.cpp:234-244`), which only accepts real Penumbra
widgets — so wiring `<Native>` isn't just "add a case," it needs a real
`Umbra::IWidget` → `WidgetBase` bridge that doesn't exist for any other tag yet, because
no other tag crosses that boundary.

`PenumbraWidget` (`include/PenumbraUiBackend/PenumbraWidgetAdapter.h:35-59`) already has
what's needed on the other side: `RawWidget()` (a non-owning `WidgetBase*` peek) and
`DetachOwnership()` (reclaims the real `unique_ptr<WidgetBase>`, transitioning the
wrapper to attached — exactly the state transition `Walker.cpp` needs before splicing the
result into a `Box::Builder::child()` call, which takes ownership itself).

### Proposed fix

```cpp
// src/PenumbraUiBackend/Walker.cpp — sketch, not exact
std::unique_ptr<WidgetBase> BuildNative(const Component& Node) {
    if (!Node.NativeBuilder) {
        return nullptr; // codegen already errors on a missing `build` prop; defensive only
    }
    std::unique_ptr<Umbra::IWidget> Handle = Node.NativeBuilder->Build();
    if (auto* AsPenumbraWidget = dynamic_cast<PenumbraWidget*>(Handle.get())) {
        return AsPenumbraWidget->DetachOwnership();
    }
    // A build{} escape hatch returning some other Umbra::IWidget implementation has no
    // real Penumbra widget to unwrap -- see "Explicitly not requested" below.
    return nullptr;
}
```

wired into the switch the same way every other tag is:

```cpp
case IrisElementTag::Native:
    Built = BuildNative(Node);
    break;
```

`IrisTagToLustreTag` (`Walker.cpp:158-170`) needs a matching `case IrisElementTag::Native:
return "Native";` too, for the same reason `Scroll`/`Input` are there — `<Native>` can
still carry a `class` for whatever styling the caller's own `build{}` lambda didn't
already apply directly to the widget it returned (Lustre resolution still runs against
`Built` after this switch, per `Walker.cpp:437-445` — unaffected by this change, since
that block is generic over any built widget). Whether pseudo-styling a `<Native>`-wrapped
widget through Lustre at all is actually useful (versus the wrapped widget's own
already-built styling) is a real open question **not resolved by this doc** — flagging it
for whoever implements this to decide with the `pharos-proto` consumer in mind (its own
`TreeRow`/`DropdownTrigger`/`ViewportWidget` already apply their own styling directly, so
Lustre re-resolving classes on top might be redundant, or might be exactly the
`class="explorer-row"` hook `iris`'s own proposed-API sketch showed).

### Required changes elsewhere

None in `iris` or `pharos-proto` beyond what `pharos-proto`'s own migration of
`TreeRow`/`DropdownTrigger`/`ChevronSeparator`/`ViewportWidget` to `<Native>` would need —
that's `pharos-proto`'s own follow-up once this lands, per this ecosystem's usual
convention.

## 2. `<Split>` — a `SetFirst`/`SetSecond` container, not a `Box::Builder::child()` one

`Penumbra::Widgets::SplitPanel` (`vendor/penumbra/include/Penumbra/Widgets/SplitPanel.h`)
has no `Builder` at all — unlike `Box`/`InlineContainer`, it's constructed directly
(`std::make_unique<SplitPanel>()`, the same "plain fields, not a Builder chain" treatment
`BuildScroll` already gives `ScrollablePanel`, `Walker.cpp:351-373`) and takes exactly two
children through `SetFirst`/`SetSecond`, not a generic `Children` vector — `GetChildCount`/
`GetChildAt` report through those two slots specially (`SplitPanel.h:39-41`). Codegen
already enforces exactly two children at compile time
(`vendor/iris/src/Iris/Codegen.cpp:44-47`, `<Split> requires exactly two children`), so
`Walker.cpp`'s build case can assume `Node.Children.size() == 2` rather than re-validating
it.

### Proposed fix

```cpp
// src/PenumbraUiBackend/Walker.cpp — sketch, not exact, mirrors BuildScroll's shape
std::unique_ptr<WidgetBase> BuildSplit(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement,
                                        PrimitiveTagMap* OutTags, RefMap* OutRefs, StyleMatchStats* Stats) {
    auto Built = std::make_unique<Penumbra::Widgets::SplitPanel>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto Axis = GetStringProp(Node.Props, "axis")) {
        Built->Axis = (*Axis == "vertical") ? Penumbra::Widgets::SplitAxis::Vertical
                                             : Penumbra::Widgets::SplitAxis::Horizontal;
    }
    if (const auto Ratio = GetFloatProp(Node.Props, "ratio")) {
        Built->SplitRatio = *Ratio;
    }
    if (const auto MinPaneSize = GetFloatProp(Node.Props, "minPaneSize")) {
        Built->MinPaneSizeLogical = *MinPaneSize;
    }
    if (const auto HandleThickness = GetFloatProp(Node.Props, "handleThickness")) {
        Built->HandleThicknessLogical = *HandleThickness;
    }
    if (auto First = BuildWidgetTreeInternal(Node.Children[0], Context, &ThisStyleElement,
                                              /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
        Built->SetFirst(std::move(First));
    }
    if (auto Second = BuildWidgetTreeInternal(Node.Children[1], Context, &ThisStyleElement,
                                               /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
        Built->SetSecond(std::move(Second));
    }
    return Built;
}
```

`ApplySharedPropsToWidget` (the plain-field sibling of the `Builder`-based
`ApplySharedProps` template — already used by `BuildScroll`) needs to exist for
`SplitPanel` the same way it already does for `ScrollablePanel`; check whether it's
already generic over any `WidgetBase`-derived type before assuming a new overload is
needed. `IrisTagToLustreTag` needs the matching `case IrisElementTag::Split: return
"Split";` addition too, so a `.lustre` rule can target `class="..."` on a `<Split>` the
same way it can on `<Scroll>` today (this one has no open question the way `<Native>`'s
does — `SplitPanel : Box`, so `ApplyBoxStyle`/`ApplyLayout` already apply to it exactly
like any other `Box`-derived widget).

### Required changes elsewhere

**Status: implemented, in a later session.** `SplitPanelStyle` (`SplitPanel.h:11-15`) has
`ColorHandle`/`ColorHandleHovered`/`ColorHandleDragged` fields `BoxStyle` doesn't —
`StyleApplier.cpp`'s generic `ApplyBoxStyle` doesn't reach them.

Rather than a new `handle-color` Lustre property (rejected — see below), the fix reuses
the existing `color` property: `StyleApplier.cpp`'s `Apply()` now has a
`dynamic_cast<SplitPanel*>` branch (after the generic `AsBox` path, since `SplitPanel :
Box`) that resolves `Style.TextColor`/`Style.Hover->TextColor`/`Style.Active->TextColor`
into `ColorHandle`/`ColorHandleHovered`/`ColorHandleDragged` via `SplitPanel::ApplyStyle`
— the same "one cascading foreground-color property, not a new component-specific one"
reasoning already applied to `<Icon>`'s color (`docs/next_steps.md`'s 2026-07-23 second-pass
entry). No `:disabled` variant, matching the gradient overlays' precedent (no
`ColorHandleDisabled` field exists).

Two things this needed that weren't free:
- `SplitPanel::ApplyStyle(const SplitPanelStyle&)` assigns the whole inherited `BoxStyle`
  slice wholesale (`Style = static_cast<const BoxStyle&>(InStyle)`, not field-by-field),
  so the wiring first seeds a local `SplitPanelStyle` from `AsBox->Style` (already
  populated by `ApplyBoxStyle` earlier in `Apply()`) and from the widget's own current
  handle-color getters, before overwriting only the fields this call's `Style` actually
  sets — otherwise it would silently clobber background/border/padding/etc, or reset an
  unrelated handle state back to transparent.
- Those getters didn't exist: `ColorHandle`/`ColorHandleHovered`/`ColorHandleDragged` were
  private on `SplitPanel` with no accessor (`Draw()` was the only reader). Added
  `GetHandleColor()`/`GetHandleColorHovered()`/`GetHandleColorDragged()` to
  `vendor/penumbra`'s `SplitPanel.h` (commit `4a45581`, pushed to its `main`, same
  `GetInteractionState()`-alongside-private-field shape `WidgetBase` already uses).

New regression coverage in `tests/LustreStyleApplierTests.cpp`:
`TestColorReachesASplitPanelHandle`, `TestHoverAndActiveColorOverlaysReachASplitPanelHandle`,
`TestNoColorOverlayLeavesSplitPanelHandleAtDefaultAndKeepsBoxStyle`. Full build +
`penumbra_ui_backend_tests` (0 failures) + `test_lustre` (29 passed) + `test_iris` (138
passed) clean.

**Why not a new `handle-color` property**: flagged during review — this codebase already
has exactly three general-purpose color concepts (`background-color`, `border-color`,
`color`/foreground), and a split handle is a foreground-ish decorative bar, not a second
background or a border — inventing `handle-color` would have been a component-specific
property where CSS convention (and this repo's own `<Icon>` precedent) says a cascading
`color` should do. No `vendor/lustre` change was needed at all as a result — `color`
already resolves to `ResolvedStyle::TextColor` and required no new parsing.

## 3. Explicitly not requested

- **A generic `Umbra::IWidget` → `WidgetBase` bridge for implementations other than
  `PenumbraWidget`** — this repo only has one `Umbra::IWidget` implementation
  (`PenumbraWidget` itself), so `BuildNative`'s `dynamic_cast` covers the only case that
  can actually occur today. A `build{}` escape hatch handing back some other backend's
  widget wrapped as `Umbra::IWidget` isn't a real scenario in this stack.
- **Deciding whether Lustre styling should apply to `<Native>`-wrapped widgets at all** —
  flagged as an open question above at the time; resolved in a later session, see
  `docs/native_lustre_styling_decision.md`.
- **`SplitPanelStyle`'s handle-color fields** — noted above as a real but separate gap at
  the time; resolved in a later session, see §2's own follow-up above.
- **Implementing any of this** — per this ecosystem's "ask the dependency, then stop"
  convention, this doc is the handoff; `pharos-proto`'s own migration of `TreeRow`/
  `DropdownTrigger`/`ChevronSeparator`/`ViewportWidget`/the root layout to `<Native>`/
  `<Split>` is that repo's own follow-up once this lands.
