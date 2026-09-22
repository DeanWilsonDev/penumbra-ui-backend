#include "PenumbraUiBackend/IrisApplication.h"

#include "PenumbraUiBackend/Lustre/StylesheetLoader.h"
#include "PenumbraUiBackend/Portal.h"

#include "Iris/ImportResolver.h"
#include "Iris/SlotResolution.h"
#include "Iris/SlotRuntime.h"

#include "Penumbra/Widgets/Box.h"

#include <host/inheritable-type-builder.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace PenumbraUiBackend {

namespace {

std::optional<std::string> ReadFileToString(const std::string& Path) {
    std::ifstream File(Path);
    if (!File) return std::nullopt;
    std::ostringstream Buffer;
    Buffer << File.rdbuf();
    return Buffer.str();
}

} // namespace

void IrisApplication::Attach(Iris::IrisNyxDriver& Driver, std::string UiDir) {
    Driver_ = &Driver;
    UiDir_  = std::move(UiDir);
}

void IrisApplication::SetFontConfig(std::string FontPath, float FontSizeLogical) {
    FontPath_        = std::move(FontPath);
    FontSizeLogical_ = FontSizeLogical;
}

bool IrisApplication::ReloadFont(float DpiScaleFactor) {
    if (FontPath_.empty()) return false;
    Font_ = GetFontBackend().LoadFont(FontPath_.c_str(), FontSizeLogical_, DpiScaleFactor);
    return true;
}

void IrisApplication::EnsureStylesheetsFor(const std::string& EntryResolvedPath) {
    std::vector<std::string> Worklist{EntryResolvedPath};
    while (!Worklist.empty()) {
        std::string Path = std::move(Worklist.back());
        Worklist.pop_back();
        if (!DiscoveredStylesheetFiles_.insert(Path).second) continue;

        std::filesystem::path LustrePath = std::filesystem::path(Path).replace_extension(".lustre");
        std::error_code       Ignored;
        if (std::filesystem::is_regular_file(LustrePath, Ignored)) {
            ::Lustre::Stylesheet Sheet = Lustre::LoadStylesheetFromFile(LustrePath.string().c_str(), Path.c_str());
            for (::Lustre::RulePtr& R : Sheet.Rules) ComposedSheet_.Rules.push_back(std::move(R));
        }

        std::optional<std::string> Source = ReadFileToString(Path);
        if (!Source) continue;
        const std::vector<Iris::ImportStatement>   Imports = Iris::ScanImports(*Source, Path);
        const Iris::ImportResolutionResult Resolved = Iris::ResolveImports(Imports, Driver_->Config(), Driver_->ProjectRoot());
        for (const Iris::ResolvedImport& R : Resolved.Resolved) Worklist.push_back(R.ResolvedPath);
    }
}

const Lustre::LustreStyleApplier& IrisApplication::StyleApplier() {
    if (!Applier_) Applier_.emplace(&GetFontBackend());
    return *Applier_;
}

IrisApplication::MountResult IrisApplication::MountComponent(
    const std::string& File, const std::string& FunctionName, std::vector<nyx::runtime::Value> Args,
    std::vector<std::shared_ptr<Iris::Component>>& KeepAlive) {
    EnsureStylesheetsFor(UiDir_ + "/" + File);

    auto Root =
        std::make_shared<Iris::Component>(Driver_->MountRoot(UiDir_ + "/" + File, FunctionName, std::move(Args)));
    KeepAlive.push_back(Root);

    BuildContext Context;
    Context.FontBackend  = &GetFontBackend();
    Context.Font         = Font_;
    Context.Style        = &ComposedStyleSet_;
    Context.StyleApplier = &StyleApplier();
    Context.OverlayHost  = OverlayHostPtr_;

    MountResult Result;
    Result.Widget = BuildWidgetTree(*Root, Context, nullptr, &Result.Refs);
    return Result;
}

bool IrisApplication::MountAppRoot(const std::string& File, const std::string& FunctionName) {
    EnsureStylesheetsFor(UiDir_ + "/" + File);

    AppRoot_ = Driver_->MountRoot(UiDir_ + "/" + File, FunctionName);
    if (!Driver_->Errors().empty()) {
        std::fprintf(stderr, "[IrisApplication] %s mount failed: %s\n", File.c_str(),
                     Driver_->Errors().back().Message.c_str());
        return false;
    }

    BuildContext Context;
    Context.FontBackend   = &GetFontBackend();
    Context.Font          = Font_;
    Context.Style         = &ComposedStyleSet_;
    Context.StyleApplier  = &StyleApplier();
    Context.LifecycleHost = &GetLifecycleRegistry();
    Context.NyxHost       = &Driver_->Runtime();

    AppRootRefs_.clear();
    std::unique_ptr<Penumbra::Widgets::WidgetBase> Built = BuildWidgetTree(AppRoot_, Context, nullptr, &AppRootRefs_);

    auto RootOverlayHost = std::make_unique<Penumbra::Widgets::OverlayHost>();
    OverlayHostPtr_       = RootOverlayHost.get();
    RootOverlayHost->SetRoot(std::move(Built));
    SetRootWidget(std::move(RootOverlayHost));
    return true;
}

Penumbra::Widgets::WidgetBase* IrisApplication::GetRef(const std::string& Name) const {
    auto It = AppRootRefs_.find(Name);
    return It == AppRootRefs_.end() ? nullptr : It->second;
}

void IrisApplication::TeardownRootWidget() {
    for (auto& [Name, Mount] : ReconciledMounts_) {
        Mount.Slots.clear();
        Mount.Wrapper.reset();
    }
    ReconciledMounts_.clear();
    SetRootWidget(nullptr);
    OverlayHostPtr_ = nullptr;
    AppRootRefs_.clear();
}

void IrisApplication::ClearReconciledMount(ReconciledMount& Mount, Penumbra::Widgets::Box* Target) {
    if (Target && Target->GetChildCount() > 0) {
        if (auto* Anchor = dynamic_cast<PortalAnchorWidget*>(Target->GetChildAt(0))) {
            Anchor->PreparePortalUnmount();
        }
    }
    Mount.Slots.clear();
    Mount.Wrapper.reset();
    if (Target) Target->ClearChildren();
    Mount.Roots.clear();
}

bool IrisApplication::MountReconciledComponent(const std::string& File, const std::string& FunctionName,
                                                 std::vector<nyx::runtime::Value> Args,
                                                 const std::string& TargetRefName) {
    auto* Target = dynamic_cast<Penumbra::Widgets::Box*>(GetRef(TargetRefName));
    if (!Target) return false;

    ReconciledMount& Mount = ReconciledMounts_[TargetRefName];
    ClearReconciledMount(Mount, Target);

    const std::size_t ErrorsBefore = Driver_->Errors().size();
    MountResult Result = MountComponent(File, FunctionName, std::move(Args), Mount.Roots);
    if (Driver_->Errors().size() > ErrorsBefore) {
        std::fprintf(stderr, "[IrisApplication] %s mount failed: %s\n", File.c_str(),
                     Driver_->Errors().back().Message.c_str());
        Mount.Roots.clear();
        return false;
    }

    Mount.Wrapper =
        WrapExistingTree(std::move(Result.Widget), nullptr, nullptr, &ComposedStyleSet_, &StyleApplier());

    BuildContext SlotContext;
    SlotContext.FontBackend  = &GetFontBackend();
    SlotContext.Font         = Font_;
    SlotContext.Style        = &ComposedStyleSet_;
    SlotContext.StyleApplier = &StyleApplier();

    Mount.Slots = iris::ResolveSlots(*Mount.Wrapper, *Mount.Roots.back(), MakeMountFn(SlotContext));
    for (std::unique_ptr<iris::SlotState>& Slot : Mount.Slots) Slot->Reconcile();

    Target->AddChild(Mount.Wrapper->DetachOwnership());
    return true;
}

void IrisApplication::TeardownReconciledComponent(const std::string& TargetRefName) {
    auto It = ReconciledMounts_.find(TargetRefName);
    if (It == ReconciledMounts_.end()) return;
    ClearReconciledMount(It->second, dynamic_cast<Penumbra::Widgets::Box*>(GetRef(TargetRefName)));
    ReconciledMounts_.erase(It);
}

void IrisApplication::TickIris() { iris::Tick(); }

namespace {

Penumbra::Widgets::WidgetBase* GetRefForNyx(IrisApplication& Self, const std::string& Name) {
    return Self.GetRef(Name);
}

} // namespace

void RegisterIrisApplicationMethods(nyx::host::InheritableTypeBuilder<IrisApplication>& Builder,
                                     const nyx::runtime::TypeDescriptor* WidgetDescriptor) {
    Builder.Method("ReloadFont", &IrisApplication::ReloadFont)
        .Method("MountAppRoot", &IrisApplication::MountAppRoot)
        .Method("MountReconciledComponent", &IrisApplication::MountReconciledComponent)
        .Method("TeardownReconciledComponent", &IrisApplication::TeardownReconciledComponent)
        .Method("TeardownRootWidget", &IrisApplication::TeardownRootWidget)
        .Method("TickIris", &IrisApplication::TickIris)
        .PointerMethod("GetRef", &GetRefForNyx, WidgetDescriptor);
}

} // namespace PenumbraUiBackend
