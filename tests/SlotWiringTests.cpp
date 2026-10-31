#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "chaos/Signal.h"
#include "chaos/Reconciler.h"
#include "chaos/SlotResolution.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/OverlayHost.h"
#include "Penumbra/Widgets/TextInput.h"

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

using Chaos::Component;
using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Chaos::ChaosPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::MakeMountFn;
using PenumbraUiBackend::PenumbraWidget;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::OverlayHost;

Component MakeFrame(std::vector<Component> Children = {}) {
    return Component(ChaosElementTag::Frame, {}, std::move(Children), nullptr);
}

Component MakeText(const std::string& Content) {
    ChaosProps Props;
    Props["text"] = ChaosPropValue{Content};
    return Component(ChaosElementTag::Text, Props, {}, nullptr);
}

Component MakeSlot(std::shared_ptr<Chaos::ChaosSlotCallable> Callable) {
    return Component(ChaosElementTag::Slot, {}, {}, std::move(Callable));
}

Component MakePortal(Component Child, float X, float Y, float Width, float Height,
                     std::function<void()> OnDismiss = nullptr) {
    ChaosProps Props;
    Props["x"] = ChaosPropValue{X};
    Props["y"] = ChaosPropValue{Y};
    Props["width"] = ChaosPropValue{Width};
    Props["height"] = ChaosPropValue{Height};
    Props["dismissOnOutsideClick"] = ChaosPropValue{true};
    if (OnDismiss) Props["onDismiss"] = ChaosPropValue{std::move(OnDismiss)};
    return Component(ChaosElementTag::Portal, std::move(Props), {std::move(Child)}, nullptr);
}

// The full stack, against a REAL Penumbra tree: BuildWidgetTree (Stage 2) builds the
// static shell with the <Slot> position left empty, MakeMountFn wraps it for the
// reconciler, and chaos::ResolveSlots splices the slot's initial render into the real
// Penumbra Box's own Children vector.
void TestSlotWiresIntoRealStaticPenumbraTree() {
    const chaos::MountFn Mount = MakeMountFn(BuildContext{});

    Component RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Chaos::MakeSlotCallable([]() -> Component { return MakeText("slot-content"); })),
        MakeText("after"),
    });
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto*                            RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    Expect(RootBox != nullptr && RootBox->Children.size() == 2,
           "the static build alone produces a real Penumbra Box with just the two static children");

    auto Slots = chaos::ResolveSlots(*Root, RootNode, Mount);
    Expect(Slots.size() == 1, "one SlotState created");
    Expect(RootBox->Children.size() == 3, "the slot's content is now a real child of the real Penumbra Box");

    auto* Middle = dynamic_cast<Label*>(RootBox->Children[1].get());
    Expect(Middle != nullptr && Middle->Text == "slot-content",
           "and it's a real Label with the right text, in the correct position");
}

// The real point of wiring this up: a live Signal update reaching all the way through
// to a real Penumbra Box's Children, not just a mock.
void TestSignalUpdateReachesRealPenumbraTreeThroughFullStack() {
    const chaos::MountFn Mount = MakeMountFn(BuildContext{});

    chaos::Signal<bool> Show = false;
    Component       RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Chaos::MakeSlotCallable(
            [&]() -> Component { return Show.get() ? MakeText("shown") : Component(nullptr); })),
        MakeText("after"),
    });
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto*                            RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    auto                             Slots = chaos::ResolveSlots(*Root, RootNode, Mount);
    Expect(RootBox->Children.size() == 2, "initially hidden — only the two static children in the real Box");

    Show.set(true);
    chaos::Tick();
    Expect(RootBox->Children.size() == 3,
           "chaos::Signal -> chaos::Tick -> SlotState -> Reconciler -> PenumbraWidget -> real Penumbra Box::Children "
           "-- the full stack updates the real widget tree");
    auto* Middle = dynamic_cast<Label*>(RootBox->Children[1].get());
    Expect(Middle != nullptr && Middle->Text == "shown", "with the correct real content");

    Show.set(false);
    chaos::Tick();
    Expect(RootBox->Children.size() == 2, "and it's removed from the real Box again when the signal flips back");
}

void TestPortalPresentsItsOrdinaryReconciledChildThroughOverlayHost() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const chaos::MountFn Mount = MakeMountFn(Context);

    Component Child = MakeText("menu");
    Child.Ref = ChaosPropValue{std::string("menu-label")};
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

    Chaos::PreparePortalSubtreeForUnmount(Root.get());
}

void TestPortalWithoutGeometryFillsTheOverlayHost() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const chaos::MountFn Mount = MakeMountFn(Context);

    ChaosProps Props;
    Props["dismissOnOutsideClick"] = ChaosPropValue{false};
    Component Node(ChaosElementTag::Portal, std::move(Props), {MakeText("dialog")}, nullptr);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Node);

    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});
    auto* LabelWidget = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    Penumbra::Rect Rect = LabelWidget ? LabelWidget->GetArrangedRect() : Penumbra::Rect{};
    Expect(Rect.X == 0.0f && Rect.Y == 0.0f && Rect.W == 640.0f && Rect.H == 480.0f,
           "a Portal with no x/y/width/height fills the OverlayHost");

    Host.Arrange({0.0f, 0.0f, 800.0f, 300.0f});
    Rect = LabelWidget ? LabelWidget->GetArrangedRect() : Penumbra::Rect{};
    Expect(Rect.W == 800.0f && Rect.H == 300.0f, "and follows the OverlayHost when it is resized");

    Chaos::PreparePortalSubtreeForUnmount(Root.get());
}

void TestMatchedPortalUpdatesPlacementAndChildInPlace() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const chaos::MountFn Mount = MakeMountFn(Context);

    Component Old = MakePortal(MakeText("old"), 1.0f, 2.0f, 30.0f, 40.0f);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Old);
    auto* OriginalChild = Root->GetChildAt(0);

    Component New = MakePortal(MakeText("new"), 9.0f, 10.0f, 70.0f, 80.0f);
    chaos::ReconcileWidget(Root, Old, New, Mount);
    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});

    auto* LabelWidget = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    const auto Rect = LabelWidget->GetArrangedRect();
    Expect(Root->GetChildAt(0) == OriginalChild && LabelWidget->Text == "new",
           "a matched Portal reconciles its ordinary child in place");
    Expect(Rect.X == 9.0f && Rect.Y == 10.0f && Rect.W == 70.0f && Rect.H == 80.0f,
           "a matched Portal updates OverlayHost placement without remounting content");

    Chaos::PreparePortalSubtreeForUnmount(Root.get());
}

void TestPortalSurfaceTracksAKeyedChildRemount() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const chaos::MountFn Mount = MakeMountFn(Context);

    Component OldChild = MakeText("old");
    OldChild.Key = ChaosPropValue{std::string("old")};
    Component Old = MakePortal(std::move(OldChild), 2.0f, 3.0f, 40.0f, 50.0f);
    std::unique_ptr<Umbra::IWidget> Root = Mount(Old);

    Component NewChild = MakeText("new");
    NewChild.Key = ChaosPropValue{std::string("new")};
    Component New = MakePortal(std::move(NewChild), 2.0f, 3.0f, 40.0f, 50.0f);
    chaos::ReconcileWidget(Root, Old, New, Mount);
    Host.Arrange({0.0f, 0.0f, 640.0f, 480.0f});

    auto* Presented = dynamic_cast<Label*>(Host.GetChildAt(0)->GetChildAt(0));
    Expect(Presented != nullptr && Presented->Text == "new" && Presented ==
               dynamic_cast<PenumbraWidget*>(Root->GetChildAt(0))->RawWidget(),
           "the overlay surface follows the Portal's current child after a keyed remount");

    Chaos::PreparePortalSubtreeForUnmount(Root.get());
}

void TestOutsideDismissCanSynchronouslyReconcilePortalWithNestedSlots() {
    OverlayHost Host;
    BuildContext Context;
    Context.OverlayHost = &Host;
    const chaos::MountFn Mount = MakeMountFn(Context);

    chaos::Signal<bool> Show = true;
    Component RootNode = MakeFrame({MakeSlot(Chaos::MakeSlotCallable([&]() -> Component {
        if (!Show.get()) return nullptr;
        Component NestedContent = MakeFrame({
            MakeSlot(Chaos::MakeSlotCallable([]() -> Component { return MakeText("nested"); })),
        });
        return MakePortal(std::move(NestedContent), 20.0f, 20.0f, 100.0f, 60.0f, [&]() {
            Show.set(false);
            chaos::Tick();
        });
    }))});

    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto Slots = chaos::ResolveSlots(*Root, RootNode, Mount);
    Expect(Host.HasOverlays(), "the conditional Portal and its nested Slot mount through the real stack");

    Penumbra::Platform::InputState Input;
    Input.MousePosition = {400.0f, 400.0f};
    Input.MouseButtonPressedThisFrame[0] = true;
    const bool Consumed = Host.UpdateInteractionState(Input);

    Expect(Consumed && !Host.HasOverlays(), "outside-click dismissal is consumed and removes the overlay");
    Expect(Root->GetChildCount() == 0,
           "OnDismiss may synchronously reconcile away the Portal after nested Slot owners are released");
}


struct InspectorRowState {
    std::string Key;
    std::string Text;
};

Component MakeKeyedInput(const InspectorRowState& Row) {
    ChaosProps Props;
    Props["text"] = ChaosPropValue{Row.Text};
    Component Input(ChaosElementTag::Input, Props, {}, nullptr);
    Input.Key = ChaosPropValue{Row.Key};
    return Input;
}

Penumbra::Widgets::TextInput* SlottedInput(Box& RootBox) {
    if (RootBox.Children.empty()) {
        return nullptr;
    }
    auto* Row = dynamic_cast<Box*>(RootBox.Children[0].get());
    if (Row == nullptr || Row->Children.empty()) {
        return nullptr;
    }
    return dynamic_cast<Penumbra::Widgets::TextInput*>(Row->Children[0].get());
}

void TestSlotUpdatesAnUnfocusedInputsTextInPlace() {
    Penumbra::Widgets::FocusState Focus;
    BuildContext                  Context;
    Context.Focus = &Focus;
    const chaos::MountFn Mount = MakeMountFn(Context);

    chaos::Signal<InspectorRowState> Selected = InspectorRowState{"entity-1", "Alpha"};
    Component RootNode = MakeFrame({MakeSlot(Chaos::MakeSlotCallable(
        [&]() -> Component { return MakeFrame({MakeKeyedInput(Selected.get())}); }))});
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto* RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    auto  Slots   = chaos::ResolveSlots(*Root, RootNode, Mount);

    Penumbra::Widgets::TextInput* Before = SlottedInput(*RootBox);
    Expect(Before != nullptr && Before->Text == "Alpha", "a slot mounts a keyed <input> with its text");

    Selected.set(InspectorRowState{"entity-1", "Alpha 2"});
    chaos::Tick();
    Penumbra::Widgets::TextInput* After = SlottedInput(*RootBox);
    Expect(After == Before && After != nullptr && After->Text == "Alpha 2",
           "a new text prop on an unfocused <input> with the same key updates the same TextInput");
}

void TestSlotRemountsAKeyedInputWhenItsKeyChanges() {
    Penumbra::Widgets::FocusState Focus;
    BuildContext                  Context;
    Context.Focus = &Focus;
    const chaos::MountFn Mount = MakeMountFn(Context);

    chaos::Signal<InspectorRowState> Selected = InspectorRowState{"entity-1", "Alpha"};
    Component RootNode = MakeFrame({MakeSlot(Chaos::MakeSlotCallable(
        [&]() -> Component { return MakeFrame({MakeKeyedInput(Selected.get())}); }))});
    std::unique_ptr<Umbra::IWidget> Root = Mount(RootNode);
    auto* RootBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Root.get())->RawWidget());
    auto  Slots   = chaos::ResolveSlots(*Root, RootNode, Mount);

    Penumbra::Widgets::TextInput* Before = SlottedInput(*RootBox);
    if (Before == nullptr) {
        Expect(false, "a slot mounts a keyed <input>");
        return;
    }
    Focus.Focused = Before;
    Before->UpdateInteractionState(Penumbra::Platform::InputState{});
    Before->PreferredWidthLogical = 321.0f;

    Selected.set(InspectorRowState{"entity-2", "Beta"});
    chaos::Tick();
    Penumbra::Widgets::TextInput* After = SlottedInput(*RootBox);
    Expect(After != nullptr && After->PreferredWidthLogical != 321.0f && After->Text == "Beta",
           "changing a keyed <input>'s key inside a slot remounts it with the new text, even while the old one had focus");
    Expect(Focus.Focused != Before, "the remounted input's predecessor no longer holds focus");
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
    TestSlotUpdatesAnUnfocusedInputsTextInPlace();
    TestSlotRemountsAKeyedInputWhenItsKeyChanges();
}
