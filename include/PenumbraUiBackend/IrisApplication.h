#pragma once

#include "PenumbraUiBackend/Lustre/StyleApplier.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Iris/Component.h"
#include "Iris/IrisNyxDriver.h"
#include "Iris/SlotRuntime.h"

#include "Penumbra/Application.h"
#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/FocusState.h"
#include "Penumbra/Widgets/OverlayHost.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <host/nyx-runtime.hpp>
#include <runtime/value.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nyx::host {
template <typename T>
class InheritableTypeBuilder;
} // namespace nyx::host

namespace Penumbra::Widgets {
class Box;
} // namespace Penumbra::Widgets

namespace PenumbraUiBackend {

// Owns the Iris/Lustre/Penumbra mounting machinery every Iris-driven Penumbra
// application needs, regardless of what language authors its own orchestration
// (OnStart/OnUpdate/click handlers): stylesheet loading by name, font/DPI reload,
// generic single-component mounting (widget + `ref` map), and the app-root
// mount/OverlayHost/SetRootWidget lifecycle. `Cairn`'s own `src/main.cpp` hand-rolled all
// of this before this class existed -- see cairn's own `eliminate-native-business-logic-
// board-kanban-to-nyx`/`reduce main.cpp` epics for why that was true even after every
// piece of *business* logic left native code: this is the boilerplate any Iris app still
// needs regardless.
//
// **Host-language-agnostic on purpose.** A plain C++ application subclasses this directly
// (overriding `OnStart`/`OnUpdate`/etc. as ordinary `Penumbra::Application` virtuals,
// calling the methods below directly -- no Nyx runtime involved in its own orchestration
// at all, though `.irisx` UI files it mounts are still Nyx/Chaos-IR under the hood, since
// that's what Iris itself compiles to). A Nyx-authored application reaches the exact same
// methods as *inherited* Nyx-callable methods instead, via `IrisApplicationBridge`
// (`NyxApplicationBridge.h`'s own `NyxApplicationBridgeT<IrisApplication>` alias), which
// bridges this class the same way `NyxApplicationBridge` (`NyxApplicationBridgeT<
// Penumbra::Application>`) already bridges plain `Penumbra::Application` -- see that
// header's own doc comment for why both share one template rather than two independently
// duplicated bridge classes.
//
// **Must stay default-constructible.** `nyx::host::InheritableTypeBuilder<T>`'s bridge
// construction is `new NyxBridge<T>()`, unconditionally zero-argument (nyx-proto's own
// `nyx-runtime.hpp`) -- there is no way to inject constructor parameters through that path.
// All setup here happens via `Attach`/`SetFontConfig`, called once, post-construction, by
// whichever bootstrap owns the instance: `NyxApplicationBridgeT<IrisApplication>` calls
// both automatically right after a successful `LoadApplication[FromFile]`; a plain C++
// subclass's own `CreateApplication()`-equivalent calls them by hand instead.
//
// **Not exposed to Nyx at all: `MountComponent`.** `nyx::host::FromValue`/`ToValue`
// (nyx-proto's own `marshal.hpp`) only marshal primitives or registered host-type
// pointers -- an arbitrary `vector<Value> Args` parameter can't cross that boundary. So
// `MountComponent` stays C++-only, called by an app's own native `RegisterFunction`-
// registered functions (which already receive `vector<Value>` from Nyx through the
// ordinary `RegisterFunction` path -- no new marshaling boundary crossed there), or
// directly by a plain C++ subclass. Only the zero/primitive-arg entry points below
// (`ReloadFont`, `MountAppRoot`, `TeardownRootWidget`, `TickIris`,
// `GetRef`) are registered onto the Nyx-visible surface (see `RegisterIrisApplicationMethods`
// below, called from `NyxApplicationBridgeT<IrisApplication>::RegisterApplicationType()`).
class IrisApplication : public Penumbra::Application {
public:
    IrisApplication() = default;
    ~IrisApplication() override = default;

    // Called once, post-construction, before anything else on this class. `Driver` must
    // outlive this object (same ownership shape `NyxApplicationBridgeT` already gives its
    // own `IrisDriver_` relative to the `Application*` it hands back). `UiDir` is the
    // directory every `.irisx`/`.lustre` file passed to the methods below is resolved
    // relative to.
    void Attach(Iris::IrisNyxDriver& Driver, std::string UiDir);

    // Called once, any time before the first `ReloadFont` call (typically right after
    // `Attach`). `FontPath`/`FontSizeLogical` replace what used to be a compile-time
    // `#define` baked directly into a hand-rolled `ReloadFont` (e.g. Cairn's own former
    // `CAIRN_FONT_PATH`/`14.0f`) -- now an app-supplied value instead.
    void SetFontConfig(std::string FontPath, float FontSizeLogical);

    // -- Font / DPI --

    // Reloads the configured font at DpiScaleFactor (`IFontBackend::LoadFont`). Returns
    // false (does nothing) if `SetFontConfig` was never called. Call again whenever the
    // window's own DPI scale changes (`Penumbra::Application::OnDpiScaleChanged`) or once
    // up front during `OnStart`.
    bool ReloadFont(float DpiScaleFactor);
    Penumbra::Render::FontHandle CurrentFont() const { return Font_; }

    // The one `LustreStyleApplier` shared by every mount this instance performs, built
    // lazily against `GetFontBackend()` on first access (matching every existing caller's
    // own former "construct once in OnStart, reuse forever" behavior -- never rebuilt on a
    // later DPI change, same as before this class existed).
    const Lustre::LustreStyleApplier& StyleApplier();

    // -- Generic single-component mount --

    struct MountResult {
        std::unique_ptr<Penumbra::Widgets::WidgetBase> Widget;
        RefMap                                          Refs;
    };

    // Mounts `File`'s own `FunctionName` component (via `IrisDriver().MountRoot`) and
    // builds it into a real Penumbra widget tree (`PenumbraUiBackend::BuildWidgetTree`),
    // styled against every `.lustre` file discovered transitively from `File`'s own
    // `import` graph (`EnsureStylesheetsFor`). The returned `Iris::Component` is pushed
    // onto `KeepAlive` -- the caller must keep that vector alive for as long as the
    // returned widget (or anything it mounted, e.g. a `<Slot>`/`onRelease` closure) stays
    // live: those closures hold a raw reference into the `Component`'s own
    // `ComponentInstance::DriverState`, which is destroyed with the `Component` itself
    // (`IrisNyxDriver::MountRoot`'s own doc comment).
    MountResult MountComponent(const std::string& File, const std::string& FunctionName,
                                std::vector<nyx::runtime::Value>               Args,
                                std::vector<std::shared_ptr<Iris::Component>>& KeepAlive);

    // -- App-root mount + OverlayHost/root-widget lifecycle --

    // Mounts `File`'s own `FunctionName` component as this application's root: builds it,
    // wraps the result in a fresh
    // `Penumbra::Widgets::OverlayHost` (so a `<Portal>` anywhere in the mounted tree has
    // somewhere to present), and calls `SetRootWidget`. Every `ref`-tagged node in the
    // mounted tree becomes retrievable via `GetRef` afterward. Returns false, logging to
    // stderr, on a mount error (`IrisDriver().Errors()` grew) -- does not touch
    // `SetRootWidget` in that case. Safe to call again later (e.g. on a DPI change) --
    // replaces the previous root and ref set outright.
    bool MountAppRoot(const std::string& File, const std::string& FunctionName);

    // A `ref`-tagged widget from the most recent `MountAppRoot` call, or nullptr if no
    // node carried that ref (or `MountAppRoot` was never called).
    Penumbra::Widgets::WidgetBase* GetRef(const std::string& Name) const;

    void TeardownRootWidget();

    bool MountReconciledComponent(const std::string& File, const std::string& FunctionName,
                                   std::vector<nyx::runtime::Value> Args, const std::string& TargetRefName);

    void TeardownReconciledComponent(const std::string& TargetRefName);

    void TickIris();

    const ::Lustre::StylesheetSet& ComposedStylesheet() const { return ComposedStyleSet_; }

    Iris::IrisNyxDriver& IrisDriver() { return *Driver_; }

protected:
    Iris::IrisNyxDriver* Driver_ = nullptr;
    std::string           UiDir_;
    std::string           FontPath_;
    float                 FontSizeLogical_ = 14.0f;
    Penumbra::Render::FontHandle Font_{0};

    ::Lustre::Stylesheet            ComposedSheet_;
    ::Lustre::StylesheetSet         ComposedStyleSet_{nullptr, &ComposedSheet_};
    std::unordered_set<std::string> DiscoveredStylesheetFiles_;
    std::unordered_set<std::string> DiscoveredStylesheetDirs_;
    std::optional<Lustre::LustreStyleApplier> Applier_;

    void EnsureStylesheetsFor(const std::string& EntryResolvedPath);

    BuildContext MakeBuildContext();

    Iris::Component AppRoot_;
    RefMap           AppRootRefs_;
    Penumbra::Widgets::OverlayHost* OverlayHostPtr_ = nullptr;
    Penumbra::Widgets::FocusState   Focus_;

    std::unique_ptr<PenumbraWidget>               AppRootWrapper_;
    std::vector<std::unique_ptr<iris::SlotState>> AppRootSlots_;

    struct ReconciledMount {
        std::vector<std::shared_ptr<Iris::Component>> Roots;
        std::unique_ptr<PenumbraWidget>                 Wrapper;
        std::vector<std::unique_ptr<iris::SlotState>>   Slots;
    };
    void ClearReconciledMount(ReconciledMount& Mount, Penumbra::Widgets::Box* Target);
    std::unordered_map<std::string, ReconciledMount> ReconciledMounts_;
};

// Adds IrisApplication's own methods onto Builder -- called from
// `NyxApplicationBridgeT<AppBaseT>::RegisterApplicationType()` only when `AppBaseT ==
// IrisApplication` (an `if constexpr` in that method, so a plain `NyxApplicationBridgeT<
// Penumbra::Application>` instantiation never instantiates this at all). `WidgetDescriptor`
// is the already-registered `TypeDescriptor` for `PenumbraWidget` (`NyxApplicationBridge.cpp`'s
// own `RegisterOpaqueType<Penumbra::Widgets::WidgetBase>` call), reused here for `GetRef`'s
// own `PointerMethod` registration rather than registering it a second time.
void RegisterIrisApplicationMethods(nyx::host::InheritableTypeBuilder<IrisApplication>& Builder,
                                     const nyx::runtime::TypeDescriptor*                  WidgetDescriptor);

} // namespace PenumbraUiBackend
