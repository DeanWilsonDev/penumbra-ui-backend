#pragma once

#include "PenumbraUiBackend/Walker.h"

#include "Iris/SlotRuntime.h"
#include "Umbra/IWidget.h"

#include "Penumbra/Backends/IImageBackend.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <SDL3/SDL.h>

#include <memory>
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

private:
    // Non-owning (attached) construction — used for every non-root node when wrapping
    // an already-built subtree (`WrapExistingTree`).
    explicit PenumbraWidget(Penumbra::Widgets::WidgetBase* AttachedWidget);

    // Recursively wraps every child already present under RawWidget() (i.e. real
    // Penumbra children `BuildWidgetTree` already built) into attached `PenumbraWidget`
    // wrappers, appended to Children_. Called once, by `WrapExistingTree`, on the root
    // wrapper it just constructed.
    void AdoptChildrenFromRawTree(Penumbra::Backends::IImageBackend* ImageBackend, SDL_Renderer* SdlRenderer,
                                   const ::Lustre::StylesheetSet* Sheets, const Lustre::IStyleApplier* StyleApplier,
                                   const PrimitiveTagMap* Tags);

    std::unique_ptr<Penumbra::Widgets::WidgetBase> OwnedWidget_;
    Penumbra::Widgets::WidgetBase*                  AttachedWidget_{nullptr};
    std::vector<std::unique_ptr<PenumbraWidget>>   Children_;
    PenumbraWidget*                                 Parent_{nullptr};
    std::string                                     PrimitiveTag_;

    Penumbra::Backends::IImageBackend* ImageBackend_{nullptr};
    SDL_Renderer*                       SdlRenderer_{nullptr};

    const ::Lustre::StylesheetSet* Sheets_{nullptr};
    const Lustre::IStyleApplier*    StyleApplier_{nullptr};

    friend std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<Penumbra::Widgets::WidgetBase>,
                                                              Penumbra::Backends::IImageBackend*, SDL_Renderer*,
                                                              const ::Lustre::StylesheetSet*,
                                                              const Lustre::IStyleApplier*, const PrimitiveTagMap*);
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
std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<Penumbra::Widgets::WidgetBase> Root,
                                                  Penumbra::Backends::IImageBackend*             ImageBackend,
                                                  SDL_Renderer*                                   SdlRenderer,
                                                  const ::Lustre::StylesheetSet* Sheets = nullptr,
                                                  const Lustre::IStyleApplier*    StyleApplier = nullptr,
                                                  const PrimitiveTagMap*          Tags = nullptr);

// Builds an `iris::MountFn` (`Iris/SlotRuntime.h`, in the `iris` repo) combining Stage
// 2's `BuildWidgetTree` with `WrapExistingTree` above — what Stage 3's reconciler calls
// whenever it needs a whole fresh subtree built from scratch (the "no old to diff
// against" mounting path, docs/iris_stage3_implementation_decision.md).
iris::MountFn MakeMountFn(BuildContext Context);

} // namespace PenumbraUiBackend
