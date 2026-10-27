#pragma once

#include "PenumbraIcons/StandardIconBackend.h"
#include "PenumbraUiBackend/ChaosApplication.h"
#include "PenumbraUiBackend/NyxApplicationBridge.h"

#include "Penumbra/Render/Color.h"

#include <host/json-binding.hpp>
#include <host/nyx-runtime.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace PenumbraUiBackend {

struct ChaosApplicationHostOptions {
    std::filesystem::path              UiDirectory;
    std::string                        ApplicationFile;
    std::string                        ApplicationClass;
    std::vector<std::filesystem::path> NyxSourceDirectories;
    std::filesystem::path              FontPath;
    float                              FontSizeLogical{12.0f};
    float                              IconStrokeThicknessLogical{1.4f};
    Penumbra::Render::Color            IconColor{238, 238, 243, 255};
};

// Everything an app built as one Nyx Application subclass needs from C++: the Chaos bridge
// over UiDirectory, nyx-proto's JSON, file and directory bindings, `appInstance()`, the icon
// backend and the font. An app registers its own host functions on Runtime() before Load().
class ChaosApplicationHost {
public:
    explicit ChaosApplicationHost(ChaosApplicationHostOptions Options);
    ~ChaosApplicationHost();

    ChaosApplicationHost(const ChaosApplicationHost&)            = delete;
    ChaosApplicationHost& operator=(const ChaosApplicationHost&) = delete;

    [[nodiscard]] nyx::host::NyxRuntime&  Runtime();
    [[nodiscard]] ChaosApplicationBridge& Bridge() { return Bridge_; }
    [[nodiscard]] ChaosApplication*       App() const { return App_; }

    // Loads the application once. A host that registers viewport renderers on App() calls this
    // first, then Run(), which reuses the loaded application.
    ChaosApplication*         Load();
    [[nodiscard]] std::string LastDiagnostics();

    // Runs the loaded application, loading it first if Load() hasn't been called, or prints the
    // load diagnostics to stderr and returns 1.
    int Run();

private:
    ChaosApplicationHostOptions        Options_;
    ChaosApplicationBridge             Bridge_;
    nyx::host::JsonBinding             Json_;
    PenumbraIcons::StandardIconBackend Icons_;
    ChaosApplication*                  App_ = nullptr;
};

} // namespace PenumbraUiBackend
