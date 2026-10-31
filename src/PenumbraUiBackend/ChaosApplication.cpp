#include "PenumbraUiBackend/ChaosApplication.h"

#include "PenumbraUiBackend/Cosmos/StylesheetLoader.h"
#include "PenumbraUiBackend/Portal.h"

#include "chaos/Diagnostics/DiagnosticSink.h"
#include "chaos/Diagnostics/RenderText.h"
#include "chaos/ImportResolver.h"
#include "chaos/SlotResolution.h"
#include "chaos/SlotRuntime.h"

#include "Penumbra/Widgets/Box.h"
#include <SDL3/SDL.h>

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

void ChaosApplication::Attach(Chaos::ChaosNyxDriver& Driver, std::string UiDir) {
    Driver.RegisterElementType("Penumbra::Component");
    RegisterComponentGetRef(Driver.Runtime());
    Driver_                  = &Driver;
    ReportedDiagnosticCount_ = 0;
    UiDir_                   = std::move(UiDir);
}

void ChaosApplication::RegisterViewportRenderer(const std::string& Name, Umbra::IViewportRenderer* Renderer) {
    if (Renderer == nullptr) {
        ViewportRenderers_.erase(Name);
    } else {
        ViewportRenderers_[Name] = Renderer;
    }
}

namespace {

struct FileDialogFilters {
    std::string                            Name;
    std::string                            Pattern;
    std::vector<SDL_DialogFileFilter>      Filters;
};

void FileDialogCallback(void* UserData, const char* const* FileList, int) {
    auto* Shared = static_cast<std::shared_ptr<ChaosApplication::FileDialogState>*>(UserData);
    {
        std::lock_guard<std::mutex> Lock((*Shared)->Guard);
        (*Shared)->Result   = FileList != nullptr && FileList[0] != nullptr ? FileList[0] : "";
        (*Shared)->Pending  = false;
        (*Shared)->Finished = true;
    }
    delete Shared;
}

} // namespace

void ChaosApplication::ShowFileDialog(bool Save, const std::string& FilterName, const std::string& FilterPattern,
                                      const std::string& DefaultPath) {
    {
        std::lock_guard<std::mutex> Lock(FileDialog_->Guard);
        if (FileDialog_->Pending) {
            return;
        }
        FileDialog_->Pending  = true;
        FileDialog_->Finished = false;
        FileDialog_->Result.clear();
    }
    static FileDialogFilters Filters;
    Filters.Name    = FilterName;
    Filters.Pattern = FilterPattern.empty() ? "*" : FilterPattern;
    Filters.Filters.assign(1, SDL_DialogFileFilter{Filters.Name.c_str(), Filters.Pattern.c_str()});
    auto* Shared = new std::shared_ptr<FileDialogState>(FileDialog_);
    const char* Location = DefaultPath.empty() ? nullptr : DefaultPath.c_str();
    if (Save) {
        SDL_ShowSaveFileDialog(FileDialogCallback, Shared, nullptr, Filters.Filters.data(), 1, Location);
    } else {
        SDL_ShowOpenFileDialog(FileDialogCallback, Shared, nullptr, Filters.Filters.data(), 1, Location, false);
    }
}

void ChaosApplication::ShowOpenFileDialog(const std::string& FilterName, const std::string& FilterPattern,
                                          const std::string& DefaultPath) {
    ShowFileDialog(false, FilterName, FilterPattern, DefaultPath);
}

void ChaosApplication::ShowSaveFileDialog(const std::string& FilterName, const std::string& FilterPattern,
                                          const std::string& DefaultPath) {
    ShowFileDialog(true, FilterName, FilterPattern, DefaultPath);
}

bool ChaosApplication::FileDialogPending() const {
    std::lock_guard<std::mutex> Lock(FileDialog_->Guard);
    return FileDialog_->Pending;
}

bool ChaosApplication::FileDialogFinished() const {
    std::lock_guard<std::mutex> Lock(FileDialog_->Guard);
    return FileDialog_->Finished;
}

std::string ChaosApplication::TakeFileDialogResult() {
    std::lock_guard<std::mutex> Lock(FileDialog_->Guard);
    FileDialog_->Finished = false;
    return std::exchange(FileDialog_->Result, std::string());
}

void ChaosApplication::SetFontConfig(std::string FontPath, float FontSizeLogical) {
    FontPath_        = std::move(FontPath);
    FontSizeLogical_ = FontSizeLogical;
    if (Applier_) Applier_->SetDefaultFont({FontPath_, FontSizeLogical_});
}

bool ChaosApplication::ReloadFont(float DpiScaleFactor) {
    DpiScaleFactor_ = DpiScaleFactor;
    if (Applier_) Applier_->SetDpiScaleFactor(DpiScaleFactor);
    if (FontPath_.empty()) return false;
    Font_ = GetFontBackend().LoadFont(FontPath_.c_str(), FontSizeLogical_, DpiScaleFactor);
    return true;
}

void ChaosApplication::EnsureStylesheetsFor(const std::string& EntryResolvedPath) {
    const nyx::host::ImportIndex& Index = Driver_->Runtime().RefreshImports();
    std::vector<std::string>      Worklist{EntryResolvedPath};
    while (!Worklist.empty()) {
        std::string Path = std::move(Worklist.back());
        Worklist.pop_back();
        if (!DiscoveredStylesheetFiles_.insert(Path).second) continue;

        const std::string Dir = std::filesystem::path(Path).parent_path().string();
        if (DiscoveredStylesheetDirs_.insert(Dir).second) {
            std::error_code Ignored;
            for (const std::filesystem::directory_entry& Entry :
                 std::filesystem::directory_iterator(Dir, Ignored)) {
                if (Entry.path().extension() != ".cosmos") continue;
                ::Cosmos::Stylesheet Sheet =
                    Cosmos::LoadStylesheetFromFile(Entry.path().string().c_str(), Entry.path().stem().string().c_str());
                ::Cosmos::Stylesheet& Target = Entry.path().filename() == "global.cosmos" ? GlobalSheet_ : ComposedSheet_;
                if (Sheet.Root) {
                    if (!Target.Root) Target.Root.emplace();
                    for (::Cosmos::VariableDeclaration& V : Sheet.Root->Variables) Target.Root->Variables.push_back(std::move(V));
                }
                for (::Cosmos::RulePtr& R : Sheet.Rules) Target.Rules.push_back(std::move(R));
            }
        }

        std::optional<std::string> Source = ReadFileToString(Path);
        if (!Source) continue;
        const std::vector<Chaos::ImportStatement>   Imports = Chaos::ScanImports(*Source, Path);
        // Only walking imports to find their stylesheets: the driver reports the same import
        // diagnostics when it compiles the file, so these aren't reported again.
        Chaos::DiagnosticSink               Ignored;
        const Chaos::ImportResolutionResult Resolved =
            Chaos::ResolveImports(Imports, Path, Driver_->Config(), Driver_->ProjectRoot(), Index, Ignored);
        for (const Chaos::ResolvedImport& R : Resolved.Resolved) Worklist.push_back(R.ResolvedPath);
    }
}

const Cosmos::CosmosStyleApplier& ChaosApplication::StyleApplier() {
    if (!Applier_) {
        Applier_.emplace(&GetFontBackend(), DpiScaleFactor_);
        if (!FontPath_.empty()) Applier_->SetDefaultFont({FontPath_, FontSizeLogical_});
    }
    return *Applier_;
}

BuildContext ChaosApplication::MakeBuildContext() {
    BuildContext Context;
    Context.FontBackend  = &GetFontBackend();
    Context.Font         = Font_;
    Context.IconBackend  = IconBackend_;
    Context.Style        = &ComposedStyleSet_;
    Context.StyleApplier = &StyleApplier();
    Context.OverlayHost  = OverlayHostPtr_;
    Context.Focus        = &Focus_;
    Context.Clipboard    = &GetWindow();
    Context.ViewportRenderers = &ViewportRenderers_;
    return Context;
}

ChaosApplication::MountResult ChaosApplication::MountComponent(
    const std::string& File, const std::string& FunctionName, std::vector<nyx::runtime::Value> Args,
    std::vector<std::shared_ptr<Chaos::Component>>& KeepAlive) {
    EnsureStylesheetsFor(UiDir_ + "/" + File);

    auto Root =
        std::make_shared<Chaos::Component>(Driver_->MountRoot(UiDir_ + "/" + File, FunctionName, std::move(Args)));
    KeepAlive.push_back(Root);

    BuildContext Context = MakeBuildContext();

    MountResult Result;
    Result.Widget = BuildWidgetTree(*Root, Context, nullptr, &Result.Refs);
    return Result;
}

bool ChaosApplication::MountAppRoot(const std::string& File, const std::string& FunctionName) {
    EnsureStylesheetsFor(UiDir_ + "/" + File);

    AppRootSlots_.clear();
    AppRootWrapper_.reset();

    const std::size_t DiagnosticsBefore = Driver_->Diagnostics().size();
    AppRoot_ = Driver_->MountRoot(UiDir_ + "/" + File, FunctionName);
    if (Driver_->Diagnostics().size() > DiagnosticsBefore) {
        std::fprintf(stderr, "[ChaosApplication] %s mount failed\n", File.c_str());
        ReportNewErrors();
        return false;
    }

    auto RootOverlayHost = std::make_unique<Penumbra::Widgets::OverlayHost>();
    OverlayHostPtr_       = RootOverlayHost.get();

    BuildContext Context  = MakeBuildContext();
    Context.LifecycleHost = &GetLifecycleRegistry();

    AppRootRefs_.clear();
    std::unique_ptr<Penumbra::Widgets::WidgetBase> Built = BuildWidgetTree(AppRoot_, Context, nullptr, &AppRootRefs_);

    AppRootWrapper_ = WrapExistingTree(std::move(Built), nullptr, nullptr, &ComposedStyleSet_, &StyleApplier());
    AppRootSlots_    = chaos::ResolveSlots(*AppRootWrapper_, AppRoot_, MakeMountFn(Context));
    for (std::unique_ptr<chaos::SlotState>& Slot : AppRootSlots_) Slot->Reconcile();

    RootOverlayHost->SetRoot(AppRootWrapper_->DetachOwnership());
    SetRootWidget(std::move(RootOverlayHost));
    ReportNewErrors();
    return true;
}

Penumbra::Widgets::WidgetBase* ChaosApplication::GetRef(const std::string& Name) const {
    if (auto It = AppRootRefs_.find(Name); It != AppRootRefs_.end()) {
        return It->second;
    }
    // AppRootRefs_ only holds the static build's refs. Content a <Slot> mounted (through
    // MakeMountFn, later) or MountReconciledComponent built has its own refs on its own
    // wrappers, so search the live wrapper trees for those.
    if (AppRootWrapper_) {
        if (PenumbraWidget* Found = AppRootWrapper_->FindDescendantByRef(Name)) {
            return Found->RawWidget();
        }
    }
    for (const auto& [TargetName, Mount] : ReconciledMounts_) {
        if (Mount.Wrapper) {
            if (PenumbraWidget* Found = Mount.Wrapper->FindDescendantByRef(Name)) {
                return Found->RawWidget();
            }
        }
    }
    return nullptr;
}

ChaosApplication::~ChaosApplication() { TeardownRootWidget(); }

void ChaosApplication::TeardownRootWidget() {
    for (auto& [Name, Mount] : ReconciledMounts_) {
        Mount.Slots.clear();
        Mount.Wrapper.reset();
    }
    ReconciledMounts_.clear();
    AppRootSlots_.clear();
    AppRootWrapper_.reset();
    SetRootWidget(nullptr);
    OverlayHostPtr_ = nullptr;
    AppRootRefs_.clear();
}

void ChaosApplication::ClearReconciledMount(ReconciledMount& Mount, Penumbra::Widgets::Box* Target) {
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

bool ChaosApplication::MountReconciledComponent(const std::string& File, const std::string& FunctionName,
                                                 std::vector<nyx::runtime::Value> Args,
                                                 const std::string& TargetRefName) {
    auto* Target = dynamic_cast<Penumbra::Widgets::Box*>(GetRef(TargetRefName));
    if (!Target) return false;

    ReconciledMount& Mount = ReconciledMounts_[TargetRefName];
    ClearReconciledMount(Mount, Target);

    const std::size_t DiagnosticsBefore = Driver_->Diagnostics().size();
    MountResult Result = MountComponent(File, FunctionName, std::move(Args), Mount.Roots);
    if (Driver_->Diagnostics().size() > DiagnosticsBefore) {
        std::fprintf(stderr, "[ChaosApplication] %s mount failed\n", File.c_str());
        ReportNewErrors();
        Mount.Roots.clear();
        return false;
    }

    Mount.Wrapper = WrapExistingTree(std::move(Result.Widget), nullptr, nullptr, &ComposedStyleSet_, &StyleApplier(),
                                     nullptr, &Result.Refs);

    BuildContext SlotContext = MakeBuildContext();
    SlotContext.OverlayHost  = nullptr;

    Mount.Slots = chaos::ResolveSlots(*Mount.Wrapper, *Mount.Roots.back(), MakeMountFn(SlotContext));
    for (std::unique_ptr<chaos::SlotState>& Slot : Mount.Slots) Slot->Reconcile();

    Target->AddChild(Mount.Wrapper->DetachOwnership());
    ReportNewErrors();
    return true;
}

void ChaosApplication::TeardownReconciledComponent(const std::string& TargetRefName) {
    auto It = ReconciledMounts_.find(TargetRefName);
    if (It == ReconciledMounts_.end()) return;
    ClearReconciledMount(It->second, dynamic_cast<Penumbra::Widgets::Box*>(GetRef(TargetRefName)));
    ReconciledMounts_.erase(It);
}

void ChaosApplication::TickChaos() {
    chaos::Tick();
    ReportNewErrors();
}

void ChaosApplication::ReportNewErrors() {
    if (!Driver_) return;
    const std::vector<Chaos::Diagnostic>& Diagnostics = Driver_->Diagnostics();
    for (; ReportedDiagnosticCount_ < Diagnostics.size(); ++ReportedDiagnosticCount_) {
        const std::string Rendered = Chaos::RenderText(Diagnostics[ReportedDiagnosticCount_], Driver_->Sources());
        std::fprintf(stderr, "%s\n", Rendered.c_str());
    }
}

namespace {

Penumbra::Widgets::WidgetBase* GetRefForNyx(ChaosApplication& Self, const std::string& Name) {
    return Self.GetRef(Name);
}

bool MountReconciledComponentForNyx(ChaosApplication& Self, const std::string& File, const std::string& FunctionName,
                                    const std::string& TargetRefName) {
    return Self.MountReconciledComponent(File, FunctionName, {}, TargetRefName);
}

} // namespace

void RegisterChaosApplicationMethods(nyx::host::InheritableTypeBuilder<ChaosApplication>& Builder,
                                     const nyx::runtime::TypeDescriptor* WidgetDescriptor) {
    Builder.Method("ReloadFont", &ChaosApplication::ReloadFont)
        .Method("MountAppRoot", &ChaosApplication::MountAppRoot)
        .Method("MountReconciledComponent", &MountReconciledComponentForNyx)
        .Method("TeardownReconciledComponent", &ChaosApplication::TeardownReconciledComponent)
        .Method("TeardownRootWidget", &ChaosApplication::TeardownRootWidget)
        .Method("TickChaos", &ChaosApplication::TickChaos)
        .PointerMethod("GetRef", &GetRefForNyx, WidgetDescriptor)
        .Method("ShowOpenFileDialog", &ChaosApplication::ShowOpenFileDialog)
        .Method("ShowSaveFileDialog", &ChaosApplication::ShowSaveFileDialog)
        .Method("FileDialogPending", &ChaosApplication::FileDialogPending)
        .Method("FileDialogFinished", &ChaosApplication::FileDialogFinished)
        .Method("TakeFileDialogResult", &ChaosApplication::TakeFileDialogResult);
}

} // namespace PenumbraUiBackend
