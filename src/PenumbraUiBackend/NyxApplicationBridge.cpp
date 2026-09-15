#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Penumbra/LifecycleRegistry.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace nyx::host {

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

    // InvokeCustom -- the public seam NyxApplicationBridge::CallApplicationMethod reaches
    // through: same dispatch Invoke() uses (NyxBridgeBase::interp_/nyxInstance_), just
    // public and taking an already-built Value vector rather than marshalling variadic
    // C++ args, for a caller that doesn't know MethodName's signature at compile time.
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

} // namespace nyx::host

namespace PenumbraUiBackend {

namespace {

float PointX(const Penumbra::Point& Value) { return Value.X; }
float PointY(const Penumbra::Point& Value) { return Value.Y; }

Penumbra::Point* GetWindowLogicalSizeForNyx(Penumbra::Application& Self) {
    auto& Bridge = static_cast<nyx::host::NyxBridge<Penumbra::Application>&>(Self);
    return Bridge.GetWindowLogicalSizeForNyx();
}

Penumbra::Render::IFontBackend* GetFontBackendForNyx(Penumbra::Application& Self) {
    return &Self.GetFontBackend();
}

Penumbra::LifecycleRegistry* GetLifecycleRegistryForNyx(Penumbra::Application& Self) {
    return &Self.GetLifecycleRegistry();
}

void SetRootWidgetFromNyx(Penumbra::Application& Self, Penumbra::Widgets::WidgetBase* Root) {
    Self.SetRootWidget(std::unique_ptr<Penumbra::Widgets::WidgetBase>(Root));
}

void SetWindowTitleForNyx(Penumbra::Application& Self, const std::string& Title) {
    static_cast<nyx::host::NyxBridge<Penumbra::Application>&>(Self).SetWindowTitle(Title);
}

void SetWindowSizeForNyx(Penumbra::Application& Self, int Width, int Height) {
    static_cast<nyx::host::NyxBridge<Penumbra::Application>&>(Self).SetWindowSize(Width, Height);
}

template <typename T>
const nyx::runtime::TypeDescriptor* RegisterOpaqueType(nyx::host::NyxRuntime& Runtime,
                                                        const std::string& Name) {
    Runtime.RegisterType<T>(Name);
    return std::get<std::shared_ptr<nyx::runtime::HostObject>>(Runtime.Globals().at(Name).data)->descriptor;
}

} // namespace

NyxApplicationBridge::NyxApplicationBridge(Iris::IrisConfig Config, std::string ProjectRoot)
    : Runtime_(), IrisDriver_(std::move(Config), std::move(ProjectRoot), Runtime_) {}

nyx::host::NyxRuntime& NyxApplicationBridge::Runtime() { return Runtime_; }

Iris::IrisNyxDriver& NyxApplicationBridge::IrisDriver() { return IrisDriver_; }

void NyxApplicationBridge::RegisterApplicationType() {
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

    Runtime_.RegisterInheritableType<Penumbra::Application>("Application")
        .Method("RequestQuit", &Penumbra::Application::RequestQuit)
        .PointerMethod("GetWindowLogicalSize", &GetWindowLogicalSizeForNyx, PointDescriptor)
        .Method("GetDpiScaleFactor", &Penumbra::Application::GetDpiScaleFactor)
        .PointerMethod("GetFontBackend", &GetFontBackendForNyx, FontBackendDescriptor)
        .Method("SetTextInputActive", &Penumbra::Application::SetTextInputActive)
        .Method("SetRootWidget", &SetRootWidgetFromNyx)
        .Method("SetWindowTitle", &SetWindowTitleForNyx)
        .Method("SetWindowSize", &SetWindowSizeForNyx)
        .PointerMethod("GetRootWidget", &Penumbra::Application::GetRootWidget, WidgetDescriptor)
        .Method("GetRootWidgetConsumedInputThisFrame",
                &Penumbra::Application::GetRootWidgetConsumedInputThisFrame)
        .PointerMethod("GetLifecycleRegistry", &GetLifecycleRegistryForNyx, LifecycleRegistryDescriptor)
        .Override("OnStart", +[](Penumbra::Application& Self) -> bool {
            return Self.Penumbra::Application::OnStart();
        })
        .Override("OnUpdate", +[](Penumbra::Application& Self, float DeltaSeconds) {
            Self.Penumbra::Application::OnUpdate(DeltaSeconds);
        })
        .Override("OnShutdown", +[](Penumbra::Application& Self) {
            Self.Penumbra::Application::OnShutdown();
        })
        .Override("OnDpiScaleChanged", +[](Penumbra::Application& Self, float NewDpiScaleFactor) {
            Self.Penumbra::Application::OnDpiScaleChanged(NewDpiScaleFactor);
        });
    ApplicationTypeRegistered_ = true;
}

Penumbra::Application* NyxApplicationBridge::LoadApplication(
    const std::string& Source, const std::string& Filename, const std::string& ApplicationClassName) {
    RegisterApplicationType();
    try {
        auto Scope = Runtime_.MountBridged<Penumbra::Application>(Source, Filename, ApplicationClassName);
        Interpreters_.push_back(Scope.interpreter);
        return &Scope.Get();
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "PenumbraUiBackend::NyxApplicationBridge::LoadApplication: %s: %s\n",
                     Filename.c_str(), Error.what());
        return nullptr;
    }
}

Penumbra::Application* NyxApplicationBridge::LoadApplicationFromFile(
    const std::filesystem::path& Path, const std::string& ApplicationClassName) {
    std::ifstream File(Path);
    if (!File) {
        std::fprintf(stderr,
                     "PenumbraUiBackend::NyxApplicationBridge::LoadApplicationFromFile: cannot open '%s'\n",
                     Path.string().c_str());
        return nullptr;
    }
    std::ostringstream Contents;
    Contents << File.rdbuf();
    return LoadApplication(Contents.str(), Path.filename().string(), ApplicationClassName);
}

std::optional<nyx::runtime::Value> NyxApplicationBridge::CallApplicationMethod(
    Penumbra::Application& App, const std::string& MethodName, std::vector<nyx::runtime::Value> Args) {
    return static_cast<nyx::host::NyxBridge<Penumbra::Application>&>(App).InvokeCustom(MethodName, std::move(Args));
}

} // namespace PenumbraUiBackend
