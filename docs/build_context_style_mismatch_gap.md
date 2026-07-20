# penumbra-ui-backend — `BuildContext.Style` pointing at the wrong stylesheet fails silently

> **Status:** Implemented, per the "Proposed: a debug-mode zero-match check"
> section below — `BuildWidgetTree` now prints a `stderr` warning when a
> whole subtree's classed nodes resolve nothing at all from
> `Context.Style`. Verified with `tests/StyleMismatchDiagnosticTests.cpp`
> (a mismatched stylesheet warns with the right count; a matching one, a
> partially-matching one, no `Style` configured at all, and no classed
> nodes at all all stay quiet) plus the full existing
> `penumbra_ui_backend_tests` suite still passing.
> **Trigger:** A real, hours-to-diagnose bug in `pharos-proto`'s
> `InspectorRow` migration — see "What actually happened" below.

---

## What this is

`BuildWidgetTreeInternal` (`src/PenumbraUiBackend/Walker.cpp:368`) resolves
and applies Lustre style for every widget it builds:

```cpp
// src/PenumbraUiBackend/Walker.cpp:368-371
if (Built && Context.Style != nullptr && Context.StyleApplier != nullptr) {
    const auto Resolved = Lustre::ResolveStyle(ThisStyleElement, *Context.Style);
    Context.StyleApplier->Apply(*Built, Resolved);
}
```

`Context.Style` (`const ::Lustre::StylesheetSet*`, `BuildContext`'s own
field — `include/PenumbraUiBackend/Walker.h:65`) is caller-supplied: nothing
in `BuildWidgetTree`'s signature or `BuildContext`'s shape ties a given
`Style` to the specific component being built. If a caller passes a
`BuildContext` whose `Style` doesn't actually contain rules for the
component's own classes, `Lustre::ResolveStyle` just returns an
all-`std::nullopt` `ResolvedStyle` (no rule matched anything — not an
error, `ResolveStyle`'s own contract per `lustre_core_spec.md` §6, "v1 does
no property-applicability validation") — `StyleApplier->Apply()` then
applies nothing, silently. Every property-having field on the built widget
stays at its type's own default: `Box::Layout` stays `LayoutMode::None`,
`Box::Style.ColorBackground` stays transparent, etc.

The specific, costly failure mode: a container `Frame` whose `.lustre`
class was supposed to set `display: stack` (mapping to `Box::Layout`,
`Walker.cpp`'s `ApplyLayout` in `StyleApplier.cpp`) silently keeps
`Layout::None` instead. `Box::MeasureContent`'s `LayoutMode::None` branch
reports `{0, 0}` regardless of the container's real children — so the
container (and everything the container's own parent lays out relative to
it) collapses to zero size. Leaf widgets inside it (`Label`, `IconWidget`)
still measure their own intrinsic size independent of their parent's
style, so they're internally fine — nothing crashes, and text content
still exists — but nothing visible reaches the screen for the collapsed
subtree, or it renders at a nonsensical size next to whatever legitimately
resolved.

## What actually happened (`pharos-proto`)

`pharos-proto`'s `inspector_panel.cpp` builds two different components
through the same `BuildContext` (`Context` shared by both call sites via
capture-by-reference):

- `InspectorChrome` — `context.Style = &sheets` where `sheets` wraps
  `InspectorChrome.lustre` (`.inspector-chrome`, `.inspector-placeholder`,
  `.inspector-title`, `.inspector-grid`). Built once, correctly.
- `InspectorRow` — built ~20 times (once per metric row) by
  `buildInspectorRow`, which called
  `BuildWidgetTree(InspectorRow(props), context, &tagMap)` reusing that
  *same* `context` object. `context.Style` still pointed at
  `InspectorChrome`'s stylesheet — none of `InspectorRow.iris`'s own
  classes (`.inspector-row`, `.inspector-row-top`,
  `.inspector-row-value-row`) exist in it, so nothing ever resolved.

Every `InspectorRow` instance's containing `Frame`s stayed at
`Layout::None` and measured to `{0, 0}`. The grid rendered as a fixed
~95px sliver with no visible rows — `Label`/`IconWidget` content inside
each row was completely correct (font, text, icon), which is what made
this so easy to miss: a glance at generated widget state or a debugger
watch on `Label::Text` would show everything fine.

**Not caught by code review, and not caught by build success** — the
whole tree still builds and links cleanly; a wrong `Style` pointer is not
a type error. **Not caught by `pharos-proto`'s own automated visual
regression suite** either — `InspectorPanel` isn't covered by it (a
separate, pre-existing gap, see `pharos-proto`'s own
`docs/pharos_visual_regression_feature_request.md`). Only caught weeks
after `InspectorRow`'s original migration, via a real screenshot of the
running app with a node actually selected.

Fixed on the `pharos-proto` side by constructing a local `BuildContext`
copy with `.Style` repointed at `InspectorRow`'s own sheets before the
`BuildWidgetTree` call (`buildInspectorRow` now does `BuildContext
rowContext = context; rowContext.Style = &sheets;`). That fixes *this*
call site. It does not fix the underlying gap: nothing in
`BuildWidgetTree`/`BuildContext` would have caught the mistake, or would
catch the next one shaped like it — any future per-instance component
built through a `BuildContext` borrowed from an unrelated caller has the
same exposure, with the same "looks like a completely broken layout, no
error anywhere" symptom.

## Proposed: a debug-mode zero-match check

`BuildWidgetTree`'s own top-level entry point (`Walker.cpp:386-389`) is
the natural place to hook this — one call there covers a whole component
subtree, matching the actual failure's blast radius (the bug above was a
"the whole component never matched its own stylesheet" failure, not a
single mistyped class deep in an otherwise-fine tree):

```cpp
// src/PenumbraUiBackend/Walker.cpp — sketch, not exact
std::unique_ptr<WidgetBase> BuildWidgetTree(const Component& Node, const BuildContext& Context,
                                             PrimitiveTagMap* OutTags) {
    std::size_t ClassedNodes = 0;
    std::size_t ResolvedNodes = 0;
    auto Built = BuildWidgetTreeInternal(Node, Context, /*ParentStyleElement=*/nullptr,
                                          /*IsComponentRoot=*/true, OutTags, &ClassedNodes, &ResolvedNodes);
#ifndef NDEBUG
    if (Context.Style != nullptr && ClassedNodes > 0 && ResolvedNodes == 0) {
        std::fprintf(stderr,
                     "PenumbraUiBackend::BuildWidgetTree: %zu widget(s) had a `class` prop, "
                     "but none resolved any property from the given Context.Style -- likely "
                     "the wrong StylesheetSet for this component.\n",
                     ClassedNodes);
    }
#endif
    return Built;
}
```

`ClassedNodes`/`ResolvedNodes` would need threading through
`BuildWidgetTreeInternal`'s existing recursion (it already computes
`ThisStyleElement.ClassName()` and calls `Lustre::ResolveStyle` per node
at `Walker.cpp:368-371` — the two counters are a one-line addition at that
existing call site, not a second tree walk). "Resolved" means
`Lustre::ResolveStyle`'s returned `ResolvedStyle` has *any* field set (not
all-`nullopt`) — `ResolvedStyle` has no built-in "is this empty"
predicate today (`lustre/include/Lustre/ResolvedStyle.h`), so this would
also need a small helper there, or an inline check of the fields
`ApplyBoxStyle`/`ApplyLayout` in `StyleApplier.cpp` actually read.

### Why "any node resolved something" and not per-node warnings

A per-node "this specific class matched nothing" warning would also catch
a genuinely mistyped class name deep in an otherwise-correctly-styled
tree — a real, separate class of bug worth catching too — but scoping
*this* check to "the whole subtree, zero total resolutions" keeps it
cheap (one pair of counters, no new data structure) and gives it a very
low false-positive rate: a component that's *supposed* to have some
unstyled leaf classes (rare but not inherently wrong) won't trip it,
only a component where literally nothing landed, which is a much
stronger signal of "wrong stylesheet entirely" specifically. A follow-up
could add the finer-grained per-class check later as its own thing if it
turns out to be worth the extra bookkeeping — not proposed here.

### Debug-only, not always-on

Proposed as a `#ifndef NDEBUG` check (a `std::fprintf(stderr, ...)`, not a
thrown exception or assert) rather than always compiled in: `BuildContext`
is already documented as tolerant of partially-missing config (`Style`/
`StyleApplier` left null is a supported, deliberate "no styling"
mode — `Walker.h`'s own `BuildContext` doc comment), and a structural test
that builds a tree with no real stylesheet at all (several already exist
in `tests/WalkerTests.cpp`, e.g. `TestNoneProducesNoWidget`-style cases
building with `BuildContext{}`) shouldn't start printing warnings just
because it deliberately supplies no `Style`. The check's own condition
(`Context.Style != nullptr && ClassedNodes > 0 && ResolvedNodes == 0`)
already guards the `Style == nullptr` "deliberately no styling" case, but
keeping it debug-only avoids adding runtime cost (`ClassedNodes`/
`ResolvedNodes` bookkeeping, however cheap) to every build in the release
path for a check whose whole purpose is catching an authoring mistake
during development.

## Required changes (as implemented)

- `BuildWidgetTreeInternal`/`BuildAndAttachChildren`/`BuildFrame`/
  `BuildGrid`/`BuildInline`/`BuildScroll` each grew a trailing
  `StyleMatchStats* Stats` parameter (a local two-`std::size_t` struct,
  `ClassedNodes`/`ResolvedNodes`, defined next to `WalkerStyleElement`) —
  every recursive call site needed updating to thread it through.
  `BuildWidgetTree`'s own public signature didn't need to change: it
  constructs a local `StyleMatchStats`, passes `&Stats` into the internal
  recursion, and the `#ifndef NDEBUG` check + `stderr` output stay
  internal to that one function.
- `ResolvedStyleIsEmpty()` — a local free function in `Walker.cpp`'s own
  anonymous namespace, not added to `lustre`'s `ResolvedStyle` itself.
  Deliberately kept local rather than filing a second cross-repo doc for
  a one-function addition: `Walker.cpp` already has one hand-maintained
  full-field enumeration of `ResolvedStyle` doing something adjacent
  (`StyleResolution.cpp`'s `MergeInto`), so a second one here isn't a new
  maintenance burden shape, just a second copy of it. Revisit if a third
  call site independently wants the same predicate.

## What unblocks

The next `InspectorRow`-shaped mistake (a per-instance component built
through a `BuildContext` borrowed from an unrelated caller, or any other
way a wrong `Style` pointer reaches `BuildWidgetTree`) prints a real,
actionable warning the moment it happens — at build/first-run time, not
weeks later from a screenshot. Doesn't prevent the mistake (nothing in
`BuildContext`'s shape stops a caller from doing this), but turns a
silent, completely-invisible-in-review failure into a loud one.

## Explicitly not requested

- **Compile-time prevention** (e.g. tying a `StylesheetSet` to a specific
  component type via templates) — a much bigger API change to
  `BuildWidgetTree`/`BuildContext`, and every existing call site in every
  consumer (`pharos-proto`, this repo's own `demo/`) would need
  revisiting. Not sized or proposed here.
- **Per-node "this exact class matched nothing" diagnostics** — see "Why
  'any node resolved something'" above; a real, separate, and
  cheaper-to-reason-about follow-up, not part of this ask.
- **Fixing any other call site** — `pharos-proto`'s own `InspectorRow` bug
  is already fixed there; this doc is only the diagnostic gap in
  `BuildWidgetTree` itself that let it go unnoticed.
