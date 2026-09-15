#pragma once

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"

#include "Penumbra/Application.h"

#include "host/nyx-runtime.hpp"
#include "interpreter/interpreter.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PenumbraUiBackend {

// Owns the single Nyx runtime shared by one Nyx-authored Penumbra application
// and its Iris UI driver. Mounted application interpreters are retained because
// NyxBridge stores a non-owning pointer to its interpreter. The member order is
// intentional: Runtime_ outlives both IrisDriver_ and Interpreters_.
class NyxApplicationBridge {
public:
    NyxApplicationBridge(Iris::IrisConfig Config, std::string ProjectRoot);

    NyxApplicationBridge(const NyxApplicationBridge&) = delete;
    NyxApplicationBridge& operator=(const NyxApplicationBridge&) = delete;
    NyxApplicationBridge(NyxApplicationBridge&&) = delete;
    NyxApplicationBridge& operator=(NyxApplicationBridge&&) = delete;

    [[nodiscard]] nyx::host::NyxRuntime& Runtime();
    [[nodiscard]] Iris::IrisNyxDriver& IrisDriver();

    // Instantiates ApplicationClassName from Source as a real
    // Penumbra::Application. Returns nullptr on parse, interpretation, or type
    // errors. The caller owns the returned application; this bridge must outlive
    // it so the retained interpreter and runtime remain valid.
    Penumbra::Application* LoadApplication(const std::string& Source, const std::string& Filename,
                                           const std::string& ApplicationClassName);

    // LoadApplication, reading Source from Path first. Returns nullptr when the
    // file cannot be opened.
    Penumbra::Application* LoadApplicationFromFile(const std::filesystem::path& Path,
                                                   const std::string& ApplicationClassName);

private:
    void RegisterApplicationType();

    nyx::host::NyxRuntime                                      Runtime_;
    Iris::IrisNyxDriver                                        IrisDriver_;
    std::vector<std::shared_ptr<nyx::interpreter::Interpreter>> Interpreters_;
    bool                                                        ApplicationTypeRegistered_{false};
};

} // namespace PenumbraUiBackend
