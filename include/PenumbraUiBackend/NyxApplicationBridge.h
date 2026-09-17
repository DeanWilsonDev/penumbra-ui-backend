#pragma once

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"

#include "host/nyx-runtime.hpp"
#include "interpreter/interpreter.hpp"

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PenumbraUiBackend {

// Forward-declared only -- a consumer that never uses `IrisApplicationBridge` (every
// existing one, today) doesn't need `IrisApplication.h`'s own dependencies pulled in just
// to see this header. `NyxApplicationBridge.cpp` includes the real header, since it needs
// `IrisApplication` complete to bridge it.
class IrisApplication;

// Owns the single Nyx runtime shared by one Nyx-authored Penumbra application and its
// Iris UI driver, templated over which native C++ type a Nyx-authored `class Foo :
// Application { ... }` is actually bridged to.
//
// `NyxApplicationBridge` (`AppBaseT = Penumbra::Application`) is the original, unchanged
// behavior: every existing consumer (this alias's own name is kept stable specifically so
// none of them need a source change). `IrisApplicationBridge` (`AppBaseT =
// IrisApplication`, `IrisApplication.h`) additionally exposes `IrisApplication`'s own
// Iris/Lustre/Penumbra mounting methods as inherited Nyx-callable methods on the same
// Nyx-visible `"Application"` name -- see `IrisApplication.h`'s own doc comment for why
// this is a single new bridge specialization sharing one Nyx-visible type name, not two
// independently-registered inheritable types (nyx-proto resolves a Nyx object's host
// bridge once, at the root of its class chain, by a single string lookup -- two
// independent `InheritableTypeDescriptor`s can't stack over one instance unless they
// share the same bridge `T`).
//
// Mounted application interpreters are retained because `NyxBridge<AppBaseT>` stores a
// non-owning pointer to its interpreter. Member order is intentional: `Runtime_` outlives
// both `IrisDriver_` and `Interpreters_`.
template <typename AppBaseT>
class NyxApplicationBridgeT {
public:
    NyxApplicationBridgeT(Iris::IrisConfig Config, std::string ProjectRoot);

    NyxApplicationBridgeT(const NyxApplicationBridgeT&) = delete;
    NyxApplicationBridgeT& operator=(const NyxApplicationBridgeT&) = delete;
    NyxApplicationBridgeT(NyxApplicationBridgeT&&) = delete;
    NyxApplicationBridgeT& operator=(NyxApplicationBridgeT&&) = delete;

    [[nodiscard]] nyx::host::NyxRuntime& Runtime();
    [[nodiscard]] Iris::IrisNyxDriver& IrisDriver();

    // Instantiates ApplicationClassName from Source as a real AppBaseT (always actually a
    // real Penumbra::Application, since AppBaseT always derives from it). Returns nullptr
    // on parse, interpretation, or type errors. The caller owns the returned application;
    // this bridge must outlive it so the retained interpreter and runtime remain valid.
    // When AppBaseT == IrisApplication, this also calls the fresh instance's own
    // `Attach(IrisDriver(), ProjectRoot)` before returning it (an `if constexpr`, in the
    // .cpp) -- an app-specific `SetFontConfig` call is still the caller's own job.
    AppBaseT* LoadApplication(const std::string& Source, const std::string& Filename,
                              const std::string& ApplicationClassName);

    // LoadApplication, reading Source from Path first. Returns nullptr when the file
    // cannot be opened.
    AppBaseT* LoadApplicationFromFile(const std::filesystem::path& Path,
                                      const std::string& ApplicationClassName);

    // Calls a method defined directly on App's own loaded Nyx class -- one that isn't one
    // of the four lifecycle hooks (OnStart/OnUpdate/OnShutdown/OnDpiScaleChanged) already
    // wired through Application's own virtual dispatch. This is how a native free
    // function registered on Runtime() (e.g. a click handler another .irisx file calls by
    // bare name) reaches custom orchestration methods a host's Nyx-authored Application
    // subclass defines for itself -- nyx::host::NyxBridgeBase::Invoke already does exactly
    // this dispatch, but it's protected and the concrete bridge type is private to
    // NyxApplicationBridge.cpp; this is the one seam that reaches it. App must be a live
    // object this same bridge returned from LoadApplication[FromFile]. Returns nullopt if
    // the Nyx class defines no method named MethodName.
    [[nodiscard]] std::optional<nyx::runtime::Value> CallApplicationMethod(
        AppBaseT& App, const std::string& MethodName, std::vector<nyx::runtime::Value> Args = {});

private:
    void RegisterApplicationType();

    nyx::host::NyxRuntime                                      Runtime_;
    Iris::IrisNyxDriver                                        IrisDriver_;
    std::string                                                 ProjectRoot_;
    std::vector<std::shared_ptr<nyx::interpreter::Interpreter>> Interpreters_;
    bool                                                        ApplicationTypeRegistered_{false};
};

using NyxApplicationBridge  = NyxApplicationBridgeT<Penumbra::Application>;
using IrisApplicationBridge = NyxApplicationBridgeT<IrisApplication>;

// The forwarder helper: turns one bare-name registration (an .irisx file's own click
// handler, list-data-source, etc. calling a native free function by name) into a
// one-line call, rather than every consumer hand-writing the same "look up the live app
// instance, forward through CallApplicationMethod" lambda per name. `PreDispatch`, when
// given, runs before every dispatch through this registration -- e.g. reading an
// `iris::Signal` at exactly the point Iris's Stage-3 reconciler needs that read to happen
// for dependency tracking to work (an app's own reactivity concern, not a generic one --
// see the call site, not this helper, for why a particular call needs it).
template <typename AppBaseT>
void RegisterInstanceForwarders(nyx::host::NyxRuntime& Runtime, NyxApplicationBridgeT<AppBaseT>& Bridge,
                                 AppBaseT& App, std::initializer_list<const char*> Names,
                                 std::function<void()> PreDispatch = nullptr) {
    for (const char* Name : Names) {
        Runtime.RegisterFunction(Name, [&Bridge, &App, Name, PreDispatch](std::vector<nyx::runtime::Value> Args) {
            if (PreDispatch) PreDispatch();
            std::optional<nyx::runtime::Value> Result = Bridge.CallApplicationMethod(App, Name, std::move(Args));
            return Result ? *Result : nyx::runtime::Value();
        });
    }
}

} // namespace PenumbraUiBackend
