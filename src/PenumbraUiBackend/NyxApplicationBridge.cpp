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

} // namespace PenumbraUiBackend
