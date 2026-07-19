#include "PenumbraUiBackend/Walker.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"

#include <optional>
#include <string>
#include <variant>

namespace PenumbraUiBackend {

namespace {

using Iris::IrisComponent;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::WidgetBase;

std::optional<std::string> GetStringProp(const IrisProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::string>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

std::optional<std::function<void()>> GetEventProp(const IrisProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::function<void()>>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

// The five event props plus `class` are the exact shared method set every Box-derived
// primitive's Builder exposes identically (Box::Builder, Label::Builder,
// InlineContainer::Builder — docs/iris_core_spec.md §3.1's "most share the same
// method-naming convention"). Templated rather than duplicated per primitive since none
// of those Builders share a common base class — only the method names line up.
template <typename BuilderT>
void ApplySharedProps(BuilderT& Builder, const IrisProps& Props) {
    if (const auto ClassName = GetStringProp(Props, "class")) {
        Builder.className(*ClassName);
    }
    if (const auto OnPress = GetEventProp(Props, "onPress")) {
        Builder.onPress(*OnPress);
    }
    if (const auto OnRelease = GetEventProp(Props, "onRelease")) {
        Builder.onRelease(*OnRelease);
    }
    if (const auto OnHover = GetEventProp(Props, "onHover")) {
        Builder.onHover(*OnHover);
    }
    if (const auto OnFocus = GetEventProp(Props, "onFocus")) {
        Builder.onFocus(*OnFocus);
    }
    if (const auto OnChange = GetEventProp(Props, "onChange")) {
        Builder.onChange(*OnChange);
    }
}

// Builds every child, skipping any that resolve to `nullptr` (an `IrisElementTag::None`
// child — "no widget here", never added to the parent's Children at all, not even as a
// hole).
template <typename BoxLikeBuilder>
void BuildAndAttachChildren(BoxLikeBuilder& Builder, const IrisComponent& Node, const BuildContext& Context) {
    for (const IrisComponent& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTree(Child, Context)) {
            Builder.child(std::move(ChildWidget));
        }
    }
}

std::unique_ptr<WidgetBase> BuildFrame(const IrisComponent& Node, const BuildContext& Context) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context);
    return Builder.build();
}

// docs/iris_stage2_decision_doc.md §3 / docs/iris_core_spec.md §3.1: Penumbra has no
// grid layout mode — `<Grid>` maps onto a plain `Box` with `LayoutMode::HorizontalStack`
// as a stub, which explicitly does not meet the Core-primitive requirement yet. `Layout`
// is a public field on `Box`, not something `Box::Builder` exposes — there's no
// `layout()` Builder method to chain, so it's set directly on the built widget.
std::unique_ptr<WidgetBase> BuildGrid(const IrisComponent& Node, const BuildContext& Context) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context);
    std::unique_ptr<Box> Built = Builder.build();
    Built->Layout = Penumbra::Widgets::LayoutMode::HorizontalStack;
    return Built;
}

std::unique_ptr<WidgetBase> BuildInline(const IrisComponent& Node, const BuildContext& Context) {
    InlineContainer::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context);
    return Builder.build();
}

// `<Text>`'s content lands in its own `"text"` prop, not a child
// (docs/iris_stage1_codegen_decision.md, Gap 2) — Codegen already concatenated it into
// one string, so there's nothing to recurse into here, just `text()` on the Builder.
// `Label::Builder` has no way to set `FontBackend`/`Font` (Penumbra's own demo sets
// both as plain fields after construction — there's no Builder method for either), so
// that happens here too, from `Context`.
std::unique_ptr<WidgetBase> BuildText(const IrisComponent& Node, const BuildContext& Context) {
    Label::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    if (const auto Text = GetStringProp(Node.Props, "text")) {
        Builder.text(*Text);
    }
    std::unique_ptr<Label> Built = Builder.build();
    Built->FontBackend = Context.FontBackend;
    Built->Font = Context.Font;
    return Built;
}

// `<Image>` is a leaf with a deliberately narrow Builder (no child()/children(), no
// event props — docs/iris_core_spec.md §3.1) and loading is not part of build() at all:
// `ImageWidget::LoadFrom()` must be called separately once a real image/SDL backend is
// available, which is exactly what `Context.ImageBackend`/`Context.SdlRenderer` are
// for. Building still succeeds with either left null (e.g. a structural test with no
// real backend) — the widget just never gets a texture loaded.
std::unique_ptr<WidgetBase> BuildImage(const IrisComponent& Node, const BuildContext& Context) {
    ImageWidget::Builder Builder;
    if (const auto ClassName = GetStringProp(Node.Props, "class")) {
        Builder.className(*ClassName);
    }
    if (const auto Src = GetStringProp(Node.Props, "src")) {
        Builder.src(*Src);
    }
    std::unique_ptr<ImageWidget> Built = Builder.build();
    if (Context.ImageBackend != nullptr && Context.SdlRenderer != nullptr) {
        Built->LoadFrom(*Context.ImageBackend, Context.SdlRenderer);
    }
    return Built;
}

} // namespace

std::unique_ptr<WidgetBase> BuildWidgetTree(const IrisComponent& Node, const BuildContext& Context) {
    switch (Node.Tag) {
        case IrisElementTag::None:
            return nullptr;
        case IrisElementTag::Slot:
            // Contributes nothing during this static build, same as None — a <Slot>
            // child is spliced in afterward by iris::ResolveSlots (Iris/SlotResolution.h,
            // docs/iris_slot_stage2_wiring_decision.md), which needs the surrounding
            // static tree to already exist (with the Slot's own position simply absent)
            // before it can attach real content there. A <Slot> that's the very ROOT of
            // what's being built (no static wrapper at all) is a different case, handled
            // by the caller via a plain, unattached iris::SlotState instead — see that
            // decision doc.
            return nullptr;
        case IrisElementTag::Frame:
            return BuildFrame(Node, Context);
        case IrisElementTag::Grid:
            return BuildGrid(Node, Context);
        case IrisElementTag::Inline:
            return BuildInline(Node, Context);
        case IrisElementTag::Image:
            return BuildImage(Node, Context);
        case IrisElementTag::Text:
            return BuildText(Node, Context);
    }
    return nullptr; // unreachable — every IrisElementTag is handled above
}

} // namespace PenumbraUiBackend
