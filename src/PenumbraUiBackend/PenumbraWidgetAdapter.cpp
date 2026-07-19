#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/Label.h"

#include <algorithm>

namespace PenumbraUiBackend {

using Penumbra::Widgets::Box;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::WidgetBase;

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

void PenumbraWidget::ApplyPropDiff(const Umbra::IrisPropDiff& Diff) {
    WidgetBase* Widget = RawWidget();
    if (Widget == nullptr) {
        return;
    }

    // The shared set every Core primitive's own Builder exposes identically
    // (docs/iris_core_spec.md §3.1) maps straight onto WidgetBase's own public fields.
    if (Diff.ClassName) {
        Widget->ClassName = *Diff.ClassName;
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
                                               SDL_Renderer*                       SdlRenderer) {
    WidgetBase* Raw = RawWidget();
    for (std::size_t Index = 0; Index < Raw->GetChildCount(); ++Index) {
        std::unique_ptr<PenumbraWidget> ChildWrapper(new PenumbraWidget(Raw->GetChildAt(Index)));
        ChildWrapper->SetImageContext(ImageBackend, SdlRenderer);
        ChildWrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer);
        Children_.push_back(std::move(ChildWrapper));
    }
}

std::unique_ptr<PenumbraWidget> WrapExistingTree(std::unique_ptr<WidgetBase>         Root,
                                                  Penumbra::Backends::IImageBackend* ImageBackend,
                                                  SDL_Renderer*                       SdlRenderer) {
    auto Wrapper = std::make_unique<PenumbraWidget>(std::move(Root));
    Wrapper->SetImageContext(ImageBackend, SdlRenderer);
    Wrapper->AdoptChildrenFromRawTree(ImageBackend, SdlRenderer);
    return Wrapper;
}

iris::MountFn MakeMountFn(BuildContext Context) {
    return [Context](const Iris::IrisComponent& Node) -> std::unique_ptr<Umbra::IWidget> {
        std::unique_ptr<WidgetBase> Built = BuildWidgetTree(Node, Context);
        if (!Built) {
            return nullptr;
        }
        return WrapExistingTree(std::move(Built), Context.ImageBackend, Context.SdlRenderer);
    };
}

} // namespace PenumbraUiBackend
