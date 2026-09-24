#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "PenumbraUiBackend/Lustre/StyleResolution.h"
#include "PenumbraUiBackend/Portal.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/SplitPanel.h"
#include "Penumbra/Widgets/TextInput.h"

#include <algorithm>

namespace PenumbraUiBackend {

using Penumbra::Widgets::Box;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::SplitPanel;
using Penumbra::Widgets::TextInput;
using Penumbra::Widgets::WidgetBase;

namespace {

class PortalOverlaySurface final : public WidgetBase {
public:
    PortalOverlaySurface(std::function<WidgetBase*()> GetContent, std::function<void()> OnDestroyed)
        : GetContent_(std::move(GetContent)), OnDestroyed_(std::move(OnDestroyed)) {}
    ~PortalOverlaySurface() override {
        if (OnDestroyed_) OnDestroyed_();
    }

    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override {
        WidgetBase* Content = GetContent_();
        return Content ? Content->Measure(AvailableSizeLogical) : Penumbra::Point{};
    }
    void Arrange(Penumbra::Rect FinalRectLogical) override {
        ArrangedRect = FinalRectLogical;
        if (WidgetBase* Content = GetContent_()) Content->Arrange(FinalRectLogical);
    }
    bool UpdateInteractionState(const Penumbra::Platform::InputState& Input) override {
        WidgetBase* Content = GetContent_();
        return Content ? Content->UpdateInteractionState(Input) : false;
    }
    void Draw(Penumbra::Render::Renderer& Renderer) override {
        if (WidgetBase* Content = GetContent_()) Content->Draw(Renderer);
    }
    std::size_t GetChildCount() const override { return GetContent_() ? 1u : 0u; }
    WidgetBase* GetChildAt(std::size_t Index) const override {
        return Index == 0 ? GetContent_() : nullptr;
    }

private:
    std::function<WidgetBase*()> GetContent_;
    std::function<void()> OnDestroyed_;
};

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
    if (auto* AsLabel = dynamic_cast<Label*>(&Widget)) {
        AsLabel->ColorText = {};
    }
    if (auto* AsTextInput = dynamic_cast<TextInput*>(&Widget)) {
        AsTextInput->ColorText = {};
        AsTextInput->ColorCaret = {};
        AsTextInput->ColorSelection = {};
    }
}

} // namespace

struct PortalAnchorWidget::State : std::enable_shared_from_this<PortalAnchorWidget::State> {
    Penumbra::Widgets::OverlayHost* Host{nullptr};
    Penumbra::Widgets::OverlayId    Id{0};
    PortalAnchorWidget*             Anchor{nullptr};
    Iris::IPortalTarget*            Adapter{nullptr};
    Iris::PortalProperties          Properties;
    bool                            SuppressDismiss{false};
    bool                            HostDismissInProgress{false};
    bool                            Prepared{false};

    Penumbra::Rect Placement() const {
        return {Properties.X, Properties.Y, Properties.Width, Properties.Height};
    }

    void Show() {
        if (Host == nullptr || Anchor == nullptr || Anchor->Children.empty()) return;
        auto Self = shared_from_this();
        auto Surface = std::make_unique<PortalOverlaySurface>(
            [Self]() -> WidgetBase* {
                return Self->Anchor != nullptr && !Self->Anchor->Children.empty()
                    ? Self->Anchor->Children.front().get()
                    : nullptr;
            },
            [Self]() { Self->OverlayDestroyed(); });
        Id = Host->ShowOverlay(std::move(Surface), Placement(), Properties.DismissOnOutsideClick);
    }

    void OverlayDestroyed() {
        Id = 0;
        if (SuppressDismiss) return;

        const std::function<void()> OnDismiss = Properties.OnDismiss;
        HostDismissInProgress = true;
        if (Adapter != nullptr) Iris::PreparePortalSubtreeForUnmount(dynamic_cast<Umbra::IWidget*>(Adapter));
        HostDismissInProgress = false;
        if (OnDismiss) OnDismiss();
    }

    void DismissWithoutNotification() {
        if (Host == nullptr || Id == 0) return;
        SuppressDismiss = true;
        const auto CurrentId = Id;
        Id = 0;
        Host->DismissOverlay(CurrentId);
        SuppressDismiss = false;
    }
};

PortalAnchorWidget::PortalAnchorWidget(Penumbra::Widgets::OverlayHost* Host, std::unique_ptr<WidgetBase> Content,
                                       const Iris::PortalProperties& Properties) {
    State_ = std::make_shared<State>();
    State_->Host = Host;
    State_->Anchor = this;
    State_->Properties = Properties;
    if (Content) AddChild(std::move(Content));
    State_->Show();
}

PortalAnchorWidget::~PortalAnchorWidget() {
    State_->Anchor = nullptr;
    State_->DismissWithoutNotification();
}

void PortalAnchorWidget::ApplyPortalProperties(const Iris::PortalProperties& Properties) {
    const bool DismissModeChanged = State_->Properties.DismissOnOutsideClick != Properties.DismissOnOutsideClick;
    State_->Properties = Properties;
    State_->Prepared = false;
    if (DismissModeChanged && State_->Id != 0) {
        State_->DismissWithoutNotification();
        State_->Show();
    } else if (State_->Host != nullptr && State_->Id != 0) {
        State_->Host->SetOverlayPlacement(State_->Id, State_->Placement());
    } else {
        State_->Show();
    }
}

void PortalAnchorWidget::AttachAdapter(Iris::IPortalTarget* Adapter) { State_->Adapter = Adapter; }
void PortalAnchorWidget::DetachAdapter(Iris::IPortalTarget* Adapter) {
    if (State_->Adapter == Adapter) State_->Adapter = nullptr;
}

void PortalAnchorWidget::PreparePortalUnmount() {
    if (State_->Prepared) return;
    State_->Prepared = true;
    if (!State_->HostDismissInProgress) State_->DismissWithoutNotification();
}

Penumbra::Point PortalAnchorWidget::Measure(Penumbra::Point) { return {}; }
void PortalAnchorWidget::Arrange(Penumbra::Rect FinalRectLogical) { ArrangedRect = FinalRectLogical; }
bool PortalAnchorWidget::UpdateInteractionState(const Penumbra::Platform::InputState&) { return false; }
void PortalAnchorWidget::Draw(Penumbra::Render::Renderer&) {}

PenumbraPortalWidget::PenumbraPortalWidget(std::unique_ptr<WidgetBase> Widget)
    : PenumbraWidget(std::move(Widget)), Anchor_(dynamic_cast<PortalAnchorWidget*>(RawWidget())) {
    if (Anchor_) Anchor_->AttachAdapter(this);
}

PenumbraPortalWidget::PenumbraPortalWidget(PortalAnchorWidget* Widget)
    : PenumbraWidget(Widget), Anchor_(Widget) {
    if (Anchor_) Anchor_->AttachAdapter(this);
}

PenumbraPortalWidget::~PenumbraPortalWidget() {
    if (Anchor_) Anchor_->DetachAdapter(this);
}

void PenumbraPortalWidget::ApplyPortalProperties(const Iris::PortalProperties& Properties) {
    if (Anchor_) Anchor_->ApplyPortalProperties(Properties);
}

void PenumbraPortalWidget::PreparePortalUnmount() {
    if (Anchor_) Anchor_->PreparePortalUnmount();
}

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

Umbra::IWidget* PenumbraWidget::GetByRef(std::string_view Ref) const {
    const PenumbraWidget* Root = this;
    while (Root->Parent_ != nullptr) {
        Root = Root->Parent_;
    }
    const auto It = Root->RefRegistry_.find(std::string(Ref));
    return It != Root->RefRegistry_.end() ? It->second : nullptr;
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

    // <Input>-only -- OnTextChanged lives on TextInput specifically, not WidgetBase
    // (unlike OnPress/OnRelease/OnHover/OnFocus/OnChange above), same dynamic_cast
    // guard the Text/Src branches around this one already use.
    if (Diff.OnTextChange) {
        if (auto* AsTextInput = dynamic_cast<TextInput*>(Widget)) {
            std::function<void(std::string)> Callback = *Diff.OnTextChange;
            AsTextInput->OnTextChanged = [Callback](const std::string& NewText) { Callback(NewText); };
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

    // docs/next_steps.md's "swap a live real widget when a reconciled `<Native>`
    // re-renders" ask -- a real, live gap found investigating it, not hypothetical:
    // SplitPanel has exactly two fixed slots addressed by structural position (Index 0
    // == First, Index 1 == Second -- Codegen guarantees a real <Split> always has
    // exactly two children in this order, docs/native_split_backend_wiring_gap.md), not
    // the generic Box::Children vector it inherits but never actually uses. Must be
    // checked *before* the generic Box fallback below: SplitPanel : Box, so without this
    // branch the cast below would silently succeed and write into a vector SplitPanel's
    // own Draw/Arrange/GetChildAt never look at -- the widget would be genuinely
    // invisible and unreachable, not just misplaced. Whatever already occupies the
    // target slot (if anything) is destroyed as a side effect of SetFirst/SetSecond's
    // own plain unique_ptr move-assignment -- correct and expected for the paired
    // RemoveChildAt-then-InsertChildAt pattern `iris::ReconcileChildrenAt` actually uses
    // (the slot is already empty by the time this runs, see RemoveChildAt's own
    // comment), not safe for a bare InsertChildAt into an already-occupied slot with no
    // matching prior removal -- SplitPanel has no `Box::ReplaceChild`-equivalent to fall
    // back on for that case (this repo's own docs/next_steps.md carries the precise
    // upstream `penumbra` ask this stands in for).
    if (auto* AsSplit = dynamic_cast<SplitPanel*>(RawWidget())) {
        if (Index == 0) {
            AsSplit->SetFirst(ChildImpl->DetachOwnership());
        } else {
            AsSplit->SetSecond(ChildImpl->DetachOwnership());
        }
    } else if (auto* AsBox = dynamic_cast<Box*>(RawWidget())) {
        AsBox->InsertChildAt(Index, ChildImpl->DetachOwnership());
    }
    // If RawWidget() is neither of the above (e.g. <Image>, a leaf), there's no real
    // child slot to place this into — Core primitives never give a leaf primitive
    // children in the first place (docs/iris_core_spec.md §3.1), so this path is
    // unreachable in practice; the wrapper bookkeeping below still stays consistent
    // regardless.
    Children_.insert(Children_.begin() + static_cast<long>(Index),
                      std::unique_ptr<PenumbraWidget>(static_cast<PenumbraWidget*>(Child.release())));
}

std::unique_ptr<Umbra::IWidget> PenumbraWidget::RemoveChildAt(std::size_t Index) {
    std::unique_ptr<PenumbraWidget> Removed = std::move(Children_[static_cast<std::size_t>(Index)]);
    Children_.erase(Children_.begin() + static_cast<long>(Index));
    Removed->Parent_ = nullptr; // detached -- no longer anyone's child until re-inserted

    if (auto* AsSplit = dynamic_cast<SplitPanel*>(RawWidget())) {
        // See InsertChildAt's own comment -- Index 0/1 map onto First/Second by
        // structural position, checked before the generic Box fallback for the same
        // "SplitPanel : Box would otherwise silently take the wrong branch" reason.
        // SetFirst/SetSecond(nullptr) destroys whatever was there as a side effect --
        // unlike the Box path below (Box::ReplaceChild hands the removed widget back
        // intact), there is no way to reclaim it here, so Removed must report no widget
        // at all afterward rather than a now-dangling AttachedWidget_ pointer.
        if (Index == 0) {
            AsSplit->SetFirst(nullptr);
        } else {
            AsSplit->SetSecond(nullptr);
        }
        Removed->OwnedWidget_.reset();
        Removed->AttachedWidget_ = nullptr;
    } else if (auto* AsBox = dynamic_cast<Box*>(RawWidget())) {
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

void PenumbraWidget::AdoptChildrenFromRawTree(
    Penumbra::Backends::IImageBackend* ImageBackend, SDL_Renderer* SdlRenderer, const ::Lustre::StylesheetSet* Sheets,
    const Lustre::IStyleApplier* StyleApplier, const PrimitiveTagMap* Tags,
    const std::unordered_map<const WidgetBase*, std::string>* ReverseRefs, PenumbraWidget* RegistryRoot) {
    WidgetBase* Raw = RawWidget();
    for (std::size_t Index = 0; Index < Raw->GetChildCount(); ++Index) {
        WidgetBase*                     ChildRaw = Raw->GetChildAt(Index);
        std::unique_ptr<PenumbraWidget> ChildWrapper;
        if (auto* Portal = dynamic_cast<PortalAnchorWidget*>(ChildRaw)) {
            ChildWrapper = std::make_unique<PenumbraPortalWidget>(Portal);
        } else {
            ChildWrapper = std::unique_ptr<PenumbraWidget>(new PenumbraWidget(ChildRaw));
        }
        ChildWrapper->SetImageContext(ImageBackend, SdlRenderer);
        ChildWrapper->SetStyleContext(Sheets, StyleApplier);
        ChildWrapper->Parent_ = this;
        if (Tags != nullptr) {
            if (auto It = Tags->find(ChildRaw); It != Tags->end()) {
                ChildWrapper->SetPrimitiveTag(It->second);
            }
        }
        if (ReverseRefs != nullptr) {
            if (auto It = ReverseRefs->find(ChildRaw); It != ReverseRefs->end()) {
                RegistryRoot->RefRegistry_[It->second] = ChildWrapper.get();
            }
        }
        ChildWrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer, Sheets, StyleApplier, Tags, ReverseRefs,
                                                RegistryRoot);
        Children_.push_back(std::move(ChildWrapper));
    }
}

std::unique_ptr<WidgetBase> PenumbraWidget::ReplaceRawWidget(std::unique_ptr<WidgetBase> NewWidget,
                                                               const PrimitiveTagMap* Tags, const RefMap* Refs) {
    WidgetBase* OldRaw = RawWidget();
    WidgetBase* NewRaw = NewWidget.get();

    std::unique_ptr<WidgetBase> Old;
    if (Parent_ == nullptr) {
        // Mount root: no real parent container to thread through -- this wrapper owns
        // its widget directly, so just swap what it owns.
        Old = std::move(OwnedWidget_);
        OwnedWidget_ = std::move(NewWidget);
        AttachedWidget_ = nullptr;
    } else {
        WidgetBase* ParentRaw = Parent_->RawWidget();
        if (auto* AsSplit = dynamic_cast<SplitPanel*>(ParentRaw)) {
            // See this method's own header comment -- SplitPanel has no
            // ReplaceChild-style primitive, so the widget being swapped out can't be
            // handed back intact here; SetFirst/SetSecond destroy it immediately.
            //
            // Addressed by this wrapper's own structural position among
            // Parent_->Children_ (0 == First, 1 == Second, same convention
            // InsertChildAt/RemoveChildAt use), not by matching OldRaw against
            // SplitPanel::GetChildAt's own *compacted* enumeration (which collapses an
            // empty First away, so GetChildAt(0) can legitimately mean "Second" -- would
            // misidentify the slot whenever First is empty and Second isn't).
            const auto OwnIt =
                std::find_if(Parent_->Children_.begin(), Parent_->Children_.end(),
                             [&](const std::unique_ptr<PenumbraWidget>& Sibling) { return Sibling.get() == this; });
            const bool IsFirst = OwnIt == Parent_->Children_.begin();
            if (IsFirst) {
                AsSplit->SetFirst(std::move(NewWidget));
            } else {
                AsSplit->SetSecond(std::move(NewWidget));
            }
            Old = nullptr;
        } else if (auto* AsBox = dynamic_cast<Box*>(ParentRaw)) {
            Old = AsBox->ReplaceChild(OldRaw, std::move(NewWidget));
        } else {
            // Every real container a live wrapper's Parent_ can point at is either a
            // Box or a SplitPanel (Core primitives never give a leaf primitive
            // children, docs/iris_core_spec.md §3.1) -- unreachable in practice, same
            // tolerance InsertChildAt's own analogous "not a Box" branch already has.
            return nullptr;
        }
        OwnedWidget_.reset();
        AttachedWidget_ = NewRaw;
    }

    if (Tags != nullptr) {
        if (auto It = Tags->find(NewRaw); It != Tags->end()) {
            SetPrimitiveTag(It->second);
        }
    }

    // This wrapper's own Children_ pointed into the now-detached old subtree -- rebuild
    // from NewRaw's real structure, same as WrapExistingTree's own initial adoption
    // step, propagating this wrapper's existing image/style context to the new
    // children exactly the way it already does for an initial wrap.
    Children_.clear();

    std::unordered_map<const WidgetBase*, std::string> ReverseRefs;
    PenumbraWidget*                                     RegistryRoot = nullptr;
    if (Refs != nullptr) {
        RegistryRoot = this;
        while (RegistryRoot->Parent_ != nullptr) {
            RegistryRoot = RegistryRoot->Parent_;
        }
        for (const auto& [Name, Widget] : *Refs) {
            ReverseRefs[Widget] = Name;
        }
        if (auto It = ReverseRefs.find(NewRaw); It != ReverseRefs.end()) {
            RegistryRoot->RefRegistry_[It->second] = this;
        }
    }
    AdoptChildrenFromRawTree(ImageBackend_, SdlRenderer_, Sheets_, StyleApplier_, Tags,
                              Refs != nullptr ? &ReverseRefs : nullptr, RegistryRoot);

    return Old;
}

std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<WidgetBase>         Root,
                                                  Penumbra::Backends::IImageBackend* ImageBackend,
                                                  SDL_Renderer*                       SdlRenderer,
                                                  const ::Lustre::StylesheetSet*      Sheets,
                                                  const Lustre::IStyleApplier*        StyleApplier,
                                                  const PrimitiveTagMap*              Tags,
                                                  const RefMap*                       Refs) {
    WidgetBase* RawRoot = Root.get();
    std::unique_ptr<PenumbraWidget> Wrapper;
    if (dynamic_cast<PortalAnchorWidget*>(RawRoot) != nullptr) {
        Wrapper = std::make_unique<PenumbraPortalWidget>(std::move(Root));
    } else {
        Wrapper = std::make_unique<PenumbraWidget>(std::move(Root));
    }
    Wrapper->SetImageContext(ImageBackend, SdlRenderer);
    Wrapper->SetStyleContext(Sheets, StyleApplier);
    if (Tags != nullptr) {
        if (auto It = Tags->find(RawRoot); It != Tags->end()) {
            Wrapper->SetPrimitiveTag(It->second);
        }
    }

    // Refs is (ref name -> raw WidgetBase*); the walk below matches by raw pointer as it
    // encounters each wrapper, so it needs the inverse -- built once here rather than
    // per-node, same one-time-cost shape BuildReconcileStyleChain elsewhere in this file
    // already accepts for its own per-call bookkeeping.
    std::unordered_map<const WidgetBase*, std::string> ReverseRefs;
    if (Refs != nullptr) {
        for (const auto& [Name, Widget] : *Refs) {
            ReverseRefs[Widget] = Name;
        }
        if (auto It = ReverseRefs.find(RawRoot); It != ReverseRefs.end()) {
            Wrapper->RefRegistry_[It->second] = Wrapper.get();
        }
    }

    // Wrapper's own Parent_ stays nullptr -- it's the root of this mount, i.e. exactly
    // the component-root boundary BuildReconcileStyleChain's IsComponentRoot() reads,
    // and GetByRef()'s own "registry only lives on the root" contract.
    Wrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer, Sheets, StyleApplier, Tags,
                                       Refs != nullptr ? &ReverseRefs : nullptr, Wrapper.get());
    return Wrapper;
}

iris::MountFn MakeMountFn(BuildContext Context) {
    return [Context](const Iris::Component& Node) -> std::unique_ptr<Umbra::IWidget> {
        PrimitiveTagMap             Tags;
        RefMap                      Refs;
        std::unique_ptr<WidgetBase> Built = BuildWidgetTree(Node, Context, &Tags, &Refs);
        if (!Built) {
            return nullptr;
        }
        return WrapExistingTree(std::move(Built), Context.ImageBackend, Context.SdlRenderer, Context.Style,
                                 Context.StyleApplier, &Tags, &Refs);
    };
}

} // namespace PenumbraUiBackend
