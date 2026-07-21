# penumbra-ui-backend — Next Steps

> A handoff pointer, not a spec — kept short deliberately. Refreshed at
> the end of each work session; supersedes its own previous contents
> rather than accumulating history (the individual gap/spec docs are the
> durable record).
> Last updated: 2026-07-21.

## What's actually blocking downstream right now

Nothing known. The `StyleApplier.cpp`-vs-`penumbra`-`Button`-API compile
break this doc previously tracked is fixed (`bc71c8e`, this session):
`AsButton->Style.Color...` instead of `AsButton->Color...`,
`vendor/penumbra` bumped to `dd1d6ab`, `.gitmodules` repointed at the
renamed `penumbra` repo, and the now-dead `Button`-specific reset block in
`PenumbraWidgetAdapter.cpp` removed. Full build + `penumbra_ui_backend_tests`
clean.

## Open decision (not a compile blocker)

**Should `StyleApplier.cpp`'s pseudo-class background-color overlay
(`:hover`/`:active`/`:disabled`) apply to any `Box`, not just `Button`?**
`penumbra@dd1d6ab` moved these fields onto `BoxStyle` specifically so any
classed `Box` could receive them, and `Box` itself already renders them
(`Box::BackgroundForState`) — but `StyleApplier.cpp:127`'s
`dynamic_cast<Button*>` guard is still narrower than that. Not picked
silently — full writeup, proposed fix, and required test changes in
`docs/pseudo_class_plain_box_decision.md`.

## Read first

- `docs/pseudo_class_plain_box_decision.md` — the open decision above.
- `docs/build_context_style_mismatch_gap.md` — a previous real
  `pharos-proto`-triggered bug in this same `StyleApplier`/`Walker`
  pairing, same shape of "compiles clean, fails silently downstream" risk
  worth keeping in mind when touching this file.
