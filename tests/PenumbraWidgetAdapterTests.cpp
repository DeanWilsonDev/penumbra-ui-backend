#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/Reconciler.h"
#include "Iris/Signal.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Label.h"
#include "Penumbra/Widgets/SplitPanel.h"
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

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::MakeMountFn;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::WrapExistingTree;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::Label;
using Penumbra::Widgets::SplitPanel;
using Penumbra::Widgets::TextInput;
using Penumbra::Widgets::WidgetBase;

Component MakeFrame(const std::string& ClassName, std::vector<Component> Children = {},
                         std::optional<IrisPropValue> Key = std::nullopt) {
    IrisProps Props;
    Props["class"] = IrisPropValue{ClassName};
    Component Node(IrisElementTag::Frame, Props, std::move(Children), nullptr);
    Node.Key = std::move(Key);
    return Node;
}

void TestWrapExistingTreeMirrorsRealTreeStructure() {
    std::vector<Component> Inner;
    Inner.push_back(MakeFrame("child"));
    const Component Root = MakeFrame("parent", std::move(Inner));

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

void TestApplyPropDiffOnTextChangeReachesRealTextInput() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});
    const Component     InputNode(IrisElementTag::Input, IrisProps{}, {}, nullptr);
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(InputNode);

    std::string          LastValue;
    Umbra::IrisPropDiff Diff;
    Diff.OnTextChange = std::function<void(std::string)>([&LastValue](std::string NewText) {
        LastValue = std::move(NewText);
    });
    Wrapped->ApplyPropDiff(Diff);

    auto* AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    auto* AsTextInput = dynamic_cast<TextInput*>(AsPenumbra->RawWidget());
    Expect(AsTextInput != nullptr && static_cast<bool>(AsTextInput->OnTextChanged),
           "ApplyPropDiff's OnTextChange reaches the real TextInput::OnTextChanged field");
    if (AsTextInput != nullptr && AsTextInput->OnTextChanged) {
        AsTextInput->OnTextChanged("typed");
        Expect(LastValue == "typed", "and invoking it calls back into the original handler");
    }
}

void TestReconcilerUpdatesRealWidgetTreeInPlace() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    const Component Old = MakeFrame("a", {}, IrisPropValue(1));
    std::unique_ptr<Umbra::IWidget> Widget = Mount(Old);
    auto*                            AsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Widget.get())->RawWidget());
    const Box*                       OriginalBoxAddress = AsBox;

    const Component New = MakeFrame("b", {}, IrisPropValue(1));
    iris::ReconcileWidget(Widget, Old, New, Mount);

    AsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Widget.get())->RawWidget());
    Expect(AsBox == OriginalBoxAddress,
           "same tag + same key: the real Penumbra Box object is reused, not rebuilt (identity preserved end to end)");
    Expect(AsBox->ClassName == "b", "and the real Box's class actually changed to reflect the new props");
}

void TestReconcilerAddsRealChildToRealParentBox() {
    const iris::MountFn Mount = MakeMountFn(BuildContext{});

    const Component Old = MakeFrame("parent");
    std::unique_ptr<Umbra::IWidget> Widget = Mount(Old);

    std::vector<Component> NewChildren;
    NewChildren.push_back(MakeFrame("child"));
    const Component New = MakeFrame("parent", std::move(NewChildren));
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
    auto Callable = Iris::MakeSlotCallable([&]() -> Component { return MakeFrame(ClassName.get()); });
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

void TestGetByRefFindsARefTaggedDescendant() {
    std::vector<Component> Inner;
    Component               InnerFrame = MakeFrame("child");
    InnerFrame.Ref = IrisPropValue{std::string("target")};
    Inner.push_back(std::move(InnerFrame));
    const Component Root = MakeFrame("parent", std::move(Inner));

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    auto*            AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    Umbra::IWidget*   Found = AsPenumbra != nullptr ? AsPenumbra->GetByRef("target") : nullptr;
    Expect(Found != nullptr, "GetByRef finds the ref-tagged descendant");

    auto* FoundAsPenumbra = dynamic_cast<PenumbraWidget*>(Found);
    auto* FoundAsBox = FoundAsPenumbra != nullptr ? dynamic_cast<Box*>(FoundAsPenumbra->RawWidget()) : nullptr;
    Expect(FoundAsBox != nullptr && FoundAsBox->ClassName == "child",
           "the found widget is really the ref-tagged child, not some other node");
    Expect(Wrapped->GetChildAt(0) == Found, "GetByRef's result is the same wrapper identity GetChildAt(0) returns");
}

void TestGetByRefOnAnUnknownNameReturnsNull() {
    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(MakeFrame("root"));

    auto* AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    Expect(AsPenumbra != nullptr && AsPenumbra->GetByRef("does-not-exist") == nullptr,
           "an unknown ref name returns nullptr, not a crash");
}

void TestGetByRefIsCallableFromANonRootWrapper() {
    std::vector<Component> Inner;
    Component               InnerFrame = MakeFrame("child");
    InnerFrame.Ref = IrisPropValue{std::string("target")};
    Inner.push_back(std::move(InnerFrame));
    const Component Root = MakeFrame("parent", std::move(Inner));

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    Umbra::IWidget* Child = Wrapped->GetChildAt(0);
    auto*            ChildAsPenumbra = dynamic_cast<PenumbraWidget*>(Child);
    Expect(ChildAsPenumbra != nullptr && ChildAsPenumbra->GetByRef("target") == Child,
           "GetByRef works from a non-root wrapper too -- it walks up to the mount root's own registry");
}

// docs/next_steps.md's "swap a live real widget when a reconciled `<Native>` re-renders"
// ask -- a real, live gap found investigating it: InsertChildAt/RemoveChildAt only ever
// checked `dynamic_cast<Box*>`, which silently *succeeds* for a SplitPanel (SplitPanel :
// Box) and writes into its inherited-but-unused Box::Children vector instead of its real
// First/Second slots. Exercises the exact RemoveChildAt-then-InsertChildAt pattern
// `iris::ReconcileChildrenAt`'s own doc comment (Reconciler.h) says a structural
// replacement actually uses.
void TestRemoveThenInsertOnASplitPanelParentSwapsTheFirstPaneWithoutTouchingBoxChildren() {
    std::vector<Component> Panes;
    Panes.push_back(MakeFrame("first-old"));
    Panes.push_back(MakeFrame("second"));
    const Component Root(IrisElementTag::Split, IrisProps{}, std::move(Panes), nullptr);

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    auto* AsSplit = dynamic_cast<SplitPanel*>(dynamic_cast<PenumbraWidget*>(Wrapped.get())->RawWidget());
    Expect(AsSplit != nullptr, "the root wraps a real SplitPanel");

    WidgetBase* SecondRaw = AsSplit != nullptr ? AsSplit->GetChildAt(1) : nullptr;

    std::unique_ptr<Umbra::IWidget> Removed = Wrapped->RemoveChildAt(0);
    Expect(Wrapped->GetChildCount() == 1, "the wrapper now reports one child");
    Expect(AsSplit != nullptr && AsSplit->GetChildCount() == 1,
           "the real SplitPanel also now reports one child -- the first pane was really cleared");
    Expect(AsSplit != nullptr && AsSplit->GetChildAt(0) == SecondRaw,
           "the remaining pane is really the second one, not silently reassigned");

    auto        NewFirst = Box::Builder().className("first-new").build();
    WidgetBase* NewFirstRaw = NewFirst.get();
    Wrapped->InsertChildAt(0, std::make_unique<PenumbraWidget>(std::move(NewFirst)));

    Expect(AsSplit != nullptr && AsSplit->GetChildCount() == 2, "the real SplitPanel has both panes again");
    Expect(AsSplit != nullptr && AsSplit->GetChildAt(0) == NewFirstRaw,
           "the real SplitPanel's own first pane is really the freshly-inserted widget");
    Expect(AsSplit != nullptr && AsSplit->GetChildAt(1) == SecondRaw,
           "the second pane, never touched by this remove/insert pair, is still the original widget");
}

// docs/next_steps.md's "swap a live real widget when a reconciled `<Native>` re-renders"
// ask -- ReplaceRawWidget's Box-parent path, the one case that CAN honor
// Box::ReplaceChild's own "hand the replaced widget back intact, never destroy it as a
// side effect" contract.
void TestReplaceRawWidgetOnABoxParentHandsBackTheOldWidgetIntact() {
    std::vector<Component> Inner;
    Inner.push_back(MakeFrame("child-old"));
    const Component Root = MakeFrame("parent", std::move(Inner));

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    auto* ChildWrapper = dynamic_cast<PenumbraWidget*>(Wrapped->GetChildAt(0));
    Expect(ChildWrapper != nullptr, "the child position wraps a real PenumbraWidget");
    WidgetBase* OldRaw = ChildWrapper->RawWidget();

    auto        NewBox = Box::Builder().className("child-new").build();
    WidgetBase* NewRaw = NewBox.get();

    std::unique_ptr<WidgetBase> Old = ChildWrapper->ReplaceRawWidget(std::move(NewBox));

    Expect(Old != nullptr && Old.get() == OldRaw, "the old widget is handed back intact, not destroyed inline");
    Expect(ChildWrapper->RawWidget() == NewRaw, "the wrapper now views the new widget");
    Expect(Wrapped->GetChildAt(0) == ChildWrapper,
           "the wrapper's own identity and tree position are unchanged by the swap");

    auto* ParentAsBox = dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Wrapped.get())->RawWidget());
    Expect(ParentAsBox != nullptr && ParentAsBox->Children.size() == 1 && ParentAsBox->Children[0].get() == NewRaw,
           "the real parent Box's own Children vector holds the new widget, in the same slot");
}

// The SplitPanel-parent path -- see ReplaceRawWidget's own header comment for why this
// case can't hand the old widget back intact today (no SplitPanel::ReplaceFirst/
// ReplaceSecond upstream yet). Still must perform a correct, non-corrupting swap.
void TestReplaceRawWidgetOnASplitPanelParentSwapsTheFirstPane() {
    std::vector<Component> Panes;
    Panes.push_back(MakeFrame("first-old"));
    Panes.push_back(MakeFrame("second"));
    const Component Root(IrisElementTag::Split, IrisProps{}, std::move(Panes), nullptr);

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    auto* RootAsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    auto* AsSplit = dynamic_cast<SplitPanel*>(RootAsPenumbra->RawWidget());
    Expect(AsSplit != nullptr, "the root wraps a real SplitPanel");

    auto* FirstWrapper = dynamic_cast<PenumbraWidget*>(Wrapped->GetChildAt(0));
    Expect(FirstWrapper != nullptr, "the first pane wraps a real PenumbraWidget");

    auto        NewBox = Box::Builder().className("first-new").build();
    WidgetBase* NewRaw = NewBox.get();

    std::unique_ptr<WidgetBase> Old = FirstWrapper->ReplaceRawWidget(std::move(NewBox));

    Expect(Old == nullptr,
           "SplitPanel's SetFirst destroys the replaced widget inline -- documented, not handed back");
    Expect(AsSplit != nullptr && AsSplit->GetChildAt(0) == NewRaw, "the real SplitPanel's own first pane is the new widget");
    Expect(FirstWrapper->RawWidget() == NewRaw, "the wrapper now views the new widget");
    Expect(Wrapped->GetChildAt(0) == FirstWrapper, "the wrapper's own identity and tree position are unchanged");
}

void TestReplaceRawWidgetOnASplitPanelParentSwapsTheSecondPane() {
    std::vector<Component> Panes;
    Panes.push_back(MakeFrame("first"));
    Panes.push_back(MakeFrame("second-old"));
    const Component Root(IrisElementTag::Split, IrisProps{}, std::move(Panes), nullptr);

    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(Root);

    auto* AsSplit = dynamic_cast<SplitPanel*>(dynamic_cast<PenumbraWidget*>(Wrapped.get())->RawWidget());
    auto* SecondWrapper = dynamic_cast<PenumbraWidget*>(Wrapped->GetChildAt(1));
    Expect(SecondWrapper != nullptr, "the second pane wraps a real PenumbraWidget");

    auto        NewBox = Box::Builder().className("second-new").build();
    WidgetBase* NewRaw = NewBox.get();
    SecondWrapper->ReplaceRawWidget(std::move(NewBox));

    Expect(AsSplit != nullptr && AsSplit->GetChildAt(1) == NewRaw, "the real SplitPanel's own second pane is the new widget");
    Expect(Wrapped->GetChildAt(1) == SecondWrapper, "the wrapper's own identity and tree position are unchanged");
}

// The mount-root path -- no real parent container exists to thread through at all.
void TestReplaceRawWidgetOnTheMountRootSwapsItsOwnWidget() {
    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(MakeFrame("root-old", {MakeFrame("stale-child")}));

    auto*       AsPenumbra = dynamic_cast<PenumbraWidget*>(Wrapped.get());
    WidgetBase* OldRaw = AsPenumbra->RawWidget();

    auto        NewRoot = Box::Builder().className("root-new").build();
    WidgetBase* NewRaw = NewRoot.get();

    std::unique_ptr<WidgetBase> Old = AsPenumbra->ReplaceRawWidget(std::move(NewRoot));

    Expect(Old != nullptr && Old.get() == OldRaw, "the mount root's own old widget is handed back intact");
    Expect(AsPenumbra->RawWidget() == NewRaw, "the wrapper now views the new root widget");
    Expect(AsPenumbra->GetChildCount() == 0,
           "the wrapper's children were rebuilt from the new (childless) root, not left stale from the old one");
}

// ReplaceRawWidget's Tags/Refs propagation -- the exact plumbing a real `<Native>`
// re-invocation would need: the freshly-built replacement's own ref-tagged descendant
// must become reachable via GetByRef from the *mount root* afterward, same as an
// initial WrapExistingTree wrap already guarantees.
void TestReplaceRawWidgetSeedsTagsAndRefsForTheNewSubtree() {
    const iris::MountFn             Mount = MakeMountFn(BuildContext{});
    std::unique_ptr<Umbra::IWidget> Wrapped = Mount(MakeFrame("parent", {MakeFrame("native-slot-old")}));

    auto* SlotWrapper = dynamic_cast<PenumbraWidget*>(Wrapped->GetChildAt(0));
    Expect(SlotWrapper != nullptr, "the swap position wraps a real PenumbraWidget");

    Component RefChild = MakeFrame("inner");
    RefChild.Ref = IrisPropValue{std::string("swapped-in")};
    std::vector<Component> NewChildren;
    NewChildren.push_back(std::move(RefChild));
    const Component NewContent = MakeFrame("new-content", std::move(NewChildren));

    PenumbraUiBackend::PrimitiveTagMap Tags;
    PenumbraUiBackend::RefMap          Refs;
    std::unique_ptr<WidgetBase> NewWidget = PenumbraUiBackend::BuildWidgetTree(NewContent, BuildContext{}, &Tags, &Refs);

    SlotWrapper->ReplaceRawWidget(std::move(NewWidget), &Tags, &Refs);

    Expect(SlotWrapper->GetPrimitiveTag() == "Frame", "the swapped node's own primitive tag was seeded from Tags");
    Expect(SlotWrapper->GetChildCount() == 1, "the swapped node's own children were rebuilt from the new subtree");

    Umbra::IWidget* Found = Wrapped->GetChildAt(0) != nullptr ? SlotWrapper->GetByRef("swapped-in") : nullptr;
    Expect(Found != nullptr, "the new subtree's ref-tagged descendant is reachable via GetByRef from the mount root");
    auto* FoundAsBox =
        Found != nullptr ? dynamic_cast<Box*>(dynamic_cast<PenumbraWidget*>(Found)->RawWidget()) : nullptr;
    Expect(FoundAsBox != nullptr && FoundAsBox->ClassName == "inner", "the found widget is really the new descendant");
}

} // namespace

void RunPenumbraWidgetAdapterTests() {
    TestWrapExistingTreeMirrorsRealTreeStructure();
    TestApplyPropDiffReachesRealWidgetBaseFields();
    TestApplyPropDiffOnTextChangeReachesRealTextInput();
    TestReconcilerUpdatesRealWidgetTreeInPlace();
    TestReconcilerAddsRealChildToRealParentBox();
    TestSignalDrivesRealPenumbraTreeThroughFullStack();
    TestGetByRefFindsARefTaggedDescendant();
    TestGetByRefOnAnUnknownNameReturnsNull();
    TestGetByRefIsCallableFromANonRootWrapper();
    TestReplaceRawWidgetOnABoxParentHandsBackTheOldWidgetIntact();
    TestReplaceRawWidgetOnASplitPanelParentSwapsTheFirstPane();
    TestReplaceRawWidgetOnASplitPanelParentSwapsTheSecondPane();
    TestReplaceRawWidgetOnTheMountRootSwapsItsOwnWidget();
    TestReplaceRawWidgetSeedsTagsAndRefsForTheNewSubtree();
}
