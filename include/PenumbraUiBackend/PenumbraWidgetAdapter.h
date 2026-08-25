#pragma once

#include "PenumbraUiBackend/Walker.h"

#include "Iris/SlotRuntime.h"
#include "Umbra/IWidget.h"

#include "Penumbra/Backends/IImageBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PenumbraUiBackend {

// Wraps a `Penumbra::Widgets::WidgetBase` to satisfy `Umbra::IWidget` — the bridge
// Iris's Stage 3 reconciler (`iris::ReconcileWidget`/`ReconcileChildren`, in the `iris`
// repo) needs to update a real Penumbra widget tree without ever naming a Penumbra type
// itself (docs/iris_stage3_implementation_decision.md's "What remains deliberately
// deferred", now closed).
//
// **Ownership model.** A `PenumbraWidget` is either *owning* (holds the real
// `unique_ptr<WidgetBase>` itself — true for a freshly built subtree's root, or any
// widget nothing else currently claims) or *attached* (a non-owning view onto a widget
// whose real ownership already lives inside some other `Box`'s own `Children` vector —
// true for every non-root node in an already-built subtree, and for any child once
// `InsertChildAt` below has placed it into a real parent). `RemoveChildAt` reverses
// that: it pulls the widget's real ownership back out of the parent `Box::Children` and
// hands it to the returned wrapper, which becomes owning again — exactly the state a
// widget needs to be back in before it can be `InsertChildAt`'d somewhere else, which is
// exactly what the reconciler's extract-then-reinsert list-diff strategy
// (`iris::ReconcileList`) does on every pass. This dual-mode design is what lets a
// `PenumbraWidget*` stay a stable identity across reconciliation even though the *real*
// Penumbra ownership underneath it moves between `Box::Children` and this wrapper as
// children get inserted, removed, and reordered.
class PenumbraWidget : public Umbra::IWidget {
public:
    // Takes ownership of a freshly-built widget — e.g. a whole subtree's root
    // (`WrapExistingTree` below), or any standalone widget nothing else has claimed.
    explicit PenumbraWidget(std::unique_ptr<Penumbra::Widgets::WidgetBase> Widget);

    Penumbra::Widgets::WidgetBase* RawWidget() const;

    // Transitions this wrapper from owning its widget to a non-owning (attached) view
    // onto it, returning real ownership to the caller. Used exactly once per widget,
    // right before it becomes a real child of some `Box` — either via `InsertChildAt`
    // below, or by whoever eventually splices a `<Slot>`'s own root widget into the
    // static tree above it (still deliberately out of scope here — see
    // docs/iris_stage3_implementation_decision.md).
    std::unique_ptr<Penumbra::Widgets::WidgetBase> DetachOwnership();

    void ApplyPropDiff(const Umbra::IrisPropDiff& Diff) override;

    std::size_t                     GetChildCount() const override;
    Umbra::IWidget*                 GetChildAt(std::size_t Index) const override;
    void                             InsertChildAt(std::size_t Index, std::unique_ptr<Umbra::IWidget> Child) override;
    std::unique_ptr<Umbra::IWidget> RemoveChildAt(std::size_t Index) override;

    // `<Image>`'s `src` prop needs to re-decode through a real image backend/renderer
    // on change (docs/iris_core_spec.md §3.1 — loading was deliberately kept out of
    // `ImageWidget`'s own `Builder`), the same resources `BuildContext` already carries
    // for the initial mount. Propagated to every wrapper recursively by
    // `WrapExistingTree` so a nested `<Image>` picks it up too, without threading it
    // through every call site by hand.
    void SetImageContext(Penumbra::Backends::IImageBackend* ImageBackend, SDL_Renderer* SdlRenderer);

    // Same propagation pattern as SetImageContext above, for the Lustre style context
    // ApplyPropDiff needs to re-resolve and re-apply style on a class change (see that
    // method's own comment). Either pointer may be null -- a wrapper with no style
    // context configured simply skips re-styling on every class change, unchanged
    // behavior from before this wiring existed.
    void SetStyleContext(const ::Lustre::StylesheetSet* Sheets, const Lustre::IStyleApplier* StyleApplier);

    // This wrapper's parent in the *wrapper* tree (not the real Penumbra `Box::Children`
    // ownership graph, which InsertChildAt/RemoveChildAt already juggle separately) --
    // nullptr for the root of whatever subtree this wrapper's own `WrapExistingTree`
    // call started at. That root-ness is exactly docs/lustre_core_spec.md §1.2's
    // component-boundary signal for style resolution, the same way Walker.cpp's own
    // `IsComponentRoot` parameter marks a fresh `BuildWidgetTree()` call's root -- only
    // as precise as Iris's current component model allows (see Walker.h's own
    // `BuildWidgetTree` comment for the same caveat).
    PenumbraWidget* GetParent() const { return Parent_; }

    // The real Lustre primitive tag ("Frame"/"Inline"/"Grid"/"Image"/"Text") this widget
    // was originally built from, if `WrapExistingTree`/`AdoptChildrenFromRawTree` were
    // given a `PrimitiveTagMap` to look it up in -- empty string otherwise (a widget not
    // built via `BuildWidgetTree`, or wrapped without a map). See `PrimitiveTagMap`'s own
    // comment in Walker.h for why this can't be recovered from the widget's C++ type
    // alone (`Frame` and `Grid` both build to a plain `Box`).
    const std::string& GetPrimitiveTag() const { return PrimitiveTag_; }
    void                SetPrimitiveTag(std::string Tag) { PrimitiveTag_ = std::move(Tag); }

    // Looks up a `ref`-tagged descendant by ref name (docs/next_steps.md's "GetByRef
    // lookup on a mounted tree" ask) -- nullptr if no node in this mount carried that
    // `ref`, or `WrapExistingTree` wasn't given a `RefMap` to populate it from in the
    // first place (same "optional, absent means untouched" convention as
    // GetPrimitiveTag()'s own PrimitiveTagMap). The registry itself lives only on the
    // mount root (built once per `WrapExistingTree` call, not duplicated onto every
    // wrapper) -- callable from any wrapper in the tree regardless, since this walks up
    // to the root via GetParent() first.
    Umbra::IWidget* GetByRef(std::string_view Ref) const;

    // docs/next_steps.md's "swap a live real widget when a reconciled `<Native>`
    // re-renders" ask. Swaps this wrapper's own live widget for NewWidget, in place at
    // the exact same tree position -- same real Penumbra parent, same slot -- without
    // this wrapper ever losing its own identity (this `PenumbraWidget*`, and its own
    // position in `Parent_->Children_`) or its wrapper-tree `Parent_` pointer. Only what
    // `RawWidget()` returns, and everything reachable under it, changes.
    //
    // This is deliberately a plain method on the concrete class, not an override of any
    // `Umbra::IWidget` virtual -- `Umbra::IWidget` (vendored via `vendor/iris/libs/
    // umbra-interfaces`, a different repo) has no such operation today, and adding one
    // there is exactly the cross-repo handoff-shape question this ask's own doc entry
    // flags as still open with `iris-proto`'s reconciler side. Calling this today means
    // going through a concrete `PenumbraWidget*` (e.g. one already obtained via
    // `GetByRef`), not through the backend-agnostic `Umbra::IWidget*` interface a real
    // reconciler would actually be holding -- a real, load-bearing gap, not an oversight;
    // see this method's own .cpp comment and docs/next_steps.md for the full picture.
    //
    // For a *Box*-backed parent, this is a thin wrapper over the already-existing
    // `Box::ReplaceChild` (`vendor/penumbra`'s `Box.h`) -- which hands the replaced
    // widget back intact rather than destroying it, letting the caller decide exactly
    // when its destructor cascade (and anything hooked to its `WidgetBase::OnDestroyed`,
    // e.g. a lifecycle unregistration, `Walker.cpp`'s `RegisterLifecycleIfPresent`) runs.
    // This method preserves that same "hand the old widget back, never destroy it as a
    // side effect" contract for the Box case.
    //
    // For a *SplitPanel*-backed parent, that contract can NOT be honored today:
    // `SplitPanel::SetFirst`/`SetSecond` (`vendor/penumbra`'s `SplitPanel.h`) are the only
    // mutation surface `SplitPanel` exposes, and both destroy whatever they replace
    // immediately via plain `unique_ptr` move-assignment -- there is no
    // `SplitPanel::ReplaceFirst`/`ReplaceSecond` mirroring `Box::ReplaceChild`'s
    // hand-it-back shape. This method still performs the swap correctly (matching
    // `pharos-proto`'s own hand-rolled `GRootSplit->SetFirst(...)` behavior today -- no
    // regression), but always returns `nullptr` for that case; a widget that needs
    // advance notice before this happens should hook its own `WidgetBase::OnDestroyed`,
    // which still fires correctly during the resulting destructor cascade regardless of
    // which path replaced it. See docs/next_steps.md for the precise proposed upstream
    // `penumbra` API this is standing in for.
    //
    // For the root of a whole mount (`Parent_ == nullptr`), there is no real parent
    // container to thread through at all -- this wrapper's own `OwnedWidget_` is swapped
    // directly, and the old one is always handed back intact (nothing to destroy inline
    // in this case; the wrapper owns it directly).
    //
    // Every case rebuilds this wrapper's own `Children_` from `NewWidget`'s real
    // structure afterward (the old ones pointed into the now-detached subtree) --
    // `Tags`/`Refs`, if given, seed the new children's `GetPrimitiveTag()`/this mount's
    // `GetByRef()` registry exactly the way `WrapExistingTree`'s own initial wrap does
    // (found by walking up to the true mount root via `GetParent()`, since `RefRegistry_`
    // only ever lives there). This wrapper's own `SetImageContext`/`SetStyleContext`
    // values are propagated to the new children the same way `AdoptChildrenFromRawTree`
    // already does for an initial wrap. Re-resolving this position's own Lustre style
    // (as opposed to its descendants', already covered by the recursive `AdoptChildren`
    // walk) is deliberately out of scope here -- unlike `ApplyPropDiff`'s `ClassName`
    // branch, a whole-widget swap has no single changed prop driving it, and whatever
    // produced NewWidget (typically a `<Native>` builder calling `BuildWidgetTree` with
    // its own `BuildContext` again) already owns styling its own output.
    std::unique_ptr<Penumbra::Widgets::WidgetBase>
    ReplaceRawWidget(std::unique_ptr<Penumbra::Widgets::WidgetBase> NewWidget, const PrimitiveTagMap* Tags = nullptr,
                      const RefMap* Refs = nullptr);

protected:
    // Non-owning (attached) construction — used for every non-root node when wrapping
    // an already-built subtree (`WrapExistingTree`).
    explicit PenumbraWidget(Penumbra::Widgets::WidgetBase* AttachedWidget);

private:
    // Recursively wraps every child already present under RawWidget() (i.e. real
    // Penumbra children `BuildWidgetTree` already built) into attached `PenumbraWidget`
    // wrappers, appended to Children_. Called once, by `WrapExistingTree`, on the root
    // wrapper it just constructed. `ReverseRefs`/`RegistryRoot` (either both null or both
    // set together) are `WrapExistingTree`'s own ref bookkeeping -- see its comment.
    void AdoptChildrenFromRawTree(Penumbra::Backends::IImageBackend* ImageBackend, SDL_Renderer* SdlRenderer,
                                   const ::Lustre::StylesheetSet* Sheets, const Lustre::IStyleApplier* StyleApplier,
                                   const PrimitiveTagMap* Tags,
                                   const std::unordered_map<const Penumbra::Widgets::WidgetBase*, std::string>* ReverseRefs,
                                   PenumbraWidget* RegistryRoot);

    std::unique_ptr<Penumbra::Widgets::WidgetBase> OwnedWidget_;
    Penumbra::Widgets::WidgetBase*                  AttachedWidget_{nullptr};
    std::vector<std::unique_ptr<PenumbraWidget>>   Children_;
    PenumbraWidget*                                 Parent_{nullptr};
    std::string                                     PrimitiveTag_;

    Penumbra::Backends::IImageBackend* ImageBackend_{nullptr};
    SDL_Renderer*                       SdlRenderer_{nullptr};

    const ::Lustre::StylesheetSet* Sheets_{nullptr};
    const Lustre::IStyleApplier*    StyleApplier_{nullptr};

    // Only ever non-empty on a mount's root wrapper (Parent_ == nullptr) -- see
    // GetByRef()'s own comment for why a non-root wrapper still works transparently.
    std::unordered_map<std::string, PenumbraWidget*> RefRegistry_;

    friend std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<Penumbra::Widgets::WidgetBase>,
                                                              Penumbra::Backends::IImageBackend*, SDL_Renderer*,
                                                              const ::Lustre::StylesheetSet*,
                                                              const Lustre::IStyleApplier*, const PrimitiveTagMap*,
                                                              const RefMap*);
};

// Wraps an already-built Penumbra widget subtree (e.g. `BuildWidgetTree`'s output) into
// a matching tree of `PenumbraWidget` identity wrappers: the root owns the real widget;
// every nested wrapper is a non-owning (attached) view onto its position inside the
// real tree's own `Box::Children` — see `PenumbraWidget`'s own doc comment for why that
// split matters. `ImageBackend`/`SdlRenderer` are propagated to every wrapper so a
// nested `<Image>`'s later `src` prop changes can re-decode correctly; `Sheets`/
// `StyleApplier` (either may be null) the same way, so a later class change anywhere in
// the subtree can re-resolve and re-apply its Lustre style (`ApplyPropDiff` below). `Tags`
// (also optional), if the caller built one via `BuildWidgetTree`'s own `OutTags`
// parameter, seeds each wrapper's `GetPrimitiveTag()` with the real originating tag.
// `Refs` (also optional), if the caller built one via `BuildWidgetTree`'s own `OutRefs`
// parameter, seeds the returned root wrapper's `GetByRef()` registry -- inverted from
// `Refs`' own (ref name -> raw WidgetBase*) shape to (raw WidgetBase* -> ref name) once
// here, then matched against each wrapper as this walk constructs it.
std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<Penumbra::Widgets::WidgetBase> Root,
                                                  Penumbra::Backends::IImageBackend*             ImageBackend,
                                                  SDL_Renderer*                                   SdlRenderer,
                                                  const ::Lustre::StylesheetSet* Sheets = nullptr,
                                                  const Lustre::IStyleApplier*    StyleApplier = nullptr,
                                                  const PrimitiveTagMap*          Tags = nullptr,
                                                  const RefMap*                   Refs = nullptr);

// Builds an `iris::MountFn` (`Iris/SlotRuntime.h`, in the `iris` repo) combining Stage
// 2's `BuildWidgetTree` with `WrapExistingTree` above — what Stage 3's reconciler calls
// whenever it needs a whole fresh subtree built from scratch (the "no old to diff
// against" mounting path, docs/iris_stage3_implementation_decision.md).
iris::MountFn MakeMountFn(BuildContext Context);

} // namespace PenumbraUiBackend
