#include "PenumbraUiBackend/IrisApplication.h"

#include "PenumbraUiBackend/Lustre/StylesheetLoader.h"
#include "PenumbraUiBackend/Portal.h"

#include "Iris/SlotResolution.h"
#include "Iris/SlotRuntime.h"

#include "Penumbra/Widgets/Box.h"

#include <host/inheritable-type-builder.hpp>

#include <cstdio>
#include <utility>

namespace PenumbraUiBackend {

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

bool IrisApplication::LoadStylesheet(const std::string& Name) {
    Sheets_[Name] = Lustre::LoadStylesheetFromFile((UiDir_ + "/" + Name + ".lustre").c_str(), Name.c_str());
    StyleSets_[Name] = ::Lustre::StylesheetSet{&Sheets_[Name], nullptr};
    return true;
}

const ::Lustre::StylesheetSet* IrisApplication::GetStylesheet(const std::string& Name) const {
    auto It = StyleSets_.find(Name);
    return It == StyleSets_.end() ? nullptr : &It->second;
}

const Lustre::LustreStyleApplier& IrisApplication::StyleApplier() {
    if (!Applier_) Applier_.emplace(&GetFontBackend());
    return *Applier_;
}

IrisApplication::MountResult IrisApplication::MountComponent(
    const std::string& File, const std::string& FunctionName, std::vector<nyx::runtime::Value> Args,
    const std::string& StylesheetName, std::vector<std::shared_ptr<Iris::Component>>& KeepAlive) {
    auto Root =
        std::make_shared<Iris::Component>(Driver_->MountRoot(UiDir_ + "/" + File, FunctionName, std::move(Args)));
    KeepAlive.push_back(Root);

    BuildContext Context;
    Context.FontBackend  = &GetFontBackend();
    Context.Font         = Font_;
    Context.Style        = GetStylesheet(StylesheetName);
    Context.StyleApplier = &StyleApplier();
    Context.OverlayHost  = OverlayHostPtr_;

    MountResult Result;
    Result.Widget = BuildWidgetTree(*Root, Context, nullptr, &Result.Refs);
    return Result;
}

bool IrisApplication::MountAppRoot(const std::string& File, const std::string& FunctionName,
                                    const std::string& StylesheetName) {
    AppRoot_ = Driver_->MountRoot(UiDir_ + "/" + File, FunctionName);
    if (!Driver_->Errors().empty()) {
        std::fprintf(stderr, "[IrisApplication] %s mount failed: %s\n", File.c_str(),
                     Driver_->Errors().back().Message.c_str());
        return false;
    }

    BuildContext Context;
    Context.FontBackend   = &GetFontBackend();
    Context.Font          = Font_;
    Context.Style         = GetStylesheet(StylesheetName);
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
                                                 const std::string& StylesheetName,
                                                 const std::string& SlotStylesheetName,
                                                 const std::string& TargetRefName) {
    auto* Target = dynamic_cast<Penumbra::Widgets::Box*>(GetRef(TargetRefName));
    if (!Target) return false;

    ReconciledMount& Mount = ReconciledMounts_[TargetRefName];
    ClearReconciledMount(Mount, Target);

    const std::size_t ErrorsBefore = Driver_->Errors().size();
    MountResult Result = MountComponent(File, FunctionName, std::move(Args), StylesheetName, Mount.Roots);
    if (Driver_->Errors().size() > ErrorsBefore) {
        std::fprintf(stderr, "[IrisApplication] %s mount failed: %s\n", File.c_str(),
                     Driver_->Errors().back().Message.c_str());
        Mount.Roots.clear();
        return false;
    }

    Mount.Wrapper =
        WrapExistingTree(std::move(Result.Widget), nullptr, nullptr, GetStylesheet(StylesheetName), &StyleApplier());

    BuildContext SlotContext;
    SlotContext.FontBackend  = &GetFontBackend();
    SlotContext.Font         = Font_;
    SlotContext.Style        = GetStylesheet(SlotStylesheetName);
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
    Builder.Method("LoadStylesheet", &IrisApplication::LoadStylesheet)
        .Method("ReloadFont", &IrisApplication::ReloadFont)
        .Method("MountAppRoot", &IrisApplication::MountAppRoot)
        .Method("MountReconciledComponent", &IrisApplication::MountReconciledComponent)
        .Method("TeardownReconciledComponent", &IrisApplication::TeardownReconciledComponent)
        .Method("TeardownRootWidget", &IrisApplication::TeardownRootWidget)
        .Method("TickIris", &IrisApplication::TickIris)
        .PointerMethod("GetRef", &GetRefForNyx, WidgetDescriptor);
}

} // namespace PenumbraUiBackend
