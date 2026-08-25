#include "PenumbraUiBackend/Walker.h"

#include "PenumbraUiBackend/Lustre/StyleResolution.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Portal.h"

#include "Iris/ComponentInstance.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/IconWidget.h"
#include "Penumbra/Widgets/ImageWidget.h"
#include "Penumbra/Widgets/InlineContainer.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/SplitPanel.h"
#include "Penumbra/Widgets/TextInput.h"

#include "host/marshal.hpp"
#include "runtime/callable.hpp"
#include "runtime/environment.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
using Penumbra::Widgets::SplitAxis;
using Penumbra::Widgets::SplitPanel;
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

// `Node.Ref` mirrors `Node.Key` exactly (both `std::optional<IrisPropValue>`,
// `Iris/Component.h`) -- extracts the plain string it always carries via real `.iris`
// codegen. Returns nullopt for an unset ref or (defensively) a non-string value.
std::optional<std::string> GetRefName(const Component& Node) {
    if (!Node.Ref.has_value()) {
        return std::nullopt;
    }
    if (const auto* Value = std::get_if<std::string>(&*Node.Ref)) {
        return *Value;
    }
    return std::nullopt;
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

// Sibling to GetEventProp for the value-carrying event variant `<Input>`'s
// onTextChange needs (IrisPropValue's std::function<void(std::string)> alternative,
// docs/iris_core_spec.md's <Input> section) -- the zero-arg GetEventProp above can't
// extract this one, std::get_if is keyed on the exact alternative type.
std::optional<std::function<void(std::string)>> GetStringEventProp(const IrisProps& Props, const std::string& Name) {
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
        case IrisElementTag::Native: return "Native";
        case IrisElementTag::Portal: return "Portal";
        case IrisElementTag::Split: return "Split";
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

// docs/next_steps.md's "reconciler-side wiring for a framework-owned component
// lifecycle system" ask. `Umbra::IWidgetLifecycle` (`iris::ComponentInstance::
// Lifecycle`'s own type) and `Penumbra::IWidgetLifecycle` (what
// `Penumbra::LifecycleRegistry::RegisterLifecycle` takes) are two distinct classes with
// identical virtual signatures
// -- deliberately mirrored, per `Penumbra::IWidgetLifecycle`'s own doc comment, rather
// than one shared type, since Penumbra doesn't depend on umbra-interfaces. This repo is
// the one place that sees both sides of the mirror, so it's the one that has to bridge
// them -- not a framework gap on either side.
// `InnerWatch_` (umbra-interfaces' `Umbra::LivenessGuard::Watch`) guards exactly the
// dangling-pointer shape this repo has actually hit: `Inner_` outliving its own pointee
// when whatever owns it (an iris-proto `NyxDriverState`, transitively through
// `Node.Instance`) is torn down before this bridge's own `OnUnmount()`/`OnTick()` is
// called by `Penumbra::LifecycleRegistry` -- previously an `EXC_BAD_ACCESS` inside
// `Inner_->OnTick(...)`/`Inner_->OnUnmount()`, only diagnosable after the fact via
// `lldb`. `AssertAlive` turns that into an immediate, attributable `abort()` at the
// actual call site instead.
class UmbraLifecycleBridge : public Penumbra::IWidgetLifecycle {
public:
    explicit UmbraLifecycleBridge(Umbra::IWidgetLifecycle* Inner)
        : Inner_(Inner), InnerWatch_(Inner->Liveness()) {}

    void OnMount() override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnMount)");
        Inner_->OnMount();
    }
    void OnUnmount() override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnUnmount)");
        Inner_->OnUnmount();
    }
    void OnTick(const Penumbra::TickInfo& Info) override {
        InnerWatch_.AssertAlive("UmbraLifecycleBridge::Inner_ (OnTick)");
        Inner_->OnTick(Umbra::TickInfo{Info.DeltaSeconds});
    }

private:
    Umbra::IWidgetLifecycle*    Inner_;
    Umbra::LivenessGuard::Watch InnerWatch_;
};

// Registers Node's own component lifecycle (if any) against Context.LifecycleHost, and
// arranges for it to unregister automatically when Built is actually torn down. Called
// once per built widget from BuildWidgetTreeInternal, at the same per-node point
// OutTags/OutRefs are already recorded -- see Walker.h's BuildWidgetTree comment for why
// this can't be a one-shot root-only check (a plain nested `<ChildComponent .../>` with
// no `<Slot>` also carries its own Component::Instance, inline in the same tree).
//
// `LifecycleRegistry::RegisterLifecycle`/`UnregisterLifecycle` already call `OnMount()`/
// `OnUnmount()` internally (`vendor/penumbra`'s `LifecycleRegistry.cpp`), so this function's
// only job is picking the right moments to call them. `WidgetBase::OnDestroyed`
// (`Penumbra/Widgets/WidgetBase.h`) -- an existing, generic "this widget is being torn
// down" hook, unused anywhere else in this repo -- fires from `~WidgetBase()`, giving
// exactly the unmount timing needed without requiring `ComponentInstance` to grow a
// destructor or a registry reference of its own (`iris-proto`'s own decision, see
// `Iris/ComponentInstance.h`'s `Lifecycle` field comment: passive and non-owning,
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
void RegisterLifecycleIfPresent(const Component& Node, const BuildContext& Context, WidgetBase& Built) {
    if (Context.LifecycleHost == nullptr || !Node.Instance || Node.Instance->Lifecycle == nullptr) {
        return;
    }
    auto                         Bridge = std::make_shared<UmbraLifecycleBridge>(Node.Instance->Lifecycle);
    Penumbra::LifecycleRegistry* Host = Context.LifecycleHost;
    Host->RegisterLifecycle(Bridge.get());
    Built.OnDestroyed = [Host, Bridge]() { Host->UnregisterLifecycle(Bridge.get()); };
}

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                                                     StyleMatchStats* Stats);

// docs/next_steps.md's "a Nyx-authored OnMount/OnTick has no way to reach its own
// component's ref'd widgets" ask. The small typed-setter wrapper `GetRef(name)` (below)
// hands back to Nyx script -- dynamic_cast dispatch onto whichever concrete Penumbra
// widget type the ref actually names, silently no-op on a mismatched call (e.g.
// SetIconName on a plain Box with no IconWidget), matching this walker's existing
// "malformed/mismatched input is simply not applied" tolerance elsewhere (GetStringProp
// et al., GetRefName above). Exactly the four operations pharos-proto's own
// inspector_panel_native.cpp hand-writes today per Inspector row -- Text/IconName/
// ColorText/SetIsVisible -- not a speculative surface (docs/next_steps.md's own "one real
// open design question" section names this as the validated minimum bar).
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

// Registers the "WidgetRef" host type (the four WidgetRefHandle methods above) against
// Runtime exactly once -- TypeBuilder<T>'s destructor commits unconditionally on every
// call, so a naive "register every time a lifecycle-bearing node is built" would leak a
// fresh TypeDescriptor into Runtime's own registry on every reload/rebuild. Dispatch on a
// HostObject-kind Value goes entirely through `host->descriptor` at call time
// (Interpreter::CallInstanceMethod, confirmed against nyx-proto's real source -- never
// through any Environment/Globals lookup), so this registration doesn't need to happen on
// whichever Environment a particular component's GetRef closure is later defined into; one
// registration against the app's single NyxRuntime is visible from every HostObject this
// walker ever hands back, regardless of which component's own RenderScope built it.
const nyx::runtime::TypeDescriptor* EnsureWidgetRefTypeRegistered(nyx::host::NyxRuntime& Runtime) {
    static std::unordered_map<nyx::host::NyxRuntime*, const nyx::runtime::TypeDescriptor*> Registered;
    if (const auto Existing = Registered.find(&Runtime); Existing != Registered.end()) {
        return Existing->second;
    }
    {
        auto Builder = Runtime.RegisterType<WidgetRefHandle>("WidgetRef");
        Builder.Method("SetText", &WidgetRefHandle::SetText)
            .Method("SetIconName", &WidgetRefHandle::SetIconName)
            .Method("SetColor", &WidgetRefHandle::SetColor)
            .Method("SetVisible", &WidgetRefHandle::SetVisible);
    } // Builder destructs here -- commits the descriptor into Runtime.
    const nyx::runtime::TypeDescriptor* Descriptor = nullptr;
    if (const auto Global = Runtime.Globals().find("WidgetRef"); Global != Runtime.Globals().end()) {
        Descriptor = std::get<std::shared_ptr<nyx::runtime::HostObject>>(Global->second.data)->descriptor;
    }
    Registered[&Runtime] = Descriptor;
    return Descriptor;
}

// Defines a Nyx-callable `GetRef(name: string)` directly into Node's own component
// instance's `Iris::NyxDriverState::RenderScope` Environment -- deliberately never onto
// Context.NyxHost->Globals(). docs/next_steps.md's own ask left this as one real open
// design question (global-on-Runtime::Globals() vs. per-instance-scoped); resolved here in
// favor of per-instance, confirmed against real source rather than assumed:
//
// - A global `GetRef` would be one flat binding shared by every concurrently-mounted
//   lifecycle-bearing component (Explorer/Atlas/Inspector/Toolbar are all named as
//   eventual GetRef consumers by the same ask) -- the last BuildWidgetTree call to
//   (re)register it would silently win for every other already-mounted component's own
//   later OnTick calls too, a real cross-component ref-map collision, not a hypothetical
//   one, since ref names are only unique *within* one BuildWidgetTree call
//   (RefMap's own doc comment), not across separately-built sibling components.
// - Per-instance scoping needs no new iris-proto/nyx-proto API at all: NyxDriverState
//   (Iris/IrisNyxDriver.h) is already a public type, RenderScope is the exact Environment
//   BuildFreeFunctionLifecycleAdapter's own `Scope.context.env->FindOwn("OnTick")` check
//   already reads (IrisNyxDriver.cpp, confirmed directly) to detect the reserved-name
//   locals this same instance's OnMount/OnTick hooks are declared in, and
//   Environment::Define (environment.hpp, already public) creates a binding "in this scope
//   only." Nyx variable lookup is late -- walks the live parent chain at call time, not a
//   snapshot taken when a closure was declared -- so defining GetRef into that same live
//   Environment object, at any point before the first OnMount/OnTick call actually runs,
//   is visible to it with no ordering hazard.
//
// Only reaches Model 1 (free-function) components: Model 2 (class-based) lifecycle hooks
// dispatch via Interpreter::TryCallInstanceMethod against the *file-level* shared
// interpreter/registry (BuildClassLifecycleAdapter, IrisNyxDriver.cpp), never through any
// per-instance Environment at all -- there is no analogous scope to define GetRef into for
// that model today. A class-based OnTick calling GetRef gets an ordinary Nyx-level
// "undefined variable" error, a real and deliberately left-open scope boundary, not a
// silently wrong result.
//
// `Refs` must outlive Node's own mounted lifetime, the same requirement RefMap's own doc
// comment already implies for every other consumer (PenumbraWidgetAdapter.cpp's GetByRef
// registry) -- GetRef's closure captures the raw pointer, not a copy, since it needs to see
// every ref this same BuildWidgetTree call goes on to record after this point too (the
// "favorable ordering fact" docs/next_steps.md's own ask grounds this whole capability in:
// by the time a node's own OnMount fires, every ref belonging to that node's own subtree is
// already recorded in *Refs).
void RegisterGetRefIfPresent(const Component& Node, const BuildContext& Context, RefMap* Refs) {
    if (Context.NyxHost == nullptr || Refs == nullptr || !Node.Instance || !Node.Instance->DriverState) {
        return;
    }
    auto* State = static_cast<Iris::NyxDriverState*>(Node.Instance->DriverState.get());
    if (!State->RenderScope.context.env) {
        return;
    }
    const nyx::runtime::TypeDescriptor* Descriptor = EnsureWidgetRefTypeRegistered(*Context.NyxHost);
    if (Descriptor == nullptr) {
        return;
    }

    // Handles are cached per ref name (not reallocated on every GetRef call, which would
    // otherwise grow unboundedly under a real per-frame OnTick) -- captured by shared_ptr
    // alongside Refs/Descriptor since std::function (NyxCallable::declaration's third
    // alternative) requires a copyable target.
    auto HandleCache = std::make_shared<std::unordered_map<std::string, std::unique_ptr<WidgetRefHandle>>>();
    auto Callable     = std::make_shared<nyx::runtime::NyxCallable>();
    Callable->declaration =
        [Refs, HandleCache, Descriptor](std::vector<nyx::runtime::Value> Args) -> nyx::runtime::Value {
        if (Args.empty() || Args[0].Kind() != nyx::runtime::ValueKind::String) {
            return nyx::runtime::Value();
        }
        const std::string& Name  = std::get<std::string>(Args[0].data);
        const auto          RefIt = Refs->find(Name);
        if (RefIt == Refs->end()) {
            return nyx::runtime::Value();
        }
        std::unique_ptr<WidgetRefHandle>& Handle = (*HandleCache)[Name];
        if (!Handle) {
            Handle = std::make_unique<WidgetRefHandle>(RefIt->second);
        }
        return nyx::host::ToValue(Handle.get(), Descriptor);
    };
    State->RenderScope.context.env->Define("GetRef", nyx::runtime::Value(Callable));
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
// hole). `ThisStyleElement` is Node's own style-target wrapper, already constructed by
// the caller -- passed down so every child's WalkerStyleElement can point back to it as
// their ancestor.
template <typename BoxLikeBuilder>
void BuildAndAttachChildren(BoxLikeBuilder& Builder, const Component& Node, const BuildContext& Context,
                             const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                             StyleMatchStats* Stats) {
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
            Builder.child(std::move(ChildWidget));
        }
    }
}

std::unique_ptr<WidgetBase> BuildFrame(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                                        StyleMatchStats* Stats) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
    return Builder.build();
}

// docs/iris_stage2_decision_doc.md §3 / docs/iris_core_spec.md §3.1: Penumbra has no
// grid layout mode — `<Grid>` maps onto a plain `Box` with `LayoutMode::HorizontalStack`
// as a stub, which explicitly does not meet the Core-primitive requirement yet. `Layout`
// is a public field on `Box`, not something `Box::Builder` exposes — there's no
// `layout()` Builder method to chain, so it's set directly on the built widget.
std::unique_ptr<WidgetBase> BuildGrid(const Component& Node, const BuildContext& Context,
                                       const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                                       StyleMatchStats* Stats) {
    Box::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
    std::unique_ptr<Box> Built = Builder.build();
    Built->Layout = Penumbra::Widgets::LayoutMode::HorizontalStack;
    return Built;
}

std::unique_ptr<WidgetBase> BuildInline(const Component& Node, const BuildContext& Context,
                                         const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                                         StyleMatchStats* Stats) {
    InlineContainer::Builder Builder;
    ApplySharedProps(Builder, Node.Props);
    BuildAndAttachChildren(Builder, Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
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
                                        const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags, RefMap* OutRefs,
                                        StyleMatchStats* Stats) {
    auto Built = std::make_unique<ScrollablePanel>();
    ApplySharedPropsToWidget(*Built, Node.Props);
    if (const auto WheelStep = GetFloatProp(Node.Props, "wheelStep")) {
        Built->WheelStepLogical = *WheelStep;
    }
    for (const Component& Child : Node.Children) {
        if (std::unique_ptr<WidgetBase> ChildWidget = BuildWidgetTreeInternal(
                Child, Context, &ThisStyleElement, /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
            Built->AddChild(std::move(ChildWidget));
        }
    }
    return Built;
}

// <Split> (docs/native_split_backend_wiring_gap.md) -- SplitPanel has no Builder either
// (same "plain fields, not a Builder chain" treatment BuildScroll above already gives
// ScrollablePanel) and takes exactly two children through SetFirst/SetSecond rather than
// a generic Children vector -- Codegen.cpp already enforces exactly two children at
// compile time (`<Split> requires exactly two children`), so real `.iris`-authored trees
// always have both; the size checks below only guard a malformed tree built directly
// (e.g. a structural test bypassing Codegen), matching this walker's existing tolerance
// for other malformed input.
std::unique_ptr<WidgetBase> BuildSplit(const Component& Node, const BuildContext& Context,
                                        const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                        RefMap* OutRefs, StyleMatchStats* Stats) {
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
                                                  /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
            Built->SetFirst(std::move(First));
        }
    }
    if (Node.Children.size() >= 2) {
        if (auto Second = BuildWidgetTreeInternal(Node.Children[1], Context, &ThisStyleElement,
                                                   /*IsComponentRoot=*/false, OutTags, OutRefs, Stats)) {
            Built->SetSecond(std::move(Second));
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

// <Native> (docs/native_split_backend_wiring_gap.md) -- an opaque escape hatch whose
// `build` prop already returns a live Umbra::IWidget handle, not more Component IR to
// walk (Component::NativeBuilder, `vendor/iris/include/Iris/Component.h`). Every real
// Umbra::IWidget in this stack is a PenumbraWidget -- this file and
// PenumbraWidgetAdapter.cpp are the only two places one is ever constructed -- so this
// downcasts to reclaim the real WidgetBase ownership via DetachOwnership(), the same
// owning -> attached transition InsertChildAt (PenumbraWidgetAdapter.cpp) already relies
// on elsewhere, just invoked here instead of there. A `build` prop that hands back some
// other Umbra::IWidget implementation has no real Penumbra widget to unwrap and builds to
// nullptr -- the same "no widget here" treatment None/Slot already get, not an error.
std::unique_ptr<WidgetBase> BuildNative(const Component& Node) {
    if (!Node.NativeBuilder) {
        return nullptr; // Codegen already rejects a <Native> with no build prop
    }
    std::unique_ptr<Umbra::IWidget> Handle = Node.NativeBuilder->Build();
    if (auto* AsPenumbraWidget = dynamic_cast<PenumbraWidget*>(Handle.get())) {
        return AsPenumbraWidget->DetachOwnership();
    }
    return nullptr;
}

std::unique_ptr<WidgetBase> BuildPortal(const Component& Node, const BuildContext& Context,
                                         const WalkerStyleElement& ThisStyleElement, PrimitiveTagMap* OutTags,
                                         RefMap* OutRefs, StyleMatchStats* Stats) {
    std::unique_ptr<WidgetBase> Content;
    if (!Node.Children.empty()) {
        Content = BuildWidgetTreeInternal(Node.Children.front(), Context, &ThisStyleElement,
                                          /*IsComponentRoot=*/false, OutTags, OutRefs, Stats);
    }
    auto Built = std::make_unique<PortalAnchorWidget>(Context.OverlayHost, std::move(Content),
                                                       Iris::ReadPortalProperties(Node.Props));
    ApplySharedPropsToWidget(*Built, Node.Props);
    return Built;
}

std::unique_ptr<WidgetBase> BuildWidgetTreeInternal(const Component& Node, const BuildContext& Context,
                                                     const WalkerStyleElement* ParentStyleElement,
                                                     bool IsComponentRoot, PrimitiveTagMap* OutTags, RefMap* OutRefs,
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
            Built = BuildFrame(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
            break;
        case IrisElementTag::Grid:
            Built = BuildGrid(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
            break;
        case IrisElementTag::Inline:
            Built = BuildInline(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
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
            Built = BuildScroll(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
            break;
        case IrisElementTag::Input:
            Built = BuildInput(Node, Context);
            break;
        case IrisElementTag::Native:
            Built = BuildNative(Node);
            break;
        case IrisElementTag::Portal:
            Built = BuildPortal(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
            break;
        case IrisElementTag::Split:
            Built = BuildSplit(Node, Context, ThisStyleElement, OutTags, OutRefs, Stats);
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

    // Same "record at the one point every built widget funnels through" reasoning as
    // OutTags above -- Node.Ref is only reachable here, not from the built WidgetBase
    // tree PenumbraWidgetAdapter.cpp's wrap step sees later.
    if (Built && OutRefs != nullptr) {
        if (const auto RefName = GetRefName(Node)) {
            (*OutRefs)[*RefName] = Built.get();
        }
    }

    // Same per-node reasoning as OutTags/OutRefs above -- Node.Instance is only
    // reachable here, not from the built WidgetBase tree later, and isn't limited to
    // Node's own root (see RegisterLifecycleIfPresent's own comment). GetRef is defined
    // before RegisterLifecycleIfPresent runs (not after) so it's already reachable from
    // inside the Nyx-authored OnMount body RegisterLifecycleIfPresent's own
    // Host->RegisterLifecycle call triggers immediately below.
    if (Built) {
        RegisterGetRefIfPresent(Node, Context, OutRefs);
        RegisterLifecycleIfPresent(Node, Context, *Built);
    }

    return Built;
}

} // namespace

std::unique_ptr<WidgetBase> BuildWidgetTree(const Component& Node, const BuildContext& Context,
                                             PrimitiveTagMap* OutTags, RefMap* OutRefs) {
    StyleMatchStats Stats;
    std::unique_ptr<WidgetBase> Built = BuildWidgetTreeInternal(
        Node, Context, /*ParentStyleElement=*/nullptr, /*IsComponentRoot=*/true, OutTags, OutRefs, &Stats);
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
