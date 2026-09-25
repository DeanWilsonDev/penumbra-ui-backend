#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/Signal.h"
#include "Iris/Reconciler.h"
#include "Iris/SlotResolution.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/OverlayHost.h"

#include <cstdio>
#include <string>

extern int Failures; // defined in WalkerTests.cpp

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::MakeMountFn;
using PenumbraUiBackend::PenumbraWidget;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::OverlayHost;

Component MakeFrame(std::vector<Component> Children = {}) {
    return Component(IrisElementTag::Frame, {}, std::move(Children), nullptr);
}

Component MakeText(const std::string& Content) {
    IrisProps Props;
    Props["text"] = IrisPropValue{Content};
    return Component(IrisElementTag::Text, Props, {}, nullptr);
}

Component MakeSlot(std::shared_ptr<Iris::IrisSlotCallable> Callable) {
    return Component(IrisElementTag::Slot, {}, {}, std::move(Callable));
}

Component MakePortal(Component Child, float X, float Y, float Width, float Height,
                     std::function<void()> OnDismiss = nullptr) {
    IrisProps Props;
    Props["x"] = IrisPropValue{X};
    Props["y"] = IrisPropValue{Y};
    Props["width"] = IrisPropValue{Width};
    Props["height"] = IrisPropValue{Height};
    Props["dismissOnOutsideClick"] = IrisPropValue{true};
    if (OnDismiss) Props["onDismiss"] = IrisPropValue{std::move(OnDismiss)};
    return Component(IrisElementTag::Portal, std::move(Props), {std::move(Child)}, nullptr);
}

// The full stack, against a REAL Penumbra tree: BuildWidgetTree (Stage 2) builds the
// static shell with the <Slot> position left empty, MakeMountFn wraps it for the
// reconciler, and iris::ResolveSlots splices the slot's initial render into the real
// Penumbra Box's own Children vector.
void TestSlotWiresIntoRealStaticPenumbraTree() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    Component RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Iris::MakeSlotCallable([]() -> Component { return MakeText("slot-content"); })),
        MakeText("after"),
    });
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto*                            RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    Expect(RootBox != nullptr && RootBox->Children.size() == 2,
           "the static build alone produces a real Penumbra Box with just the two static children");

    auto Slots = iris::ResolveSlots(*Root, RootNode, Mount);
    Expect(Slots.size() == 1, "one SlotState created");
    Expect(RootBox->Children.size() == 3, "the slot's content is now a real child of the real Penumbra Box");

    auto* Middle = dynamic_cast<Label*>(RootBox->Children[1].get());
    Expect(Middle != nullptr && Middle->Text == "slot-content",
           "and it's a real Label with the right text, in the correct position");
}

// The real point of wiring this up: a live Signal update reaching all the way through
// to a real Penumbra Box's Children, not just a mock.
void TestSignalUpdateReachesRealPenumbraTreeThroughFullStack() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    iris::Signal<bool> Show = false;
    Component       RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Iris::MakeSlotCallable(
            [&]() -> Component { return Show.get() ? MakeText("shown") : Component(nullptr); })),
        MakeText("after"),
    });
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto*                            RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    auto                             Slots = iris::ResolveSlots(*Root, RootNode, Mount);
    Expect(RootBox->Children.size() == 2, "initially hidden — only the two static children in the real Box");

    Show.set(true);
    iris::Tick();
    Expect(RootBox->Children.size() == 3,
           "iris::Signal -> iris::Tick -> SlotState -> Reconciler -> PenumbraWidget -> real Penumbra Box::Children "
           "-- the full stack updates the real widget tree");
    auto* Middle = dynamic_cast<Label*>(RootBox->Children[1].get());
    Expect(Middle != nullptr && Middle->Text == "shown", "with the correct real content");

    Show.set(false);
    iris::Tick();
    Expect(RootBox->Children.size() == 2, "and it's removed from the real Box again when the signal flips back");
}

void TestPortalPresentsItsOrdinaryReconciledChildThroughOverlayHost() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const iris::MountFn Mount = MakeMountFn(Context);

    Component Child = MakeText("menu");
    Child.Ref = IrisPropValue{std::string("menu-label")};
    Component Node = MakePortal(std::move(Child), 12.0f, 24.0f, 180.0f, 96.0f);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Node);

    Expect(Host.HasOverlays(), "a mounted Portal is presented by the configured OverlayHost");
    auto* PortalWrapper = dynamic_cast<PenumbraWidget*>(Root.get());
    Expect(Root->GetChildCount() == 1 && PortalWrapper != nullptr && PortalWrapper->GetByRef("menu-label") != nullptr,
           "the portal child remains an ordinary reconciled/ref-addressable child");

    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});
    auto* Surface = Host.GetChildAt(0);
    auto* LabelWidget = Surface ? dynamic_cast<Label*>(Surface->GetChildAt(0)) : nullptr;
    const auto Rect = LabelWidget ? LabelWidget->GetArrangedRect() : Penumbra::Rect{};
    Expect(LabelWidget != nullptr && LabelWidget->Text == "menu" && Rect.X == 12.0f && Rect.Y == 24.0f &&
               Rect.W == 180.0f && Rect.H == 96.0f,
           "OverlayHost placement is applied directly to the real declarative child");

    Iris::PreparePortalSubtreeForUnmount(Root.get());
}

void TestPortalWithoutGeometryFillsTheOverlayHost() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const iris::MountFn Mount = MakeMountFn(Context);

    IrisProps Props;
    Props["dismissOnOutsideClick"] = IrisPropValue{false};
    Component Node(IrisElementTag::Portal, std::move(Props), {MakeText("dialog")}, nullptr);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Node);

    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});
    auto* LabelWidget = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    Penumbra::Rect Rect = LabelWidget ? LabelWidget->GetArrangedRect() : Penumbra::Rect{};
    Expect(Rect.X == 0.0f && Rect.Y == 0.0f && Rect.W == 640.0f && Rect.H == 480.0f,
           "a Portal with no x/y/width/height fills the OverlayHost");

    Host.Arrange({0.0f, 0.0f, 800.0f, 300.0f});
    Rect = LabelWidget ? LabelWidget->GetArrangedRect() : Penumbra::Rect{};
    Expect(Rect.W == 800.0f && Rect.H == 300.0f, "and follows the OverlayHost when it is resized");

    Iris::PreparePortalSubtreeForUnmount(Root.get());
}

void TestMatchedPortalUpdatesPlacementAndChildInPlace() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const iris::MountFn Mount = MakeMountFn(Context);

    Component Old = MakePortal(MakeText("old"), 1.0f, 2.0f, 30.0f, 40.0f);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Old);
    auto* OriginalChild = Root->GetChildAt(0);

    Component New = MakePortal(MakeText("new"), 9.0f, 10.0f, 70.0f, 80.0f);
    iris::ReconcileWidget(Root, Old, New, Mount);
    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});

    auto* LabelWidget = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    const auto Rect = LabelWidget->GetArrangedRect();
    Expect(Root->GetChildAt(0) == OriginalChild && LabelWidget->Text == "new",
           "a matched Portal reconciles its ordinary child in place");
    Expect(Rect.X == 9.0f && Rect.Y == 10.0f && Rect.W == 70.0f && Rect.H == 80.0f,
           "a matched Portal updates OverlayHost placement without remounting content");

    Iris::PreparePortalSubtreeForUnmount(Root.get());
}

void TestPortalSurfaceTracksAKeyedChildRemount() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const iris::MountFn Mount = MakeMountFn(Context);

    Component OldChild = MakeText("old");
    OldChild.Key = IrisPropValue{std::string("old")};
    Component Old = MakePortal(std::move(OldChild), 2.0f, 3.0f, 40.0f, 50.0f);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Old);

    Component NewChild = MakeText("new");
    NewChild.Key = IrisPropValue{std::string("new")};
    Component New = MakePortal(std::move(NewChild), 2.0f, 3.0f, 40.0f, 50.0f);
    iris::ReconcileWidget(Root, Old, New, Mount);
    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});

    auto* Presented = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    Expect(Presented != nullptr && Presented->Text == "new" && Presented ==
               dynamic_cast<PenumbraWidget*>(Root->GetChildAt(0))->RawWidget(),
           "the overlay surface follows the Portal's current child after a keyed remount");

    Iris::PreparePortalSubtreeForUnmount(Root.get());
}

void TestOutsideDismissCanSynchronouslyReconcilePortalWithNestedSlots() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const iris::MountFn Mount = MakeMountFn(Context);

    iris::Signal<bool> Show = true;
    Component RootNode = MakeFrame({MakeSlot(Iris::MakeSlotCallable([&]() -> Component {
        if (!Show.get()) return nullptr;
        Component NestedContent = MakeFrame({
            MakeSlot(Iris::MakeSlotCallable([]() -> Component { return MakeText("nested"); })),
        });
        return MakePortal(std::move(NestedContent), 20.0f, 20.0f, 100.0f, 60.0f, [&]() {
            Show.set(false);
            iris::Tick();
        });
    }))});

    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto Slots = iris::ResolveSlots(*Root, RootNode, Mount);
    Expect(Host.HasOverlays(), "the conditional Portal and its nested Slot mount through the real stack");

    Penumbra::Platform::InputState Input;
    Input.MousePosition = {400.0f, 400.0f};
    Input.MouseButtonPressedThisFrame[0] = true;
    const bool Consumed = Host.UpdateInteractionState(Input);

    Expect(Consumed && !Host.HasOverlays(), "outside-click dismissal is consumed and removes the overlay");
    Expect(Root->GetChildCount() == 0,
           "OnDismiss may synchronously reconcile away the Portal after nested Slot owners are released");
}

} // namespace

void RunSlotWiringTests() {
    TestSlotWiresIntoRealStaticPenumbraTree();
    TestSignalUpdateReachesRealPenumbraTreeThroughFullStack();
    TestPortalPresentsItsOrdinaryReconciledChildThroughOverlayHost();
    TestPortalWithoutGeometryFillsTheOverlayHost();
    TestMatchedPortalUpdatesPlacementAndChildInPlace();
    TestPortalSurfaceTracksAKeyedChildRemount();
    TestOutsideDismissCanSynchronouslyReconcilePortalWithNestedSlots();
}
