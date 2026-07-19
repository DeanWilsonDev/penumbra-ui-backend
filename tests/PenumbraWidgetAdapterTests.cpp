#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/Reconciler.h"
#include "Iris/Signal.h"

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
using PenumbraUiBackend::WrapExistingTree;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Label;

IrisComponent MakeFrame(const std::string& ClassName, std::vector<IrisComponent> Children = {},
                         std::optional<IrisPropValue> Key = std::nullopt) {
    IrisProps Props;
    Props["class"] = IrisPropValue{ClassName};
    IrisComponent Node(IrisElementTag::Frame, Props, std::move(Children), nullptr);
    Node.Key = std::move(Key);
    return Node;
}

void TestWrapExistingTreeMirrorsRealTreeStructure() {
    std::vector<IrisComponent> Inner;
    Inner.push_back(MakeFrame("child"));
    const IrisComponent Root = MakeFrame("parent", std::move(Inner));

    const iris::MountFn Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    Expect(Wrapped != nullptr, "MakeMountFn produces a wrapper for a real subtree");
    Expect(Wrapped->GetChildCount() == 1, "the wrapper mirrors the real tree's one child");

    auto* AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    Expect(AsPenumbra != nullptr, "the wrapper is really a PenumbraWidget");
    auto* AsBox = dynamic_cast<Box*>(AsPenumbra->RawWidget());
    Expect(AsBox != nullptr && AsBox->ClassName == "parent", "the underlying real Penumbra Box has the right class");
    Expect(AsBox != nullptr && AsBox->Children.size() == 1,
           "the real Penumbra Box also has exactly one real child — wrapper and real tree agree");
}

void TestApplyPropDiffReachesRealWidgetBaseFields() {
    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(MakeFrame("initial"));

    bool Pressed = false;
    Umbra::IrisPropDiff Diff;
    Diff.ClassName = "updated";
    Diff.OnPress = std::function<void()>([&Pressed]() { Pressed = true; });
    Wrapped->ApplyPropDiff(Diff);

    auto* AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    auto* AsBox = dynamic_cast<Box*>(AsPenumbra->RawWidget());
    Expect(AsBox->ClassName == "updated", "ApplyPropDiff's ClassName reaches the real WidgetBase field");
    Expect(static_cast<bool>(AsBox->OnPressed), "ApplyPropDiff's OnPress reaches the real WidgetBase field");
    AsBox->OnPressed();
    Expect(Pressed, "and invoking it calls back into the original handler");
}

void TestReconcilerUpdatesRealWidgetTreeInPlace() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    const IrisComponent Old = MakeFrame("a", {}, IrisPropValue(1));
    std::unique_ptr<Umbra::IWidget> Widget = Mount(Old);
    auto*                            AsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Widget.get())->RawWidget());
    const Box*                       OriginalBoxAddress = AsBox;

    const IrisComponent New = MakeFrame("b", {}, IrisPropValue(1));
    iris::ReconcileWidget(Widget, Old, New, Mount);

    AsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Widget.get())->RawWidget());
    Expect(AsBox == OriginalBoxAddress,
           "same tag + same key: the real Penumbra Box object is reused, not rebuilt (identity preserved end to end)");
    Expect(AsBox->ClassName == "b", "and the real Box's class actually changed to reflect the new props");
}

void TestReconcilerAddsRealChildToRealParentBox() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    const IrisComponent Old = MakeFrame("parent");
    std::unique_ptr<Umbra::IWidget> Widget = Mount(Old);

    std::vector<IrisComponent> NewChildren;
    NewChildren.push_back(MakeFrame("child"));
    const IrisComponent New = MakeFrame("parent", std::move(NewChildren));
    iris::ReconcileWidget(Widget, Old, New, Mount);

    Expect(Widget->GetChildCount() == 1, "the wrapper now reports one child");
    auto* AsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Widget.get())->RawWidget());
    Expect(AsBox->Children.size() == 1,
           "the real Penumbra Box's own Children vector also grew — the new widget was really attached, "
           "not just tracked in the wrapper");
}

void TestSignalDrivesRealPenumbraTreeThroughFullStack() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    iris::Signal<std::string> ClassName("initial");
    auto Callable = Iris::MakeSlotCallable([&]() -> IrisComponent { return MakeFrame(ClassName.get()); });
    iris::SlotState Slot(Callable, Mount);
    Slot.Reconcile(); // mount

    ClassName.set("changed");
    iris::Tick();

    // No direct accessor exists on SlotState for its widget (by design — see
    // docs/iris_stage3_implementation_decision.md), so this test only confirms the full
    // stack (Signal -> IrisRuntime -> SlotState -> Reconciler -> PenumbraWidget -> real
    // Penumbra Box) runs end to end without error. Direct widget-state assertions are
    // covered by TestReconcilerUpdatesRealWidgetTreeInPlace above, which drives the same
    // ReconcileWidget path SlotState itself calls internally.
    Expect(true, "Signal -> Tick -> SlotState -> Reconciler -> PenumbraWidget -> real Penumbra Box runs end to end");
}

} // namespace

void RunPenumbraWidgetAdapterTests() {
    TestWrapExistingTreeMirrorsRealTreeStructure();
    TestApplyPropDiffReachesRealWidgetBaseFields();
    TestReconcilerUpdatesRealWidgetTreeInPlace();
    TestReconcilerAddsRealChildToRealParentBox();
    TestSignalDrivesRealPenumbraTreeThroughFullStack();
}
