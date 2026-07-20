#include "PenumbraUiBackend/Walker.h"

#include "PenumbraUiBackend/Lustre/StyleResolution.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/TextInput.h"

#include <cstdio>
#include <optional>
#include <string>
#include <variant>

namespace PenumbraUiBackend {

namespace {

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::ScrollablePanel;
using Penumbra::Widgets::TextInput;
using Penumbra::Widgets::WidgetBase;

// docs/build_context_style_mismatch_gap.md: debug-mode tracking for "did any
// node in this BuildWidgetTree() call actually resolve a property from the
// given Context.Style" -- a component built through a BuildContext whose
// Style points at the wrong StylesheetSet resolves nothing at all, silently
// (Lustre::ResolveStyle's own contract: no rule matched is not an error).
// Threaded through the recursion as a nullable pointer (nullptr in the
// common non-debug-tooling path) rather than added to BuildContext itself --
// this is call-scoped bookkeeping for one BuildWidgetTree() invocation, not
// caller configuration.
struct StyleMatchStats {
    std::size_t ClassedNodes = 0;
    std::size_t ResolvedNodes = 0;
};

// Whether Style carries literally nothing -- every field still at its
// default-constructed std::nullopt/empty-shared_ptr. Enumerates every
// ResolvedStyle field by hand, same as StyleResolution.cpp's own
// MergeInto() already does for the same struct (no built-in "is this
// empty" predicate exists on ResolvedStyle itself -- lustre/include/Lustre/
// ResolvedStyle.h). Pseudo-class overlays count too: a rule that only ever
// sets e.g. `:hover { background-color: ... }` with no base declaration
// still means *something* real matched.
bool ResolvedStyleIsEmpty(const ::Lustre::ResolvedStyle& Style) {
    return !Style.BackgroundColor && !Style.BackgroundGradientStart && !Style.BackgroundGradientEnd &&
           !Style.BorderColor && !Style.BorderWidth && !Style.BorderRadius && !Style.Padding && !Style.Margin &&
           !Style.TextColor && !Style.Font && !Style.DisplayMode && !Style.FlexDirectionMode && !Style.Gap &&
           !Style.AlignItems && !Style.Transition && !Style.Hover && !Style.Active && !Style.Disabled &&
           !Style.WidthLogical && !Style.HeightLogical && !Style.TransformScale && !Style.MaxWidthLogical &&
           !Style.TextOverflowMode;
}

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

std::optional<float> GetFloatProp(const IrisProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<float>(&It->second)) {
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

// <Scroll>/<Input> both build to a WidgetBase subclass with no Builder of its own
// (ScrollablePanel.h/TextInput.h -- unlike every other Core primitive here). Shared
// props (class, event props) are WidgetBase's own plain public fields
// (docs/iris_core_spec.md §3.1's `<Icon>` entry already established this "public
// field, not a Builder chain" pattern for the pieces without one), so this mirrors
// ApplySharedProps's own five-events-plus-class set by hand rather than through a
// templated Builder call.
void ApplySharedPropsToWidget(WidgetBase& Widget, const IrisProps& Props) {
    if (const auto ClassName = GetStringProp(Props, "class")) {
        Widget.ClassName = *ClassName;
    }
    if (const auto OnPress = GetEventProp(Props, "onPress")) {
        Widget.OnPressed = *OnPress;
    }
    if (const auto OnRelease = GetEventProp(Props, "onRelease")) {
        Widget.OnReleased = *OnRelease;
    }
    if (const auto OnHover = GetEventProp(Props, "onHover")) {
        Widget.OnHovered = *OnHover;
    }
    if (const auto OnFocus = GetEventProp(Props, "onFocus")) {
        Widget.OnFocused = *OnFocus;
    }
    if (const auto OnChange = GetEventProp(Props, "onChange")) {
        Widget.OnChanged = *OnChange;
    }
}

// docs/lustre_core_spec.md §1.1's mapping table, keyed the other direction (a Core tag
// to the PascalCase string Lustre::IStyleTarget::PrimitiveTag() reports -- these already
// match one-for-one, this just names the IrisElementTag values Lustre's own selector
// resolution doesn't know about).
std::string IrisTagToLustreTag(IrisElementTag Tag) {
    switch (Tag) {
        case IrisElementTag::Frame: return "Frame";
        case IrisElementTag::Inline: return "Inline";
        case IrisElementTag::Grid: return "Grid";
        case IrisElementTag::Image: return "Image";
        case IrisElementTag::Icon: return "Icon";
        case IrisElementTag::Text: return "Text";
        case IrisElementTag::Scroll: return "Scroll";
        case IrisElementTag::Input: return "Input";
        default: return ""; // None/Slot never reach here -- see BuildWidgetTreeInternal
    }
}

// Lustre::IStyleTarget over the ancestor chain BuildWidgetTree's own recursion already
// walks -- no separate tree walk needed, since resolving a node's style happens exactly
// once, right when that node is built, with every ancestor's WalkerStyleElement still
// alive on the call stack above it. `IsComponentRoot_` is true exactly for the node a
// given `BuildWidgetTree()` call started at (see that function's own doc comment for why
// that's the correct boundary signal today).
class WalkerStyleElement : public ::Lustre::IStyleTarget {
public:
    WalkerStyleElement(std::string ClassName, std::string PrimitiveTag, bool IsComponentRoot,
                        const WalkerStyleElement* Parent)
        : ClassName_(std::move(ClassName)), PrimitiveTag_(std::move(PrimitiveTag)), IsComponentRoot_(IsComponentRoot),
          Parent_(Parent) {}

    std::string ClassName() const override { return ClassName_; }
    std::string PrimitiveTag() const override { return PrimitiveTag_; }
    bool         IsComponentRoot() const override { return IsComponentRoot_; }
    const ::Lustre::IStyleTarget* Parent() const override { return Parent_; }

private:
    std::string ClassName_;
    std::string PrimitiveTag_;
    bool        IsComponentRoot_;
    const WalkerStyleElement* Parent_;
};

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, PrimitiveTagMap* OutTags,
                                                     StyleMatchStats* Stats);

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
// hole). `ThisStyleElement` is Node's own style-target wrapper, already constructed by
// the caller -- passed down so every child's WalkerStyleElement can point back to it as
// their ancestor.
template <typename BoxLikeBuilder>
void BuildAndAttachChildren(BoxLikeBuilder& Builder, const Component& Node, const BuildContext& Context,
                             const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                             StyleMatchStats* Stats) {
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, OutTags, Stats)) {
            Builder.child(std::move(ChildWidget));
        }
    }
}

std::unique_ptr<WidgetBase> BuildFrame(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                        StyleMatchStats* Stats) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, Stats);
    return Builder.build();
}

// docs/iris_stage2_decision_doc.md §3 / docs/iris_core_spec.md §3.1: Penumbra has no
// grid layout mode — `<Grid>` maps onto a plain `Box` with `LayoutMode::HorizontalStack`
// as a stub, which explicitly does not meet the Core-primitive requirement yet. `Layout`
// is a public field on `Box`, not something `Box::Builder` exposes — there's no
// `layout()` Builder method to chain, so it's set directly on the built widget.
std::unique_ptr<WidgetBase> BuildGrid(const Component& Node, const BuildContext& Context,
                                       const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                       StyleMatchStats* Stats) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, Stats);
    std::unique_ptr<Box> Built = Builder.build();
    Built->Layout = Penumbra::Widgets::LayoutMode::HorizontalStack;
    return Built;
}

std::unique_ptr<WidgetBase> BuildInline(const Component& Node, const BuildContext& Context,
                                         const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                         StyleMatchStats* Stats) {
    InlineContainer::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, Stats);
    return Builder.build();
}

// `<Text>`'s content lands in its own `"text"` prop, not a child
// (docs/iris_stage1_codegen_decision.md, Gap 2) — Codegen already concatenated it into
// one string, so there's nothing to recurse into here, just `text()` on the Builder.
// `Label::Builder` has no way to set `FontBackend`/`Font` (Penumbra's own demo sets
// both as plain fields after construction — there's no Builder method for either), so
// that happens here too, from `Context`.
std::unique_ptr<WidgetBase> BuildText(const Component& Node, const BuildContext& Context) {
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
std::unique_ptr<WidgetBase> BuildImage(const Component& Node, const BuildContext& Context) {
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

// `<Icon>` is a leaf with a deliberately narrow Builder (no child()/children(), no
// event props — docs/iris_core_spec.md §3.1, same shape as `<Image>`). Unlike
// `<Image>` there is no separate load step: `IconWidget::IconBackend` is a plain
// public field (mirroring how `Label::FontBackend`/`Font` are already set directly
// below in BuildText, not through the Builder), set here from `Context.IconBackend` so
// Draw() can resolve the icon name fresh every frame. Building still succeeds with
// `Context.IconBackend` left null (e.g. a structural test with no real backend) — the
// widget just never draws anything. `size`, if present, overrides
// `IconWidget::SizeLogical`'s 16px default (docs/iris_core_spec.md §3.1) — omitted
// leaves the built widget at that default, same "absent prop -> builder method never
// called" treatment every other optional prop here gets.
std::unique_ptr<WidgetBase> BuildIcon(const Component& Node, const BuildContext& Context) {
    IconWidget::Builder Builder;
    if (const auto ClassName = GetStringProp(Node.Props, "class")) {
        Builder.className(*ClassName);
    }
    if (const auto Icon = GetStringProp(Node.Props, "icon")) {
        Builder.icon(*Icon);
    }
    if (const auto Size = GetFloatProp(Node.Props, "size")) {
        Builder.size(*Size);
    }
    std::unique_ptr<IconWidget> Built = Builder.build();
    Built->IconBackend = Context.IconBackend;
    return Built;
}

// <Scroll> (docs/iris_core_spec.md §3.1) -- element children only, same as <Frame>, but
// ScrollablePanel has no Builder to route them through: AddChild directly, same "plain
// field/method, not a Builder chain" treatment ApplySharedPropsToWidget above already
// gives its shared props. wheelStep maps onto WheelStepLogical, the one dedicated field
// this widget has.
std::unique_ptr<WidgetBase> BuildScroll(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                        StyleMatchStats* Stats) {
    auto Built = std::make_unique<ScrollablePanel>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto WheelStep = GetFloatProp(Node.Props, "wheelStep")) {
        Built->WheelStepLogical = *WheelStep;
    }
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, OutTags, Stats)) {
            Built->AddChild(std::move(ChildWidget));
        }
    }
    return Built;
}

// <Input> (docs/iris_core_spec.md §3.1) -- a leaf, same shape as <Icon>: TextInput has
// no Builder either. FontBackend/Font are set directly from Context, the same
// "Label::Builder has no method for it" treatment BuildText below already uses; Focus/
// Clipboard likewise come straight from Context (both may be null -- an inert but
// still-built widget, same tolerance BuildImage/BuildIcon already have for their own
// optional backend pointers).
std::unique_ptr<WidgetBase> BuildInput(const Component& Node, const BuildContext& Context) {
    auto Built = std::make_unique<TextInput>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto Text = GetStringProp(Node.Props, "text")) {
        Built->Text = *Text;
    }
    if (const auto PreferredWidth = GetFloatProp(Node.Props, "preferredWidth")) {
        Built->PreferredWidthLogical = *PreferredWidth;
    }
    Built->FontBackend = Context.FontBackend;
    Built->Font = Context.Font;
    Built->Focus = Context.Focus;
    Built->Clipboard = Context.Clipboard;
    return Built;
}

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, PrimitiveTagMap* OutTags,
                                                     StyleMatchStats* Stats) {
    // None/Slot build to nullptr with no widget at all -- nothing to construct a style
    // target for, same "no widget here" treatment BuildWidgetTree's own doc comment
    // describes.
    if (Node.Tag == IrisElementTag::None || Node.Tag == IrisElementTag::Slot) {
        return nullptr;
    }

    const std::string LustreTag = IrisTagToLustreTag(Node.Tag);
    const WalkerStyleElement ThisStyleElement(GetStringProp(Node.Props, "class").value_or(std::string{}), LustreTag,
                                               IsComponentRoot, ParentStyleElement);

    std::unique_ptr<WidgetBase> Built;
    switch (Node.Tag) {
        case IrisElementTag::Frame:
            Built = BuildFrame(Node, Context, ThisStyleElement, OutTags, Stats);
            break;
        case IrisElementTag::Grid:
            Built = BuildGrid(Node, Context, ThisStyleElement, OutTags, Stats);
            break;
        case IrisElementTag::Inline:
            Built = BuildInline(Node, Context, ThisStyleElement, OutTags, Stats);
            break;
        case IrisElementTag::Image:
            Built = BuildImage(Node, Context);
            break;
        case IrisElementTag::Icon:
            Built = BuildIcon(Node, Context);
            break;
        case IrisElementTag::Text:
            Built = BuildText(Node, Context);
            break;
        case IrisElementTag::Scroll:
            Built = BuildScroll(Node, Context, ThisStyleElement, OutTags, Stats);
            break;
        case IrisElementTag::Input:
            Built = BuildInput(Node, Context);
            break;
        default:
            break; // unreachable -- None/Slot returned above, every other tag handled
    }

    if (Built && Context.Style != nullptr && Context.StyleApplier != nullptr) {
        const auto Resolved = Lustre::ResolveStyle(ThisStyleElement, *Context.Style);
        Context.StyleApplier->Apply(*Built, Resolved);
        if (Stats != nullptr && !ThisStyleElement.ClassName().empty()) {
            ++Stats->ClassedNodes;
            if (!ResolvedStyleIsEmpty(Resolved)) {
                ++Stats->ResolvedNodes;
            }
        }
    }

    // Recorded regardless of whether OutTags is used for styling here -- this is the
    // one point every built widget funnels through, and the only place the real
    // IrisElementTag is still known (PenumbraWidgetAdapter.cpp's wrap step, later,
    // only has the built WidgetBase tree, with this distinction already erased).
    if (Built && OutTags != nullptr) {
        (*OutTags)[Built.get()] = LustreTag;
    }

    return Built;
}

} // namespace

std::unique_ptr<WidgetBase> BuildWidgetTree(const Component& Node, const BuildContext& Context,
                                             PrimitiveTagMap* OutTags) {
    StyleMatchStats Stats;
    std::unique_ptr<WidgetBase> Built =
        BuildWidgetTreeInternal(Node, Context, /*ParentStyleElement=*/nullptr, /*IsComponentRoot=*/true, OutTags, &Stats);
#ifndef NDEBUG
    // docs/build_context_style_mismatch_gap.md: every classed node resolved
    // nothing at all from Context.Style -- a near-certain sign the wrong
    // StylesheetSet was passed for this component (the common way to get
    // here: reusing a BuildContext captured for a different component,
    // whose Style points at that other component's own stylesheet, which
    // shares no class names with this one). A component deliberately built
    // with Context.Style == nullptr ("no styling at all", a supported mode
    // -- see BuildContext's own doc comment) never reaches this: the guard
    // above only calls ResolveStyle/increments Stats when Context.Style is
    // non-null in the first place.
    if (Stats.ClassedNodes > 0 && Stats.ResolvedNodes == 0) {
        std::fprintf(stderr,
                     "PenumbraUiBackend::BuildWidgetTree: %zu widget(s) had a `class` prop, but none resolved any "
                     "property from the given Context.Style -- likely the wrong StylesheetSet for this component.\n",
                     Stats.ClassedNodes);
    }
#endif
    return Built;
}

} // namespace PenumbraUiBackend
