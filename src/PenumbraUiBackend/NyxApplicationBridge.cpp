#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "PenumbraUiBackend/IrisApplication.h"

#include "Penumbra/LifecycleRegistry.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <host/inheritable-type-builder.hpp>

#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>

namespace nyx::host {

// NyxBridge<T> has no generic body (nyx-proto's own doc comment on the primary template)
// -- every bridged type needs its own full specialization. The two below are near-
// identical by necessity, not oversight: `IrisApplication` introduces no lifecycle-hook
// overrides of its own (only new named methods), so `NyxBridge<IrisApplication>` needs
// exactly the same Configure/OnStart/OnUpdate/OnShutdown/OnDpiScaleChanged/InvokeCustom
// bridging `NyxBridge<Penumbra::Application>` already has -- just bridging `IrisApplication`
// instead of bare `Penumbra::Application` as its own base.

template <>
class NyxBridge<Penumbra::Application> : public Penumbra::Application, public NyxBridgeBase {
public:
    Penumbra::Point* GetWindowLogicalSizeForNyx() {
        WindowLogicalSize = Penumbra::Application::GetWindowLogicalSize();
        return &WindowLogicalSize;
    }

    // SetWindowTitle/SetWindowSize -- only meaningful while PendingConfig_ is live, i.e.
    // during a call to Configure() below (Application::Run() constructs the window
    // immediately after Configure() returns, using whatever Config holds by then).
    void SetWindowTitle(const std::string& Title) {
        if (PendingConfig_) PendingConfig_->Title = Title;
    }
    void SetWindowSize(int Width, int Height) {
        if (PendingConfig_) {
            PendingConfig_->WindowLogicalWidth = Width;
            PendingConfig_->WindowLogicalHeight = Height;
        }
    }

    // InvokeCustom -- the public seam NyxApplicationBridgeT::CallApplicationMethod
    // reaches through: same dispatch Invoke() uses (NyxBridgeBase::interp_/nyxInstance_),
    // just public and taking an already-built Value vector rather than marshalling
    // variadic C++ args, for a caller that doesn't know MethodName's signature at compile
    // time.
    std::optional<runtime::Value> InvokeCustom(const std::string& MethodName,
                                               std::vector<runtime::Value> Args) {
        try {
            return interp_->TryCallInstanceMethod(nyxInstance_, MethodName, std::move(Args));
        } catch (const runtime::RuntimeError& Error) {
            auto Obj = std::make_shared<runtime::NyxObject>();
            Obj->typeName = "Error::Unknown";
            Obj->adHocFields = {{"message", runtime::Value(std::string(Error.what()))}};
            return runtime::Value(Obj);
        }
    }

    void Configure(Penumbra::ApplicationConfig& Config) override {
        PendingConfig_ = &Config;
        Invoke("Configure"); // no Nyx override -> Config keeps its ApplicationConfig() defaults
        PendingConfig_ = nullptr;
    }

    bool OnStart() override {
        if (std::optional<runtime::Value> Result = Invoke("OnStart")) {
            return FromValue<bool>(*Result);
        }
        return Penumbra::Application::OnStart();
    }

    void OnUpdate(float DeltaSeconds) override {
        if (!Invoke("OnUpdate", DeltaSeconds)) {
            Penumbra::Application::OnUpdate(DeltaSeconds);
        }
    }

    void OnShutdown() override {
        if (!Invoke("OnShutdown")) {
            Penumbra::Application::OnShutdown();
        }
    }

    void OnDpiScaleChanged(float NewDpiScaleFactor) override {
        if (!Invoke("OnDpiScaleChanged", NewDpiScaleFactor)) {
            Penumbra::Application::OnDpiScaleChanged(NewDpiScaleFactor);
        }
    }

private:
    Penumbra::Point WindowLogicalSize;
    Penumbra::ApplicationConfig* PendingConfig_ = nullptr;
};

template <>
class NyxBridge<PenumbraUiBackend::IrisApplication> : public PenumbraUiBackend::IrisApplication, public NyxBridgeBase {
public:
    Penumbra::Point* GetWindowLogicalSizeForNyx() {
        WindowLogicalSize = PenumbraUiBackend::IrisApplication::GetWindowLogicalSize();
        return &WindowLogicalSize;
    }

    void SetWindowTitle(const std::string& Title) {
        if (PendingConfig_) PendingConfig_->Title = Title;
    }
    void SetWindowSize(int Width, int Height) {
        if (PendingConfig_) {
            PendingConfig_->WindowLogicalWidth = Width;
            PendingConfig_->WindowLogicalHeight = Height;
        }
    }

    std::optional<runtime::Value> InvokeCustom(const std::string& MethodName,
                                               std::vector<runtime::Value> Args) {
        try {
            return interp_->TryCallInstanceMethod(nyxInstance_, MethodName, std::move(Args));
        } catch (const runtime::RuntimeError& Error) {
            auto Obj = std::make_shared<runtime::NyxObject>();
            Obj->typeName = "Error::Unknown";
            Obj->adHocFields = {{"message", runtime::Value(std::string(Error.what()))}};
            return runtime::Value(Obj);
        }
    }

    void Configure(Penumbra::ApplicationConfig& Config) override {
        PendingConfig_ = &Config;
        Invoke("Configure");
        PendingConfig_ = nullptr;
    }

    bool OnStart() override {
        if (std::optional<runtime::Value> Result = Invoke("OnStart")) {
            return FromValue<bool>(*Result);
        }
        return PenumbraUiBackend::IrisApplication::OnStart();
    }

    void OnUpdate(float DeltaSeconds) override {
        if (!Invoke("OnUpdate", DeltaSeconds)) {
            PenumbraUiBackend::IrisApplication::OnUpdate(DeltaSeconds);
        }
    }

    void OnShutdown() override {
        if (!Invoke("OnShutdown")) {
            PenumbraUiBackend::IrisApplication::OnShutdown();
        }
    }

    void OnDpiScaleChanged(float NewDpiScaleFactor) override {
        if (!Invoke("OnDpiScaleChanged", NewDpiScaleFactor)) {
            PenumbraUiBackend::IrisApplication::OnDpiScaleChanged(NewDpiScaleFactor);
        }
    }

private:
    Penumbra::Point WindowLogicalSize;
    Penumbra::ApplicationConfig* PendingConfig_ = nullptr;
};

} // namespace nyx::host

namespace PenumbraUiBackend {

namespace {

float PointX(const Penumbra::Point& Value) { return Value.X; }
float PointY(const Penumbra::Point& Value) { return Value.Y; }

// Templated over AppBaseT so one RegisterApplicationType<AppBaseT>() body serves both
// bridge instantiations -- each reaches through to the matching NyxBridge<AppBaseT>
// specialization above for the bridge-only members (GetWindowLogicalSizeForNyx/
// SetWindowTitle/SetWindowSize), and to AppBaseT's own inherited Penumbra::Application
// surface directly for everything else (GetFontBackend/GetLifecycleRegistry/SetRootWidget
// are public on Penumbra::Application and inherited unchanged by IrisApplication, so no
// bridge-specific reach-through is needed for those).
template <typename AppBaseT>
Penumbra::Point* GetWindowLogicalSizeForNyx(AppBaseT& Self) {
    return static_cast<nyx::host::NyxBridge<AppBaseT>&>(Self).GetWindowLogicalSizeForNyx();
}

template <typename AppBaseT>
Penumbra::Render::IFontBackend* GetFontBackendForNyx(AppBaseT& Self) {
    return &Self.GetFontBackend();
}

template <typename AppBaseT>
Penumbra::LifecycleRegistry* GetLifecycleRegistryForNyx(AppBaseT& Self) {
    return &Self.GetLifecycleRegistry();
}

template <typename AppBaseT>
void SetRootWidgetFromNyx(AppBaseT& Self, Penumbra::Widgets::WidgetBase* Root) {
    Self.SetRootWidget(std::unique_ptr<Penumbra::Widgets::WidgetBase>(Root));
}

template <typename AppBaseT>
void SetWindowTitleForNyx(AppBaseT& Self, const std::string& Title) {
    static_cast<nyx::host::NyxBridge<AppBaseT>&>(Self).SetWindowTitle(Title);
}

template <typename AppBaseT>
void SetWindowSizeForNyx(AppBaseT& Self, int Width, int Height) {
    static_cast<nyx::host::NyxBridge<AppBaseT>&>(Self).SetWindowSize(Width, Height);
}

template <typename T>
const nyx::runtime::TypeDescriptor* RegisterOpaqueType(nyx::host::NyxRuntime& Runtime,
                                                        const std::string& Name) {
    Runtime.RegisterType<T>(Name);
    return std::get<std::shared_ptr<nyx::runtime::HostObject>>(Runtime.Globals().at(Name).data)->descriptor;
}

} // namespace

template <typename AppBaseT>
NyxApplicationBridgeT<AppBaseT>::NyxApplicationBridgeT(Iris::IrisConfig Config, std::string ProjectRoot)
    : Runtime_(), IrisDriver_(std::move(Config), ProjectRoot, Runtime_), ProjectRoot_(std::move(ProjectRoot)) {}

template <typename AppBaseT>
nyx::host::NyxRuntime& NyxApplicationBridgeT<AppBaseT>::Runtime() { return Runtime_; }

template <typename AppBaseT>
Iris::IrisNyxDriver& NyxApplicationBridgeT<AppBaseT>::IrisDriver() { return IrisDriver_; }

template <typename AppBaseT>
void NyxApplicationBridgeT<AppBaseT>::RegisterApplicationType() {
    if (ApplicationTypeRegistered_) return;

    Runtime_.RegisterType<Penumbra::Point>("PenumbraPoint").Method("X", &PointX).Method("Y", &PointY);
    const auto* PointDescriptor =
        std::get<std::shared_ptr<nyx::runtime::HostObject>>(Runtime_.Globals().at("PenumbraPoint").data)
            ->descriptor;
    const auto* FontBackendDescriptor =
        RegisterOpaqueType<Penumbra::Render::IFontBackend>(Runtime_, "PenumbraFontBackend");
    const auto* LifecycleRegistryDescriptor =
        RegisterOpaqueType<Penumbra::LifecycleRegistry>(Runtime_, "PenumbraLifecycleRegistry");
    const auto* WidgetDescriptor =
        RegisterOpaqueType<Penumbra::Widgets::WidgetBase>(Runtime_, "PenumbraWidget");

    // InheritableTypeBuilder is move-only, non-copyable, and its destructor commits the
    // accumulated descriptor unless moved-from -- the original (non-templated) version of
    // this method never named the builder at all, just chained-and-discarded it as one
    // statement, relying on that destructor. This version needs the same builder to reach
    // past the chain (into RegisterIrisApplicationMethods below, for the IrisApplication
    // case only), so it's explicitly move-constructed into a real local instead -- the
    // chain's own temporary is destroyed at the end of this initializer statement, moved-
    // from and so a no-op; `Builder` here is what actually commits, at the end of this
    // function.
    // &AppBaseT::Xxx, for a method only ever *inherited* (never redeclared) by AppBaseT,
    // keeps the pointer-to-member's own nominal type as Penumbra::Application::* (where
    // the member actually lives), not AppBaseT::* -- pointer-to-member types name their
    // declaring class, not whatever lookup path found them. InheritableTypeBuilder<T>'s
    // own Method/PointerMethod overloads deduce Ret/Args by structurally matching the
    // parameter's T::* against the argument's own type (T already fixed to AppBaseT here,
    // not itself being deduced) -- template deduction doesn't apply the ordinary implicit
    // Base::* -> Derived::* conversion the way plain initialization/assignment would, so
    // an inherited-only member's address needs an explicit static_cast to the exact
    // AppBaseT::* type deduction needs to match structurally.
    nyx::host::InheritableTypeBuilder<AppBaseT> Builder =
        std::move(Runtime_.RegisterInheritableType<AppBaseT>("Application")
            .Method("RequestQuit", static_cast<void (AppBaseT::*)()>(&AppBaseT::RequestQuit))
            .PointerMethod("GetWindowLogicalSize", &GetWindowLogicalSizeForNyx<AppBaseT>, PointDescriptor)
            .Method("GetDpiScaleFactor", static_cast<float (AppBaseT::*)() const>(&AppBaseT::GetDpiScaleFactor))
            .PointerMethod("GetFontBackend", &GetFontBackendForNyx<AppBaseT>, FontBackendDescriptor)
            .Method("SetTextInputActive", static_cast<void (AppBaseT::*)(bool)>(&AppBaseT::SetTextInputActive))
            .Method("SetRootWidget", &SetRootWidgetFromNyx<AppBaseT>)
            .Method("SetWindowTitle", &SetWindowTitleForNyx<AppBaseT>)
            .Method("SetWindowSize", &SetWindowSizeForNyx<AppBaseT>)
            .PointerMethod("GetRootWidget",
                           static_cast<Penumbra::Widgets::WidgetBase* (AppBaseT::*)() const>(&AppBaseT::GetRootWidget),
                           WidgetDescriptor)
            .Method("GetRootWidgetConsumedInputThisFrame",
                    static_cast<bool (AppBaseT::*)() const>(&AppBaseT::GetRootWidgetConsumedInputThisFrame))
            .PointerMethod("GetLifecycleRegistry", &GetLifecycleRegistryForNyx<AppBaseT>, LifecycleRegistryDescriptor)
            .Override("OnStart", +[](AppBaseT& Self) -> bool {
                return Self.Penumbra::Application::OnStart();
            })
            .Override("OnUpdate", +[](AppBaseT& Self, float DeltaSeconds) {
                Self.Penumbra::Application::OnUpdate(DeltaSeconds);
            })
            .Override("OnShutdown", +[](AppBaseT& Self) {
                Self.Penumbra::Application::OnShutdown();
            })
            .Override("OnDpiScaleChanged", +[](AppBaseT& Self, float NewDpiScaleFactor) {
                Self.Penumbra::Application::OnDpiScaleChanged(NewDpiScaleFactor);
            }));

    if constexpr (std::is_same_v<AppBaseT, IrisApplication>) {
        RegisterIrisApplicationMethods(Builder, WidgetDescriptor);
    }
    ApplicationTypeRegistered_ = true;
}

template <typename AppBaseT>
AppBaseT* NyxApplicationBridgeT<AppBaseT>::LoadApplication(
    const std::string& Source, const std::string& Filename, const std::string& ApplicationClassName) {
    RegisterApplicationType();
    try {
        auto Scope = Runtime_.MountBridged<AppBaseT>(Source, Filename, ApplicationClassName);
        Interpreters_.push_back(Scope.interpreter);
        AppBaseT& App = Scope.Get();
        if constexpr (std::is_same_v<AppBaseT, IrisApplication>) {
            App.Attach(IrisDriver_, ProjectRoot_);
        }
        return &App;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "PenumbraUiBackend::NyxApplicationBridgeT::LoadApplication: %s: %s\n",
                     Filename.c_str(), Error.what());
        return nullptr;
    }
}

template <typename AppBaseT>
AppBaseT* NyxApplicationBridgeT<AppBaseT>::LoadApplicationFromFile(
    const std::filesystem::path& Path, const std::string& ApplicationClassName) {
    std::ifstream File(Path);
    if (!File) {
        std::fprintf(stderr,
                     "PenumbraUiBackend::NyxApplicationBridgeT::LoadApplicationFromFile: cannot open '%s'\n",
                     Path.string().c_str());
        return nullptr;
    }
    std::ostringstream Contents;
    Contents << File.rdbuf();
    return LoadApplication(Contents.str(), Path.filename().string(), ApplicationClassName);
}

template <typename AppBaseT>
std::optional<nyx::runtime::Value> NyxApplicationBridgeT<AppBaseT>::CallApplicationMethod(
    AppBaseT& App, const std::string& MethodName, std::vector<nyx::runtime::Value> Args) {
    return static_cast<nyx::host::NyxBridge<AppBaseT>&>(App).InvokeCustom(MethodName, std::move(Args));
}

template <typename AppBaseT>
nyx::runtime::Value NyxApplicationBridgeT<AppBaseT>::GetApplicationInstanceValue(AppBaseT& App) {
    return static_cast<nyx::host::NyxBridge<AppBaseT>&>(App).NyxInstanceValue();
}

template class NyxApplicationBridgeT<Penumbra::Application>;
template class NyxApplicationBridgeT<IrisApplication>;

} // namespace PenumbraUiBackend
