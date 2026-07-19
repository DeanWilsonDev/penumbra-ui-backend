#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "PenumbraUiBackend/Lustre/StyleResolution.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Button.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"

#include <algorithm>

namespace PenumbraUiBackend {

using Penumbra::Widgets::Box;
using Penumbra::Widgets::Button;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::WidgetBase;

namespace {

// Lustre::IStyleTarget over a live PenumbraWidget's ancestor chain, for
// ApplyPropDiff's class-change re-resolution below. Kept as its own small
// value type (not PenumbraWidget implementing IStyleTarget directly) because
// IStyleTarget::Parent() must return a pointer valid for the whole
// Resolve() call, and PenumbraWidget's own tree only has PenumbraWidget*
// parent pointers, not IStyleTarget* ones -- BuildReconcileStyleChain below
// builds a small parallel chain with stable addresses instead.
class ReconcileStyleElement : public ::Lustre::IStyleTarget {
public:
    ReconcileStyleElement(std::string ClassName, std::string PrimitiveTag, bool IsRoot)
        : ClassName_(std::move(ClassName)), PrimitiveTag_(std::move(PrimitiveTag)), IsRoot_(IsRoot) {}

    void SetParent(const ReconcileStyleElement* Parent) { Parent_ = Parent; }

    std::string ClassName() const override { return ClassName_; }
    std::string PrimitiveTag() const override { return PrimitiveTag_; }
    bool         IsComponentRoot() const override { return IsRoot_; }
    const ::Lustre::IStyleTarget* Parent() const override { return Parent_; }

private:
    std::string ClassName_;
    std::string PrimitiveTag_;
    bool        IsRoot_;
    const ReconcileStyleElement* Parent_{nullptr};
};

// Fallback only, for a wrapper whose GetPrimitiveTag() came back empty (built without a
// PrimitiveTagMap -- see Walker.h's own comment on that type). Identifies what real
// Penumbra widget type Widget IS, not what Iris tag originally built it: `Frame` and
// `Grid` both build to a plain `Box` (Walker.cpp's own BuildGrid comment), a distinction
// this can't recover, so it guesses "Frame" for both. When a PrimitiveTagMap *was*
// threaded through (MakeMountFn always does this now), BuildReconcileStyleChain below
// never reaches this function at all -- GetPrimitiveTag() already has the real answer.
std::string InferPrimitiveTag(const WidgetBase& Widget) {
    if (dynamic_cast<const Label*>(&Widget)) {
        return "Text";
    }
    if (dynamic_cast<const InlineContainer*>(&Widget)) {
        return "Inline";
    }
    if (dynamic_cast<const ImageWidget*>(&Widget)) {
        return "Image";
    }
    return "Frame";
}

std::vector<std::unique_ptr<ReconcileStyleElement>> BuildReconcileStyleChain(const PenumbraWidget& Widget) {
    std::vector<std::unique_ptr<ReconcileStyleElement>> Chain;
    const PenumbraWidget* Node = &Widget;
    while (Node != nullptr) {
        const WidgetBase* Raw = Node->RawWidget();
        const std::string& StoredTag = Node->GetPrimitiveTag();
        const std::string  Tag = !StoredTag.empty() ? StoredTag : (Raw ? InferPrimitiveTag(*Raw) : std::string{});
        Chain.push_back(std::make_unique<ReconcileStyleElement>(Raw ? Raw->ClassName : std::string{}, Tag,
                                                                  Node->GetParent() == nullptr));
        Node = Node->GetParent();
    }
    for (std::size_t Index = 0; Index + 1 < Chain.size(); ++Index) {
        Chain[Index]->SetParent(Chain[Index + 1].get());
    }
    return Chain;
}

// A class change fully replaces which rules apply, so the widget's own style
// fields need a clean slate before re-applying -- unlike LustreStyleApplier's
// own "leave what a style didn't set untouched" behavior (correct for
// merging cascade layers *within* one resolve), a property the *new* class's
// style doesn't set must not keep whatever the *old* class left behind.
void ResetStyleableFields(WidgetBase& Widget) {
    if (auto* AsBox = dynamic_cast<Box*>(&Widget)) {
        AsBox->Style = Penumbra::Widgets::BoxStyle{};
    }
    if (auto* AsButton = dynamic_cast<Button*>(&Widget)) {
        AsButton->ColorBackgroundHovered = {};
        AsButton->ColorBackgroundPressed = {};
        AsButton->ColorBackgroundDisabled = {};
    }
    if (auto* AsLabel = dynamic_cast<Label*>(&Widget)) {
        AsLabel->ColorText = {};
    }
}

} // namespace

PenumbraWidget::PenumbraWidget(std::unique_ptr<WidgetBase> Widget) : OwnedWidget_(std::move(Widget)) {}

PenumbraWidget::PenumbraWidget(WidgetBase* AttachedWidget) : AttachedWidget_(AttachedWidget) {}

WidgetBase* PenumbraWidget::RawWidget() const { return OwnedWidget_ ? OwnedWidget_.get() : AttachedWidget_; }

std::unique_ptr<WidgetBase> PenumbraWidget::DetachOwnership() {
    AttachedWidget_ = OwnedWidget_.get();
    return std::move(OwnedWidget_);
}

void PenumbraWidget::SetImageContext(Penumbra::Backends::IImageBackend* ImageBackend, SDL_Renderer* SdlRenderer) {
    ImageBackend_ = ImageBackend;
    SdlRenderer_ = SdlRenderer;
}

void PenumbraWidget::SetStyleContext(const ::Lustre::StylesheetSet* Sheets, const Lustre::IStyleApplier* StyleApplier) {
    Sheets_ = Sheets;
    StyleApplier_ = StyleApplier;
}

void PenumbraWidget::ApplyPropDiff(const Umbra::IrisPropDiff& Diff) {
    WidgetBase* Widget = RawWidget();
    if (Widget == nullptr) {
        return;
    }

    // The shared set every Core primitive's own Builder exposes identically
    // (docs/iris_core_spec.md §3.1) maps straight onto WidgetBase's own public fields.
    if (Diff.ClassName) {
        Widget->ClassName = *Diff.ClassName;

        // A class change means Lustre's own resolved style for this element may have
        // changed entirely -- re-resolve and re-apply it now, the same way a browser
        // recomputes an element's style the instant its `class` attribute changes.
        // Skipped whenever no style context is configured (SetStyleContext never
        // called, or explicitly given nullptrs) -- exactly the pre-wiring behavior.
        if (Sheets_ != nullptr && StyleApplier_ != nullptr) {
            ResetStyleableFields(*Widget);
            const std::vector<std::unique_ptr<ReconcileStyleElement>> Chain = BuildReconcileStyleChain(*this);
            const ::Lustre::ResolvedStyle Resolved = Lustre::ResolveStyle(*Chain.front(), *Sheets_);
            StyleApplier_->Apply(*Widget, Resolved);
        }
    }
    if (Diff.OnPress) {
        Widget->OnPressed = *Diff.OnPress;
    }
    if (Diff.OnRelease) {
        Widget->OnReleased = *Diff.OnRelease;
    }
    if (Diff.OnHover) {
        Widget->OnHovered = *Diff.OnHover;
    }
    if (Diff.OnFocus) {
        Widget->OnFocused = *Diff.OnFocus;
    }
    if (Diff.OnChange) {
        Widget->OnChanged = *Diff.OnChange;
    }

    // <Text>-only.
    if (Diff.Text) {
        if (auto* AsLabel = dynamic_cast<Label*>(Widget)) {
            AsLabel->Text = *Diff.Text;
        }
    }

    // <Image>-only — src re-decodes synchronously through the real image backend/
    // renderer (docs/iris_core_spec.md §3.1, docs/iris_stage3_decision_doc.md §5's
    // accepted cost). A null ImageBackend_/SdlRenderer_ (no real backend wired up, e.g.
    // a structural test) just skips the reload — FilePath still updates.
    if (Diff.Src) {
        if (auto* AsImage = dynamic_cast<ImageWidget*>(Widget)) {
            AsImage->FilePath = *Diff.Src;
            if (ImageBackend_ != nullptr && SdlRenderer_ != nullptr) {
                AsImage->LoadFrom(*ImageBackend_, SdlRenderer_);
            }
        }
    }

    // Diff.Handle and Diff.Checked are deliberately no-ops here: no Core primitive
    // reaches either path today (Umbra::TextureHandle is currently a data-less stub —
    // nothing to swap; <Checkbox> isn't a Core primitive, docs/iris_core_spec.md §3.1)
    // — see docs/penumbra_ui_backend_adapter_decision.md.
}

std::size_t PenumbraWidget::GetChildCount() const { return Children_.size(); }

Umbra::IWidget* PenumbraWidget::GetChildAt(std::size_t Index) const { return Children_[Index].get(); }

void PenumbraWidget::InsertChildAt(std::size_t Index, std::unique_ptr<Umbra::IWidget> Child) {
    // Safe: this backend is the only thing that ever constructs a Umbra::IWidget, so
    // any IWidget it's handed back is always really a PenumbraWidget.
    auto* ChildImpl = static_cast<PenumbraWidget*>(Child.get());
    ChildImpl->ImageBackend_ = ImageBackend_;
    ChildImpl->SdlRenderer_ = SdlRenderer_;
    ChildImpl->Sheets_ = Sheets_;
    ChildImpl->StyleApplier_ = StyleApplier_;
    ChildImpl->Parent_ = this;

    if (auto* AsBox = dynamic_cast<Box*>(RawWidget())) {
        AsBox->InsertChildAt(Index, ChildImpl->DetachOwnership());
    }
    // If RawWidget() isn't a Box (e.g. <Image>, a leaf), there's no real child slot to
    // place this into — Core primitives never give a leaf primitive children in the
    // first place (docs/iris_core_spec.md §3.1), so this path is unreachable in
    // practice; the wrapper bookkeeping below still stays consistent regardless.
    Children_.insert(Children_.begin() + static_cast<long>(Index),
                      std::unique_ptr<PenumbraWidget>(static_cast<PenumbraWidget*>(Child.release())));
}

std::unique_ptr<Umbra::IWidget> PenumbraWidget::RemoveChildAt(std::size_t Index) {
    std::unique_ptr<PenumbraWidget> Removed = std::move(Children_[static_cast<std::size_t>(Index)]);
    Children_.erase(Children_.begin() + static_cast<long>(Index));
    Removed->Parent_ = nullptr; // detached -- no longer anyone's child until re-inserted

    if (auto* AsBox = dynamic_cast<Box*>(RawWidget())) {
        WidgetBase* RemovedRaw = Removed->RawWidget();
        const auto  It = std::find_if(AsBox->Children.begin(), AsBox->Children.end(),
                                       [&](const std::unique_ptr<WidgetBase>& Owned) { return Owned.get() == RemovedRaw; });
        if (It != AsBox->Children.end()) {
            Removed->OwnedWidget_ = std::move(*It);
            Removed->AttachedWidget_ = nullptr;
            AsBox->Children.erase(It);
        }
    }
    return Removed;
}

void PenumbraWidget::AdoptChildrenFromRawTree(Penumbra::Backends::IImageBackend* ImageBackend,
                                               SDL_Renderer*                       SdlRenderer,
                                               const ::Lustre::StylesheetSet*      Sheets,
                                               const Lustre::IStyleApplier*        StyleApplier,
                                               const PrimitiveTagMap*              Tags) {
    WidgetBase* Raw = RawWidget();
    for (std::size_t Index = 0; Index < Raw->GetChildCount(); ++Index) {
        WidgetBase*                     ChildRaw = Raw->GetChildAt(Index);
        std::unique_ptr<PenumbraWidget> ChildWrapper(new PenumbraWidget(ChildRaw));
        ChildWrapper->SetImageContext(ImageBackend, SdlRenderer);
        ChildWrapper->SetStyleContext(Sheets, StyleApplier);
        ChildWrapper->Parent_ = this;
        if (Tags != nullptr) {
            if (auto It = Tags->find(ChildRaw); It != Tags->end()) {
                ChildWrapper->SetPrimitiveTag(It->second);
            }
        }
        ChildWrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer, Sheets, StyleApplier, Tags);
        Children_.push_back(std::move(ChildWrapper));
    }
}

std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<WidgetBase>         Root,
                                                  Penumbra::Backends::IImageBackend* ImageBackend,
                                                  SDL_Renderer*                       SdlRenderer,
                                                  const ::Lustre::StylesheetSet*      Sheets,
                                                  const Lustre::IStyleApplier*        StyleApplier,
                                                  const PrimitiveTagMap*              Tags) {
    WidgetBase* RawRoot = Root.get();
    auto        Wrapper = std::make_unique<PenumbraWidget>(std::move(Root));
    Wrapper->SetImageContext(ImageBackend, SdlRenderer);
    Wrapper->SetStyleContext(Sheets, StyleApplier);
    if (Tags != nullptr) {
        if (auto It = Tags->find(RawRoot); It != Tags->end()) {
            Wrapper->SetPrimitiveTag(It->second);
        }
    }
    // Wrapper's own Parent_ stays nullptr -- it's the root of this mount, i.e. exactly
    // the component-root boundary BuildReconcileStyleChain's IsComponentRoot() reads.
    Wrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer, Sheets, StyleApplier, Tags);
    return Wrapper;
}

iris::MountFn MakeMountFn(BuildContext Context) {
    return [Context](const Iris::IrisComponent& Node) -> std::unique_ptr<Umbra::IWidget> {
        PrimitiveTagMap             Tags;
        std::unique_ptr<WidgetBase> Built = BuildWidgetTree(Node, Context, &Tags);
        if (!Built) {
            return nullptr;
        }
        return WrapExistingTree(std::move(Built), Context.ImageBackend, Context.SdlRenderer, Context.Style,
                                 Context.StyleApplier, &Tags);
    };
}

} // namespace PenumbraUiBackend
