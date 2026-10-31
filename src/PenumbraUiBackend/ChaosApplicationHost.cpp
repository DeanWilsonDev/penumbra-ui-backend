#include "PenumbraUiBackend/ChaosApplicationHost.h"

#include "chaos/ChaosConfig.h"

#include <diagnostics/render-text.hpp>
#include <host/directory-binding.hpp>
#include <host/file-binding.hpp>
#include <runtime/value.hpp>

#include <cstdio>
#include <utility>

namespace PenumbraUiBackend {

namespace {

Chaos::ChaosConfig HostChaosConfig() {
    Chaos::ChaosConfig Config;
    Config.Target      = Chaos::ChaosBuildTarget::UmbraEngine;
    Config.Version     = "1";
    Config.SearchPaths = {"."};
    return Config;
}

} // namespace

ChaosApplicationHost::ChaosApplicationHost(ChaosApplicationHostOptions Options)
    : Options_(std::move(Options)),
      Bridge_(HostChaosConfig(), Options_.UiDirectory.string()),
      Json_(Bridge_.Runtime()),
      Icons_(Options_.IconStrokeThicknessLogical, Options_.IconColor) {
    nyx::host::NyxRuntime& HostRuntime = Bridge_.Runtime();
    for (const std::filesystem::path& Directory : Options_.NyxSourceDirectories) {
        HostRuntime.AddImportSearchPath(Directory);
    }
    nyx::host::RegisterFileBinding(HostRuntime);
    nyx::host::RegisterDirectoryBinding(HostRuntime);
    HostRuntime.RegisterFunction("appInstance", "() -> Application", [this](std::vector<nyx::runtime::Value>) {
        return Bridge_.GetApplicationInstanceValue(*App_);
    });
}

ChaosApplicationHost::~ChaosApplicationHost() { delete App_; }

nyx::host::NyxRuntime& ChaosApplicationHost::Runtime() { return Bridge_.Runtime(); }

ChaosApplication* ChaosApplicationHost::Load() {
    App_ = Bridge_.LoadApplicationFromFile(Options_.UiDirectory / Options_.ApplicationFile, Options_.ApplicationClass);
    if (App_) {
        App_->SetFontConfig(Options_.FontPath.string(), Options_.FontSizeLogical);
        App_->SetIconBackend(&Icons_);
    }
    return App_;
}

std::string ChaosApplicationHost::LastDiagnostics() {
    nyx::host::NyxRuntime& HostRuntime = Bridge_.Runtime();
    std::string            Text;
    for (const nyx::Diagnostic& Diagnostic : HostRuntime.LastDiagnostics()) {
        Text += nyx::RenderText(Diagnostic, *HostRuntime.SharedSources()) + "\n";
    }
    return Text;
}

int ChaosApplicationHost::Run() {
    ChaosApplication* Loaded = App_ ? App_ : Load();
    if (!Loaded) {
        std::fprintf(stderr, "Failed to load %s from %s\n%s", Options_.ApplicationClass.c_str(),
                     Options_.ApplicationFile.c_str(), LastDiagnostics().c_str());
        return 1;
    }
    return Loaded->Run();
}

} // namespace PenumbraUiBackend
