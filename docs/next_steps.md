# penumbra-ui-backend — Next Steps

> A handoff pointer, not a spec — kept short deliberately. Refreshed at
> the end of each work session; supersedes its own previous contents
> rather than accumulating history (the individual gap/spec docs are the
> durable record).
> Last updated: 2026-07-23.

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

- `docs/native_split_backend_wiring_gap.md` — **implemented** (commit `52f6f14`):
  `Walker.cpp` now has `BuildNative`/`BuildSplit` build cases for both tags `iris` landed
  (`main` commit `37fbcc3`), unblocking `pharos-proto`'s next round of componentization
  (`TreeRow`/`DropdownTrigger`/`ChevronSeparator`/`ViewportWidget`/the root layout). Two
  smaller follow-on questions noted in the doc's own "Explicitly not requested" section
  are still open (Lustre-styling-through-`<Native>` semantics, `SplitPanelStyle`'s
  handle-color fields).
- `docs/pseudo_class_plain_box_decision.md` — the open decision above.
- `docs/build_context_style_mismatch_gap.md` — a previous real
  `pharos-proto`-triggered bug in this same `StyleApplier`/`Walker`
  pairing, same shape of "compiles clean, fails silently downstream" risk
  worth keeping in mind when touching this file.
