#include "PenumbraUiBackend/Walker.h"

#include "PenumbraUiBackend/ChartNodes.h"
#include "PenumbraUiBackend/Cosmos/StyleResolution.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Portal.h"

#include "Chaos/ComponentInstance.h"
#include "Chaos/ChaosNyxDriver.h"
#include "Chaos/Reconciler.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/SplitPanel.h"
#include "Penumbra/Widgets/TextArea.h"
#include "Penumbra/Widgets/TextInput.h"
#include "Penumbra/Widgets/TextSelection.h"

#include "host/nyx-runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace PenumbraUiBackend {

namespace {

using Chaos::Component;
using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::IconWidget;
using Penumbra::Widgets::ImageWidget;
using Penumbra::Widgets::InlineContainer;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::ScrollablePanel;
using Penumbra::Widgets::SplitAxis;
using Penumbra::Widgets::SplitPanel;
using Penumbra::Widgets::TextArea;
using Penumbra::Widgets::TextInput;
using Penumbra::Widgets::WidgetBase;

// docs/build_context_style_mismatch_gap.md: debug-mode tracking for "did any
// node in this BuildWidgetTree() call actually resolve a property from the
// given Context.Style" -- a component built through a BuildContext whose
// Style points at the wrong StylesheetSet resolves nothing at all, silently
// (Cosmos::ResolveStyle's own contract: no rule matched is not an error).
// Threaded through the recursion as a nullable pointer (nullptr in the
// common non-debug-tooling path) rather than added to BuildContext itself --
// this is call-scoped bookkeeping for one BuildWidgetTree() invocation, not
// caller configuration.
struct StyleMatchStats {
    std::size_t ClassedNodes = 0;
    std::size_t ResolvedNodes = 0;
};

// What one BuildWidgetTree() call records about each widget it builds, for the caller's
// optional maps (null when the caller didn't ask) and the style-mismatch diagnostic.
struct BuildRecords {
    PrimitiveTagMap* Tags{nullptr};
    RefMap*          Refs{nullptr};
    NumberPropMap*   Numbers{nullptr};
    StyleMatchStats* Stats{nullptr};
};

bool ResolvedStyleIsEmpty(const ::Cosmos::ResolvedStyle& Style) {
    return !Style.BackgroundColor && !Style.BackgroundGradientStart && !Style.BackgroundGradientEnd &&
           !Style.BorderColor && !Style.BorderWidth && !Style.BorderRadius && !Style.Padding && !Style.Margin &&
           !Style.TextColor && !Style.FontFamily && !Style.FontSizeLogical && !Style.Font && !Style.DisplayMode &&
           !Style.FlexDirectionMode && !Style.Gap && !Style.AlignItems && Style.Transitions.empty() && !Style.Hover &&
           !Style.Active && !Style.Disabled && !Style.Width && !Style.Height && !Style.MinWidth && !Style.MaxWidth &&
           !Style.TransformScale &&
           !Style.TextOverflowMode && !Style.FlexGrow && !Style.ScrollbarThumbColor && !Style.ScrollbarTrackColor &&
           !Style.ScrollbarWidthLogical && !Style.JustifyContent && !Style.WhiteSpaceMode && !Style.ShadowColor &&
           !Style.ShadowBlurRadiusLogical && !Style.Opacity && !Style.Starting && Style.AttrBindings.empty() &&
           !Style.SelectionColor;
}

// `Node.Ref` mirrors `Node.Key` exactly (both `std::optional<ChaosPropValue>`,
// `Chaos/Component.h`). Returns nullopt for an unset ref or (defensively) a non-string
// value.
std::optional<std::string> GetRefName(const Component& Node) {
    if (!Node.Ref.has_value()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::string>(&*Node.Ref)) {
        return *Value;
    }
    return std::nullopt;
}

std::optional<std::string> GetStringProp(const ChaosProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::string>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

// An int or float prop as a float -- what a Cosmos attr() binding reads.
std::optional<float> GetNumberProp(const ChaosProps& Props, std::string_view Name) {
    const auto It = Props.find(std::string(Name));
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* AsFloat = std::get_if<float>(&It->second)) {
        return *AsFloat;
    }
    if (const auto* AsInt = std::get_if<int>(&It->second)) {
        return static_cast<float>(*AsInt);
    }
    return std::nullopt;
}

std::optional<float> GetFloatProp(const ChaosProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<float>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

std::optional<bool> GetBoolProp(const ChaosProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<bool>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

std::optional<std::function<void()>> GetEventProp(const ChaosProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::function<void()>>(&It->second)) {
        return *Value;
    }
    return std::nullopt;
}

// Sibling to GetEventProp for the value-carrying event variant `<Input>`'s
// onTextChange needs (ChaosPropValue's std::function<void(std::string)> alternative,
// docs/iris_core_spec.md's <Input> section) -- the zero-arg GetEventProp above can't
// extract this one, std::get_if is keyed on the exact alternative type.
std::optional<std::function<void(std::string)>> GetStringEventProp(const ChaosProps& Props, const std::string& Name) {
    const auto It = Props.find(Name);
    if (It == Props.end()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::function<void(std::string)>>(&It->second)) {
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
void ApplySharedPropsToWidget(WidgetBase& Widget, const ChaosProps& Props) {
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

std::string ChaosTagToCosmosTag(ChaosElementTag Tag) {
    switch (Tag) {
        case ChaosElementTag::Frame: return "Frame";
        case ChaosElementTag::Inline: return "Inline";
        case ChaosElementTag::Grid: return "Grid";
        case ChaosElementTag::Image: return "Image";
        case ChaosElementTag::Icon: return "Icon";
        case ChaosElementTag::Text: return "Text";
        case ChaosElementTag::Scroll: return "Scroll";
        case ChaosElementTag::Input: return "Input";
        case ChaosElementTag::TextArea: return "TextArea";
        case ChaosElementTag::Native: return "Native";
        case ChaosElementTag::Portal: return "Portal";
        case ChaosElementTag::Split: return "Split";
        case ChaosElementTag::BarChart: return "BarChart";
        case ChaosElementTag::LineChart: return "LineChart";
        case ChaosElementTag::DonutChart: return "DonutChart";
        case ChaosElementTag::Series: return "Series";
        case ChaosElementTag::Slice: return "Slice";
        default: return "";
    }
}

// Cosmos::IStyleTarget over the ancestor chain BuildWidgetTree's own recursion already
// walks -- no separate tree walk needed, since resolving a node's style happens exactly
// once, right when that node is built, with every ancestor's WalkerStyleElement still
// alive on the call stack above it. `IsComponentRoot_` is true exactly for the node a
// given `BuildWidgetTree()` call started at (see that function's own doc comment for why
// that's the correct boundary signal today).
class WalkerStyleElement : public ::Cosmos::IStyleTarget {
public:
    WalkerStyleElement(std::string ClassName, std::string PrimitiveTag, bool IsComponentRoot,
                        const WalkerStyleElement* Parent)
        : ClassName_(std::move(ClassName)), PrimitiveTag_(std::move(PrimitiveTag)), IsComponentRoot_(IsComponentRoot),
          Parent_(Parent) {}

    std::string ClassName() const override { return ClassName_; }
    std::string PrimitiveTag() const override { return PrimitiveTag_; }
    bool         IsComponentRoot() const override { return IsComponentRoot_; }
    const ::Cosmos::IStyleTarget* Parent() const override { return Parent_; }

private:
    std::string ClassName_;
    std::string PrimitiveTag_;
    bool        IsComponentRoot_;
    const WalkerStyleElement* Parent_;
};

// What a component's lifecycle hooks get back from `GetRef(name)`. Each setter
// dynamic_casts to the widget type it applies to and does nothing on any other, so a
// mismatched call (SetIconName on a Label) is a no-op rather than an error.
class WidgetRefHandle {
public:
    explicit WidgetRefHandle(WidgetBase* Widget) : Widget_(Widget) {}

    void SetText(const std::string& Text) {
        if (auto* AsLabel = dynamic_cast<Label*>(Widget_)) {
            AsLabel->Text = Text;
        }
    }

    void SetIconName(const std::string& Name) {
        if (auto* AsIcon = dynamic_cast<IconWidget*>(Widget_)) {
            AsIcon->IconName = Name;
        }
    }

    // Nyx has no native color literal today (no precedent anywhere else this codebase
    // bridges to Nyx), so this takes four 0-255 channel ints -- the same layout
    // Penumbra::Render::Color itself already stores (Color.h), just without requiring a
    // struct on the Nyx side. Clamped, not asserted: a script passing an out-of-range
    // channel is a scripting mistake, not a C++-side bug to crash over.
    void SetColor(std::int32_t R, std::int32_t G, std::int32_t B, std::int32_t A) {
        const auto              Clamp = [](std::int32_t V) { return static_cast<std::uint8_t>(std::clamp(V, 0, 255)); };
        const Penumbra::Render::Color Resolved{Clamp(R), Clamp(G), Clamp(B), Clamp(A)};
        if (auto* AsLabel = dynamic_cast<Label*>(Widget_)) {
            AsLabel->ColorText = Resolved;
        } else if (auto* AsIcon = dynamic_cast<IconWidget*>(Widget_)) {
            AsIcon->ColorLogical = Resolved;
        }
    }

    // WidgetBase::SetIsVisible itself (not a dynamic_cast branch) -- every built widget
    // has it, not just Box-derived ones, so no cast/no-op case exists here at all.
    void SetVisible(bool Visible) { Widget_->SetIsVisible(Visible); }

private:
    WidgetBase* Widget_;
};

// The refs a component's lifecycle hooks can reach through `GetRef`, with the handles
// already handed out for them. A handle is cached per name so a per-frame OnTick doesn't
// allocate one on every call.
class ComponentRefs {
public:
    explicit ComponentRefs(const RefMap* Refs) : Refs_(Refs) {}

    WidgetRefHandle* Find(const std::string& Name) {
        if (Refs_ == nullptr) {
            return nullptr;
        }
        const auto RefIt = Refs_->find(Name);
        if (RefIt == Refs_->end()) {
            return nullptr;
        }
        std::unique_ptr<WidgetRefHandle>& Handle = Handles_[Name];
        if (!Handle) {
            Handle = std::make_unique<WidgetRefHandle>(RefIt->second);
        }
        return Handle.get();
    }

private:
    const RefMap*                                                     Refs_;
    std::unordered_map<std::string, std::unique_ptr<WidgetRefHandle>> Handles_;
};

// The refs of the component whose lifecycle hook is running, which is what the
// runtime-wide `GetRef` resolves against. Null outside a hook, so `GetRef` finds nothing.
thread_local ComponentRefs* RunningComponentRefs = nullptr;

class RunningComponentScope {
public:
    explicit RunningComponentScope(ComponentRefs& Refs) : Previous_(RunningComponentRefs) {
        RunningComponentRefs = &Refs;
    }
    ~RunningComponentScope() { RunningComponentRefs = Previous_; }

    RunningComponentScope(const RunningComponentScope&)            = delete;
    RunningComponentScope& operator=(const RunningComponentScope&) = delete;

private:
    ComponentRefs* Previous_;
};

// docs/next_steps.md's "reconciler-side wiring for a framework-owned component
// lifecycle system" ask. `Umbra::IWidgetLifecycle` (`chaos::ComponentInstance::
// Lifecycle`'s own type) and `Penumbra::IWidgetLifecycle` (what
// `Penumbra::LifecycleRegistry::RegisterLifecycle` takes) are two distinct classes with
// identical virtual signatures
// -- deliberately mirrored, per `Penumbra::IWidgetLifecycle`'s own doc comment, rather
// than one shared type, since Penumbra doesn't depend on umbra-interfaces. This repo is
// the one place that sees both sides of the mirror, so it's the one that has to bridge
// them -- not a framework gap on either side.
// `InnerWatch_` (umbra-interfaces' `Umbra::LivenessGuard::Watch`) guards exactly the
// dangling-pointer shape this repo has actually hit: `Inner_` outliving its own pointee
// when whatever owns it (a Chaos `NyxDriverState`, transitively through
// `Node.Instance`) is torn down before this bridge's own `OnUnmount()`/`OnTick()` is
// called by `Penumbra::LifecycleRegistry` -- previously an `EXC_BAD_ACCESS` inside
// `Inner_->OnTick(...)`/`Inner_->OnUnmount()`, only diagnosable after the fact via
// `lldb`. `AssertAlive` turns that into an immediate, attributable `abort()` at the
// actual call site instead.
class UmbraLifecycleBridge : public Penumbra::IWidgetLifecycle {
public:
    UmbraLifecycleBridge(Umbra::IWidgetLifecycle* Inner, const RefMap* Refs)
        : Inner_(Inner), InnerWatch_(Inner->Liveness()), Refs_(Refs) {}

    void OnMount() override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnMount)");
        RunningComponentScope Running(Refs_);
        Inner_->OnMount();
    }
    void OnUnmount() override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnUnmount)");
        RunningComponentScope Running(Refs_);
        Inner_->OnUnmount();
    }
    void OnTick(const Penumbra::TickInfo& Info) override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnTick)");
        RunningComponentScope Running(Refs_);
        Inner_->OnTick(Umbra::TickInfo{Info.DeltaSeconds});
    }

private:
    Umbra::IWidgetLifecycle*    Inner_;
    Umbra::LivenessGuard::Watch InnerWatch_;
    ComponentRefs               Refs_;
};

// Registers Node's own component lifecycle (if any) against Context.LifecycleHost, and
// arranges for it to unregister automatically when Built is actually torn down. Called
// once per built widget from BuildWidgetTreeInternal, at the same per-node point
// OutTags/OutRefs are already recorded -- see Walker.h's BuildWidgetTree comment for why
// this can't be a one-shot root-only check (a plain nested `<ChildComponent .../>` with
// no `<Slot>` also carries its own Component::Instance, inline in the same tree).
//
// `LifecycleRegistry::RegisterLifecycle`/`UnregisterLifecycle` already call `OnMount()`/
// `OnUnmount()` internally (`external/penumbra`'s `LifecycleRegistry.cpp`), so this function's
// only job is picking the right moments to call them. `WidgetBase::OnDestroyed`
// (`Penumbra/Widgets/WidgetBase.h`) -- an existing, generic "this widget is being torn
// down" hook, unused anywhere else in this repo -- fires from `~WidgetBase()`, giving
// exactly the unmount timing needed without requiring `ComponentInstance` to grow a
// destructor or a registry reference of its own (Chaos's own decision, see
// `Chaos/ComponentInstance.h`'s `Lifecycle` field comment: passive and non-owning,
// deliberately no self-unregister-at-destruction). The bridge itself needs no separate
// storage anywhere -- the OnDestroyed lambda's own capture owns it for exactly as long
// as it needs to live, freeing it the instant UnregisterLifecycle has been called.
//
// `Bridge` is a `shared_ptr`, not `unique_ptr`, purely so the capturing lambda stays
// copyable -- `WidgetBase::OnDestroyed` is a `std::function<void()>`, which requires a
// copyable target, the same constraint `TestNativeUnwrapsAPenumbraWidgetToItsRealWidgetBase`
// (`tests/WalkerTests.cpp`) already worked around the same way for a move-only capture.
// `Built.OnDestroyed` is only ever invoked once (from `~WidgetBase()`), so the shared
// ownership never actually gets shared in practice.
void RegisterLifecycleIfPresent(const Component& Node, const BuildContext& Context, WidgetBase& Built,
                                const RefMap* Refs) {
    if (Context.LifecycleHost == nullptr || !Node.Instance || Node.Instance->Lifecycle == nullptr) {
        return;
    }
    auto                         Bridge = std::make_shared<UmbraLifecycleBridge>(Node.Instance->Lifecycle, Refs);
    Penumbra::LifecycleRegistry* Host = Context.LifecycleHost;
    Host->RegisterLifecycle(Bridge.get());
    Built.OnDestroyed = [Host, Bridge]() { Host->UnregisterLifecycle(Bridge.get()); };
}

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, BuildRecords& Records);

// The five event props plus `class` are the exact shared method set every Box-derived
// primitive's Builder exposes identically (Box::Builder, Label::Builder,
// InlineContainer::Builder — docs/iris_core_spec.md §3.1's "most share the same
// method-naming convention"). Templated rather than duplicated per primitive since none
// of those Builders share a common base class — only the method names line up.
template <typename BuilderT>
void ApplySharedProps(BuilderT& Builder, const ChaosProps& Props) {
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

// Builds every child, skipping any that resolve to `nullptr` (a `ChaosElementTag::None`
// child — "no widget here", never added to the parent's Children at all, not even as a
// hole). `ThisStyleElement` is Node's own style-target wrapper, already constructed by
// the caller -- passed down so every child's WalkerStyleElement can point back to it as
// their ancestor.
template <typename BoxLikeBuilder>
void BuildAndAttachChildren(BoxLikeBuilder& Builder, const Component& Node, const BuildContext& Context,
                             const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, Records)) {
            Builder.child(std::move(ChildWidget));
        }
    }
}

std::unique_ptr<WidgetBase> BuildFrame(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, Records);
    return Builder.build();
}

// docs/iris_stage2_decision_doc.md §3 / docs/iris_core_spec.md §3.1: Penumbra has no
// grid layout mode — `<Grid>` maps onto a plain `Box` with `LayoutMode::HorizontalStack`
// as a stub, which explicitly does not meet the Core-primitive requirement yet. `Layout`
// is a public field on `Box`, not something `Box::Builder` exposes — there's no
// `layout()` Builder method to chain, so it's set directly on the built widget.
std::unique_ptr<WidgetBase> BuildGrid(const Component& Node, const BuildContext& Context,
                                       const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, Records);
    std::unique_ptr<Box> Built = Builder.build();
    Built->Layout = Penumbra::Widgets::LayoutMode::HorizontalStack;
    return Built;
}

std::unique_ptr<WidgetBase> BuildInline(const Component& Node, const BuildContext& Context,
                                         const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    InlineContainer::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, Records);
    return Builder.build();
}

// `<Text>`'s content lands in its own `"text"` prop, not a child, so there's nothing to
// recurse into here, just `text()` on the Builder.
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
// gives its shared props. wheelStep maps onto WheelStepLogical, direction (default
// "vertical", the only other value "horizontal") onto ScrollablePanel::Direction, and
// horizontalWheelStep onto HorizontalWheelStepLogical -- the Direction == Horizontal
// counterpart of wheelStep/WheelStepLogical, for a <Scroll direction="horizontal"> (a
// non-wrapping code block's own container, give-code-blocks-their-own-horizontally-
// scrollable-non-wrapping-frame-in-the-card-modal, Cairn).
std::unique_ptr<WidgetBase> BuildScroll(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    auto Built = std::make_unique<ScrollablePanel>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto WheelStep = GetFloatProp(Node.Props, "wheelStep")) {
        Built->WheelStepLogical = *WheelStep;
    }
    if (const auto DirectionProp = GetStringProp(Node.Props, "direction"); DirectionProp == "horizontal") {
        Built->Direction = ScrollablePanel::ScrollDirection::Horizontal;
    }
    if (const auto HorizontalWheelStep = GetFloatProp(Node.Props, "horizontalWheelStep")) {
        Built->HorizontalWheelStepLogical = *HorizontalWheelStep;
    }
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, Records)) {
            Built->AddChild(std::move(ChildWidget));
        }
    }
    return Built;
}

// <Split> (docs/native_split_backend_wiring_gap.md) -- SplitPanel has no Builder either
// (same "plain fields, not a Builder chain" treatment BuildScroll above already gives
// ScrollablePanel) and takes exactly two children through SetFirst/SetSecond rather than
// a generic Children vector; the size checks below guard a tree with fewer, matching
// this walker's existing tolerance for other malformed input.
std::unique_ptr<WidgetBase> BuildSplit(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    auto Built = std::make_unique<SplitPanel>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto Axis = GetStringProp(Node.Props, "axis")) {
        Built->Axis = (*Axis == "vertical") ? SplitAxis::Vertical : SplitAxis::Horizontal;
    }
    if (const auto Ratio = GetFloatProp(Node.Props, "ratio")) {
        Built->SplitRatio = *Ratio;
    }
    if (const auto MinPaneSize = GetFloatProp(Node.Props, "minPaneSize")) {
        Built->MinPaneSizeLogical = *MinPaneSize;
    }
    if (const auto HandleThickness = GetFloatProp(Node.Props, "handleThickness")) {
        Built->HandleThicknessLogical = *HandleThickness;
    }
    if (Node.Children.size() >= 1) {
        if (auto First = BuildWidgetTreeInternal(Node.Children[0], Context, &ThisStyleElement,
                                                  /*IsComponentRoot=*/false, Records)) {
            Built->SetFirst(std::move(First));
        }
    }
    if (Node.Children.size() >= 2) {
        if (auto Second = BuildWidgetTreeInternal(Node.Children[1], Context, &ThisStyleElement,
                                                   /*IsComponentRoot=*/false, Records)) {
            Built->SetSecond(std::move(Second));
        }
    }
    return Built;
}

// <BarChart>/<LineChart>/<DonutChart> (ChartNodes.h): the chart's own props through
// ApplyChartProps -- the same mapping a reconcile uses -- and its children, normally
// <Series>/<Slice> data nodes, which it reads its data from at every layout.
template <typename ChartT>
std::unique_ptr<WidgetBase> BuildChart(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    auto Built = std::make_unique<ChartT>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    Built->FontBackend = Context.FontBackend;
    Built->Font        = Context.Font;
    ApplyChartProps(*Built, chaos::ComputePropDiff({}, Node.Props));
    if constexpr (std::is_base_of_v<Penumbra::Widgets::AxisChartBase, ChartT>) {
        Built->ShowAxes = GetBoolProp(Node.Props, "showAxes").value_or(true);
    }
    if constexpr (std::is_base_of_v<Penumbra::Widgets::BarChart, ChartT>) {
        Built->Stacked = GetBoolProp(Node.Props, "stacked").value_or(false);
    }
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget =
                BuildWidgetTreeInternal(Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, Records)) {
            Built->AddChild(std::move(ChildWidget));
        }
    }
    return Built;
}

// <Series>/<Slice>: a chart's data, as leaf nodes that draw nothing.
template <typename NodeT>
std::unique_ptr<WidgetBase> BuildChartData(const Component& Node) {
    auto Built = std::make_unique<NodeT>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    ApplyChartProps(*Built, chaos::ComputePropDiff({}, Node.Props));
    return Built;
}

std::unique_ptr<WidgetBase> BuildInput(const Component& Node, const BuildContext& Context) {
    auto Built = std::make_unique<TextInput>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto Text = GetStringProp(Node.Props, "text")) {
        Built->Text = *Text;
    }
    if (const auto PreferredWidth = GetFloatProp(Node.Props, "preferredWidth")) {
        Built->PreferredWidthLogical = *PreferredWidth;
    }
    if (const auto OnTextChange = GetStringEventProp(Node.Props, "onTextChange")) {
        std::function<void(std::string)> Callback = *OnTextChange;
        Built->OnTextChanged = [Callback](const std::string& NewText) { Callback(NewText); };
    }
    Built->FontBackend = Context.FontBackend;
    Built->Font = Context.Font;
    Built->Focus = Context.Focus;
    Built->Clipboard = Context.Clipboard;
    return Built;
}

std::unique_ptr<WidgetBase> BuildTextArea(const Component& Node, const BuildContext& Context) {
    auto Built = std::make_unique<TextArea>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto Text = GetStringProp(Node.Props, "text")) {
        Built->Text = *Text;
    }
    if (const auto PreferredWidth = GetFloatProp(Node.Props, "preferredWidth")) {
        Built->PreferredWidthLogical = *PreferredWidth;
    }
    if (const auto WheelStep = GetFloatProp(Node.Props, "wheelStep")) {
        Built->WheelStepLogical = *WheelStep;
    }
    if (const auto OnTextChange = GetStringEventProp(Node.Props, "onTextChange")) {
        std::function<void(std::string)> Callback = *OnTextChange;
        Built->OnTextChanged = [Callback](const std::string& NewText) { Callback(NewText); };
    }
    const Umbra::ChaosPropDiff Props = chaos::ComputePropDiff({}, Node.Props);
    const auto Ranges = Props.NumberLists.find("spanRanges");
    const auto Classes = Props.StringLists.find("spanClasses");
    if (Ranges != Props.NumberLists.end() && Classes != Props.StringLists.end()) {
        Built->SetSpans(TextAreaSpansFromProps(Ranges->second, Classes->second));
    }
    Built->FontBackend = Context.FontBackend;
    Built->Font = Context.Font;
    Built->Focus = Context.Focus;
    Built->Clipboard = Context.Clipboard;
    return Built;
}

// <Native> (docs/native_split_backend_wiring_gap.md) -- an opaque escape hatch whose
// `build` prop already returns a live Umbra::IWidget handle, not more Component IR to
// walk (Component::NativeBuilder, `external/chaos/include/Chaos/Component.h`). Every real
// Umbra::IWidget in this stack is a PenumbraWidget -- this file and
// PenumbraWidgetAdapter.cpp are the only two places one is ever constructed -- so this
// downcasts to reclaim the real WidgetBase ownership via DetachOwnership(), the same
// owning -> attached transition InsertChildAt (PenumbraWidgetAdapter.cpp) already relies
// on elsewhere, just invoked here instead of there. A `build` prop that hands back some
// other Umbra::IWidget implementation has no real Penumbra widget to unwrap and builds to
// nullptr -- the same "no widget here" treatment None/Slot already get, not an error.
std::unique_ptr<WidgetBase> BuildNative(const Component& Node) {
    if (!Node.NativeBuilder) {
        return nullptr;
    }
    std::unique_ptr<Umbra::IWidget> Handle = Node.NativeBuilder->Build();
    if (auto* AsPenumbraWidget = dynamic_cast<PenumbraWidget*>(Handle.get())) {
        return AsPenumbraWidget->DetachOwnership();
    }
    return nullptr;
}

std::unique_ptr<WidgetBase> BuildPortal(const Component& Node, const BuildContext& Context,
                                         const WalkerStyleElement& ThisStyleElement, BuildRecords& Records) {
    std::unique_ptr<WidgetBase> Content;
    if (!Node.Children.empty()) {
        Content = BuildWidgetTreeInternal(Node.Children.front(), Context, &ThisStyleElement,
                                          /*IsComponentRoot=*/false, Records);
    }
    auto Built = std::make_unique<PortalAnchorWidget>(Context.OverlayHost, std::move(Content),
                                                       Chaos::ReadPortalProperties(Node.Props));
    ApplySharedPropsToWidget(*Built, Node.Props);
    return Built;
}

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, BuildRecords& Records) {
    // None/Slot build to nullptr with no widget at all -- nothing to construct a style
    // target for, same "no widget here" treatment BuildWidgetTree's own doc comment
    // describes.
    if (Node.Tag == ChaosElementTag::None || Node.Tag == ChaosElementTag::Slot) {
        return nullptr;
    }

    const std::string CosmosTag = ChaosTagToCosmosTag(Node.Tag);
    const WalkerStyleElement ThisStyleElement(GetStringProp(Node.Props, "class").value_or(std::string{}), CosmosTag,
                                               IsComponentRoot, ParentStyleElement);

    std::unique_ptr<WidgetBase> Built;
    switch (Node.Tag) {
        case ChaosElementTag::Frame:
            Built = BuildFrame(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Grid:
            Built = BuildGrid(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Inline:
            Built = BuildInline(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Image:
            Built = BuildImage(Node, Context);
            break;
        case ChaosElementTag::Icon:
            Built = BuildIcon(Node, Context);
            break;
        case ChaosElementTag::Text:
            Built = BuildText(Node, Context);
            break;
        case ChaosElementTag::Scroll:
            Built = BuildScroll(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Input:
            Built = BuildInput(Node, Context);
            break;
        case ChaosElementTag::TextArea:
            Built = BuildTextArea(Node, Context);
            break;
        case ChaosElementTag::Native:
            Built = BuildNative(Node);
            break;
        case ChaosElementTag::Portal:
            Built = BuildPortal(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Split:
            Built = BuildSplit(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::BarChart:
            Built = BuildChart<DataBarChart>(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::LineChart:
            Built = BuildChart<DataLineChart>(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::DonutChart:
            Built = BuildChart<DataDonutChart>(Node, Context, ThisStyleElement, Records);
            break;
        case ChaosElementTag::Series:
            Built = BuildChartData<SeriesNode>(Node);
            break;
        case ChaosElementTag::Slice:
            Built = BuildChartData<SliceNode>(Node);
            break;
        default:
            break; // unreachable -- None/Slot returned above, every other tag handled
    }

    if (auto* AsBox = dynamic_cast<Box*>(Built.get()); AsBox && GetBoolProp(Node.Props, "selectable").value_or(false)) {
        Penumbra::Widgets::TextSelection& Selection = AsBox->EnableTextSelection();
        Selection.Focus = Context.Focus;
        Selection.Clipboard = Context.Clipboard;
    }

    if (Built && Context.Style != nullptr && Context.StyleApplier != nullptr) {
        auto Resolved = Cosmos::ResolveStyle(ThisStyleElement, *Context.Style);
        Cosmos::BindAttributes(Resolved, [&Node](std::string_view Name) { return GetNumberProp(Node.Props, Name); });
        Context.StyleApplier->Apply(*Built, Resolved);
        if (auto* AsTextArea = dynamic_cast<TextArea*>(Built.get())) {
            Cosmos::StyleTextAreaSpans(*AsTextArea, ThisStyleElement, *Context.Style, *Context.StyleApplier);
        }
        if (Records.Stats != nullptr && !ThisStyleElement.ClassName().empty()) {
            ++Records.Stats->ClassedNodes;
            if (!ResolvedStyleIsEmpty(Resolved)) {
                ++Records.Stats->ResolvedNodes;
            }
        }
    }

    // Recorded regardless of whether OutTags is used for styling here -- this is the
    // one point every built widget funnels through, and the only place the real
    // ChaosElementTag is still known (PenumbraWidgetAdapter.cpp's wrap step, later,
    // only has the built WidgetBase tree, with this distinction already erased).
    if (Built && Records.Tags != nullptr) {
        (*Records.Tags)[Built.get()] = CosmosTag;
    }

    // A wrapper re-resolving this widget's style later (a class change) has only the
    // widget, not Node, so it needs the number props an attr() binding may read.
    if (Built && Records.Numbers != nullptr) {
        for (const auto& [Name, Value] : Node.Props) {
            if (const std::optional<float> Number = GetNumberProp(Node.Props, Name)) {
                (*Records.Numbers)[Built.get()][Name] = *Number;
            }
        }
    }

    // Same "record at the one point every built widget funnels through" reasoning as
    // OutTags above -- Node.Ref is only reachable here, not from the built WidgetBase
    // tree PenumbraWidgetAdapter.cpp's wrap step sees later.
    if (Built && Records.Refs != nullptr) {
        if (const auto RefName = GetRefName(Node)) {
            (*Records.Refs)[*RefName] = Built.get();
        }
    }

    // Same per-node reasoning as OutTags/OutRefs above -- Node.Instance is only
    // reachable here, not from the built WidgetBase tree later, and isn't limited to
    // Node's own root (see RegisterLifecycleIfPresent's own comment).
    if (Built) {
        RegisterLifecycleIfPresent(Node, Context, *Built, Records.Refs);
    }

    return Built;
}

} // namespace

void RegisterComponentGetRef(nyx::host::NyxRuntime& Runtime) {
    if (!Runtime.Globals().contains("WidgetRef")) {
        Runtime.RegisterType<WidgetRefHandle>("WidgetRef")
            .Method("SetText", &WidgetRefHandle::SetText)
            .Method("SetIconName", &WidgetRefHandle::SetIconName)
            .Method("SetColor", &WidgetRefHandle::SetColor)
            .Method("SetVisible", &WidgetRefHandle::SetVisible);
    }
    if (!Runtime.Globals().contains("GetRef")) {
        Runtime.RegisterFunction("GetRef", [](const std::string& Name) -> WidgetRefHandle* {
            return RunningComponentRefs != nullptr ? RunningComponentRefs->Find(Name) : nullptr;
        });
    }
}

std::unique_ptr<WidgetBase> BuildWidgetTree(const Component& Node, const BuildContext& Context,
                                             PrimitiveTagMap* OutTags, RefMap* OutRefs, NumberPropMap* OutNumbers) {
    StyleMatchStats Stats;
    BuildRecords    Records{OutTags, OutRefs, OutNumbers, &Stats};
    std::unique_ptr<WidgetBase> Built = BuildWidgetTreeInternal(
        Node, Context, /*ParentStyleElement=*/nullptr, /*IsComponentRoot=*/true, Records);
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
