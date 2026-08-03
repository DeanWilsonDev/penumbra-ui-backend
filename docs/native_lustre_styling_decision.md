# penumbra-ui-backend — should Lustre styling apply to `<Native>`-wrapped widgets?

> **Status:** Decided — yes, generically, same as any other built widget. No code change:
> this is what `Walker.cpp` already does, and this doc is the record of that being the
> intended behavior rather than an oversight.
> **Trigger:** `docs/native_split_backend_wiring_gap.md` §1 flagged this as a genuinely
> open question when `<Native>`'s build case landed (commit `52f6f14`), deliberately left
> for a later decision rather than guessed at inline.

---

## The question

`BuildNative` (`src/PenumbraUiBackend/Walker.cpp`) unwraps a `<Native>` node's
`build{}`-supplied `Umbra::IWidget` back into a real `WidgetBase` via
`PenumbraWidget::DetachOwnership()`. `BuildWidgetTreeInternal`'s post-build Lustre-apply
step (`Context.Style`/`Context.StyleApplier`, `Walker.cpp:512-521` at the time of
writing) then runs unconditionally for every built widget, `<Native>` included — there's
no `if (Node.Tag != IrisElementTag::Native)` guard anywhere. `IrisTagToLustreTag` also
maps `Native` → `"Native"`, so a `.lustre` rule can target it via `class="..."` the same
as any other tag.

The open question was whether that's actually the right default: a `<Native>`-wrapped
widget's own `build{}` lambda (in `pharos-proto`'s case, `TreeRow`/`DropdownTrigger`/
`ViewportWidget`) typically already applies its own styling directly in C++ before
handing the widget back. Re-resolving Lustre classes on top could be redundant at best,
or could silently clobber intentional in-`build{}` styling at worst — `StyleApplier::Apply`
only writes fields the resolved style actually sets, but a `.lustre` rule that
accidentally matches a `<Native>` node's class (e.g. a shared base class also used
elsewhere) would still overwrite whatever the `build{}` lambda set for that same field.

## Decision: keep applying generically

Reasons, in order of weight:

1. **Consistency with every other tag.** No other `IrisElementTag` gets a "skip Lustre"
   carve-out — `<Scroll>`, `<Input>`, `<Icon>` all resolve and apply styling through the
   exact same post-build step regardless of what the tag's own `BuildXxx` helper already
   set on the widget (e.g. `BuildScroll` sets `ScrollablePanel`-specific fields directly,
   and Lustre still layers `BoxStyle` on top afterward). Special-casing `<Native>` out
   would be the one tag-specific exception to an otherwise-uniform pipeline, for a
   maybe-problem rather than an observed one.
2. **It's exactly the hook `iris`'s own proposed-API sketch wanted.** `docs/
   native_split_backend_wiring_gap.md` §1 itself notes `class="explorer-row"` as a
   plausible reason `<Native>` carries a `class` prop at all — if Lustre never applied to
   `<Native>`-wrapped widgets, that prop would be dead weight, parsed and threaded through
   `Component`/`ElementNode` for no reachable effect.
3. **The "clobber" risk is opt-in, not structural.** A `.lustre` rule only reaches a
   `<Native>` node if that node has a matching `class`. A `build{}` lambda that wants full
   control simply doesn't put a `class` on its `<Native>` tag (or uses one no `.lustre`
   file targets) — the same opt-out every other tag already has. Nothing about `<Native>`
   makes it more exposed to accidental class collisions than a hand-built `<Frame
   class="row">` would be.
4. **Reversing this later is cheap; guessing wrong now is not.** Since nothing was coded
   for the restrictive option, "apply generically" costs nothing extra to keep. If a real
   `pharos-proto` consumer hits an actual clobbering bug, narrowing the behavior later
   (e.g. an explicit opt-out prop) is a small, targeted fix informed by a real case —
   cheaper than speculatively building an escape hatch nobody has needed yet.

## What this means for `pharos-proto`

`TreeRow`/`DropdownTrigger`/`ChevronSeparator`/`ViewportWidget`, once migrated to
`<Native>`, can rely on `class="..."` + a `.lustre` rule reaching the widget their
`build{}` lambda returns, exactly like any other tag. If a given `build{}` lambda's own
styling should be the final word on some property, the fix is the same one available to
every other tag today: don't give that node a `class` a `.lustre` rule would match, or
scope the `.lustre` rule narrowly enough not to collide. No new API surface needed.

## Explicitly not requested

- **An opt-out mechanism** (e.g. a `no-style` prop, or a `<Native>`-specific flag skipping
  the post-build apply step) — not built, since no real collision has been observed yet.
  A real follow-up if `pharos-proto`'s migration surfaces one.
- **Changing `BuildNative` or the post-build apply step** — this doc changes nothing in
  code; the existing unconditional behavior is what's being ratified.
