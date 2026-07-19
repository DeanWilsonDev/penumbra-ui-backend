#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/Signal.h"
#include "Iris/SlotResolution.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Label.h"

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

using Iris::IrisComponent;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::MakeMountFn;
using PenumbraUiBackend::PenumbraWidget;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Label;

IrisComponent MakeFrame(std::vector<IrisComponent> Children = {}) {
    return IrisComponent(IrisElementTag::Frame, {}, std::move(Children), nullptr);
}

IrisComponent MakeText(const std::string& Content) {
    IrisProps Props;
    Props["text"] = IrisPropValue{Content};
    return IrisComponent(IrisElementTag::Text, Props, {}, nullptr);
}

IrisComponent MakeSlot(std::shared_ptr<Iris::IrisSlotCallable> Callable) {
    return IrisComponent(IrisElementTag::Slot, {}, {}, std::move(Callable));
}

// The full stack, against a REAL Penumbra tree: BuildWidgetTree (Stage 2) builds the
// static shell with the <Slot> position left empty, MakeMountFn wraps it for the
// reconciler, and iris::ResolveSlots splices the slot's initial render into the real
// Penumbra Box's own Children vector.
void TestSlotWiresIntoRealStaticPenumbraTree() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    IrisComponent RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Iris::MakeSlotCallable([]() -> IrisComponent { return MakeText("slot-content"); })),
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
    IrisComponent       RootNode = MakeFrame({
        MakeText("before"),
        MakeSlot(Iris::MakeSlotCallable(
            [&]() -> IrisComponent { return Show.get() ? MakeText("shown") : IrisComponent(nullptr); })),
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

} // namespace

void RunSlotWiringTests() {
    TestSlotWiresIntoRealStaticPenumbraTree();
    TestSignalUpdateReachesRealPenumbraTreeThroughFullStack();
}
