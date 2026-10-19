#include "Penumbra/EntryPoint.h"
#include "Penumbra/Render/Color.h"
#include "Penumbra/Widgets/Box.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr int kAutoQuitAfterFrames = 300;

std::unique_ptr<PenumbraUiBackend::NyxApplicationBridge> Bridge;

} // namespace

Penumbra::Application* CreateApplication() {
    Chaos::ChaosConfig Config{Chaos::ChaosBuildTarget::UmbraEngine, "1", {"."}};
    Bridge = std::make_unique<PenumbraUiBackend::NyxApplicationBridge>(Config, ".");
    nyx::host::NyxRuntime& Runtime = Bridge->Runtime();
    auto WidgetDescriptor = std::make_shared<const nyx::runtime::TypeDescriptor*>(nullptr);

    Runtime.RegisterFunction("log", [](const std::string& Message) { std::printf("[DemoApp.nyx] %s\n", Message.c_str()); });

    Runtime.RegisterFunction(
        "tick", [](float DeltaSeconds) -> bool {
            static int FrameCount = 0;
            ++FrameCount;
            if (FrameCount % 120 == 0) {
                std::printf("[DemoApp.nyx] OnUpdate has fired %d times so far (dt=%.4f)\n", FrameCount,
                            DeltaSeconds);
            }
            if (FrameCount == kAutoQuitAfterFrames) {
                std::printf("[demo_nyx] %d Nyx-driven frames complete; auto-quitting\n", FrameCount);
            }
            return FrameCount >= kAutoQuitAfterFrames;
        });

    Runtime.RegisterFunction("createDemoRoot", "() -> PenumbraWidget", [WidgetDescriptor](std::vector<nyx::runtime::Value>) {
        auto Root = std::make_unique<Penumbra::Widgets::Box>();
        Root->Style.ColorBackground = Penumbra::Render::Color{40, 120, 200, 255};
        Root->OnPressed = []() { std::printf("[demo_nyx] script-mounted root received a click\n"); };
        return nyx::host::ToValue(Root.release(), *WidgetDescriptor);
    });

    Runtime.RegisterFunction("inspectApplicationHandles", [](float Width, float Height, float DpiScale,
                                                             Penumbra::Render::IFontBackend*, Penumbra::LifecycleRegistry*,
                                                             Penumbra::Widgets::WidgetBase*) {
        std::printf("[demo_nyx] Application methods exposed to Nyx: window %.0fx%.0f, DPI %.2f, "
                    "font/lifecycle/root handles valid\n",
                    Width, Height, DpiScale);
    });

    const std::string ScriptPath = std::string(DEMO_NYX_ASSET_DIR) + "/DemoApp.nyx";
    Penumbra::Application* Application = Bridge->LoadApplicationFromFile(ScriptPath, "DemoApplication");
    if (Application) {
        *WidgetDescriptor =
            std::get<std::shared_ptr<nyx::runtime::HostObject>>(Runtime.Globals().at("PenumbraWidget").data)
                ->descriptor;
    }
    return Application;
}
