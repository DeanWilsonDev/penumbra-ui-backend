# penumbra-ui-backend — Next Steps

> A handoff pointer, not a spec — kept short deliberately. Refreshed at
> the end of each work session; supersedes its own previous contents
> rather than accumulating history (the individual gap/spec docs are the
> durable record).
> Last updated: 2026-07-23.

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

- `docs/pseudo_class_plain_box_decision.md` — the open decision above.
- `docs/build_context_style_mismatch_gap.md` — a previous real
  `pharos-proto`-triggered bug in this same `StyleApplier`/`Walker`
  pairing, same shape of "compiles clean, fails silently downstream" risk
  worth keeping in mind when touching this file.
