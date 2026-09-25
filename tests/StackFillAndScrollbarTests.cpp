#include "PenumbraUiBackend/Lustre/StyleApplier.h"
#include "PenumbraUiBackend/Walker.h"

#include "Lustre/Parser.h"

#include "Penumbra/Platform/InputState.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/ScrollablePanel.h"
#include "Penumbra/Widgets/TextArea.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern int Failures;

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

bool Near(float A, float B) { return std::fabs(A - B) < 0.01f; }

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::Lustre::LustreStyleApplier;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::ScrollablePanel;
using Penumbra::Widgets::TextArea;
using Penumbra::Widgets::WidgetBase;

Component MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

IrisProps WithClass(const std::string& ClassName) {
    IrisProps Props;
    Props["class"] = IrisPropValue{ClassName};
    return Props;
}

::Lustre::Stylesheet ParseOrDie(const std::string& Source) {
    ::Lustre::Parser      Parser(Source, "StackFill.lustre");
    ::Lustre::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "stack-fill fixture stylesheet parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Lustre::Stylesheet{};
}

struct StyledBuild {
    ::Lustre::Stylesheet          Sheet;
    ::Lustre::StylesheetSet       Sheets;
    LustreStyleApplier            Applier;
    BuildContext                  Context;
    std::unique_ptr<WidgetBase>   Root;

    StyledBuild(const std::string& Source, const Component& Node) : Sheet(ParseOrDie(Source)), Sheets{nullptr, &Sheet} {
        Context.Style = &Sheets;
        Context.StyleApplier = &Applier;
        Root = BuildWidgetTree(Node, Context);
    }
};

const char* kLaneSheet = R"(
.lane {
    display: stack;
    flex-direction: column;
    align-items: stretch;
    gap: 8px;
    padding: 10px;
}

.lane-header {
    height: 30px;
}

.lane-body {
    display: stack;
    flex-direction: column;
    gap: 4px;
    flex-grow: 1;
}

.card {
    height: 100px;
}
)";

Component MakeLane(int CardCount) {
    std::vector<Component> Cards;
    for (int Index = 0; Index < CardCount; ++Index) {
        Cards.push_back(MakeNode(IrisElementTag::Frame, WithClass("card")));
    }
    IrisProps BodyProps = WithClass("lane-body");
    BodyProps["wheelStep"] = IrisPropValue{50.0f};
    return MakeNode(IrisElementTag::Frame, WithClass("lane"),
                    {MakeNode(IrisElementTag::Frame, WithClass("lane-header")),
                     MakeNode(IrisElementTag::Scroll, BodyProps, std::move(Cards))});
}

void TestAFlexGrowScrollGetsTheHeightItsSiblingsLeave() {
    StyledBuild Build(kLaneSheet, MakeLane(10));
    auto* Lane = dynamic_cast<Box*>(Build.Root.get());
    if (Lane == nullptr || Lane->Children.size() != 2) {
        Expect(false, "the lane fixture builds a Box with a header and a body");
        return;
    }

    Lane->Measure({300.0f, 400.0f});
    Lane->Arrange({0.0f, 0.0f, 300.0f, 400.0f});

    const Penumbra::Rect Body = Lane->Children[1]->GetArrangedRect();
    Expect(Near(Body.Y, 48.0f), "the flex-grow body starts below the header and the gap");
    Expect(Near(Body.H, 342.0f), "the flex-grow body's height is the lane's content height minus the header and gap");
    Expect(Near(Body.Y + Body.H, 390.0f), "the flex-grow body ends at the lane's bottom padding edge");
}

void TestTheLastCardOfAFlexGrowScrollCanBeScrolledFullyIntoView() {
    StyledBuild Build(kLaneSheet, MakeLane(10));
    auto* Lane = dynamic_cast<Box*>(Build.Root.get());
    auto* Body = Lane != nullptr && Lane->Children.size() == 2 ? dynamic_cast<ScrollablePanel*>(Lane->Children[1].get())
                                                               : nullptr;
    if (Body == nullptr) {
        Expect(false, "the lane fixture's body builds a ScrollablePanel");
        return;
    }

    const Penumbra::Rect Window{0.0f, 0.0f, 300.0f, 400.0f};
    Lane->Measure({Window.W, Window.H});
    Lane->Arrange(Window);

    Penumbra::Platform::InputState Input;
    Input.MousePosition = {150.0f, 200.0f};
    Input.MouseWheelDelta = -1000.0f;
    Body->UpdateInteractionState(Input);
    Lane->Measure({Window.W, Window.H});
    Lane->Arrange(Window);

    const Penumbra::Rect Viewport = Body->GetArrangedRect();
    const Penumbra::Rect LastCard = Body->Children.back()->GetArrangedRect();
    Expect(Near(LastCard.Y + LastCard.H, Viewport.Y + Viewport.H),
           "scrolled to the end, the last card's bottom edge meets the scroll viewport's bottom edge");
    Expect(Viewport.Y + Viewport.H <= Window.H, "the scroll viewport ends inside the window, not below it");
}

void TestFlexGrowSplitsTheRemainingWidthByWeight() {
    const char* Sheet = R"(
.row {
    display: stack;
    flex-direction: row;
    gap: 10px;
}

.fixed {
    width: 90px;
}

.one {
    flex-grow: 1;
}

.three {
    flex-grow: 3;
}
)";
    const Component Row = MakeNode(IrisElementTag::Frame, WithClass("row"),
                                   {MakeNode(IrisElementTag::Frame, WithClass("fixed")),
                                    MakeNode(IrisElementTag::Frame, WithClass("one")),
                                    MakeNode(IrisElementTag::Frame, WithClass("three"))});
    StyledBuild Build(Sheet, Row);
    auto* AsBox = dynamic_cast<Box*>(Build.Root.get());
    if (AsBox == nullptr || AsBox->Children.size() != 3) {
        Expect(false, "the row fixture builds a Box with three children");
        return;
    }

    AsBox->Measure({510.0f, 50.0f});
    AsBox->Arrange({0.0f, 0.0f, 510.0f, 50.0f});

    const Penumbra::Rect One = AsBox->Children[1]->GetArrangedRect();
    const Penumbra::Rect Three = AsBox->Children[2]->GetArrangedRect();
    Expect(Near(One.X, 100.0f) && Near(One.W, 100.0f), "flex-grow: 1 takes a quarter of the 400px left over");
    Expect(Near(Three.X, 210.0f) && Near(Three.W, 300.0f), "flex-grow: 3 takes three quarters of the 400px left over");
}

void TestAStackWithoutFlexGrowStillPacksChildrenAtTheirOwnSize() {
    const char* Sheet = R"(
.column {
    display: stack;
    flex-direction: column;
    justify-content: end;
}

.item {
    height: 40px;
}
)";
    const Component Column = MakeNode(IrisElementTag::Frame, WithClass("column"),
                                      {MakeNode(IrisElementTag::Frame, WithClass("item"))});
    StyledBuild Build(Sheet, Column);
    auto* AsBox = dynamic_cast<Box*>(Build.Root.get());
    if (AsBox == nullptr || AsBox->Children.size() != 1) {
        Expect(false, "the column fixture builds a Box with one child");
        return;
    }

    AsBox->Measure({100.0f, 200.0f});
    AsBox->Arrange({0.0f, 0.0f, 100.0f, 200.0f});

    const Penumbra::Rect Item = AsBox->Children[0]->GetArrangedRect();
    Expect(Near(Item.Y, 160.0f) && Near(Item.H, 40.0f), "justify-content still applies when no child sets flex-grow");
}

void TestAScrollHonoursAnExplicitWidthAndHeight() {
    const char* Sheet = R"(
.viewport {
    width: 320px;
    height: 200px;
}

.tall {
    height: 900px;
}
)";
    StyledBuild Empty(Sheet, MakeNode(IrisElementTag::Scroll, WithClass("viewport")));
    const Penumbra::Point EmptySize = Empty.Root->Measure({800.0f, 600.0f});
    Expect(Near(EmptySize.X, 320.0f) && Near(EmptySize.Y, 200.0f),
           "an empty <Scroll> with width/height measures to exactly that size");

    StyledBuild Overflowing(Sheet, MakeNode(IrisElementTag::Scroll, WithClass("viewport"),
                                            {MakeNode(IrisElementTag::Frame, WithClass("tall"))}));
    const Penumbra::Point OverflowingSize = Overflowing.Root->Measure({800.0f, 600.0f});
    Expect(Near(OverflowingSize.X, 320.0f) && Near(OverflowingSize.Y, 200.0f),
           "an overflowing <Scroll> with width/height still measures to exactly that size");
}

void TestScrollbarPropertiesReachTheScrollablePanel() {
    const char* Sheet = R"(
.body {
    scrollbar-width: 6px;
    scrollbar-color: #3A3A48 #20202880;
}

.body:hover {
    scrollbar-color: #4C5287;
}
)";
    StyledBuild Build(Sheet, MakeNode(IrisElementTag::Scroll, WithClass("body")));
    const auto* Scroll = dynamic_cast<ScrollablePanel*>(Build.Root.get());
    if (Scroll == nullptr) {
        Expect(false, "<Scroll> builds a ScrollablePanel");
        return;
    }
    Expect(Near(Scroll->ScrollbarWidthLogical, 6.0f), "scrollbar-width reaches ScrollablePanel::ScrollbarWidthLogical");
    Expect(Scroll->ColorScrollbarThumb.R == 0x3A && Scroll->ColorScrollbarThumb.B == 0x48 &&
               Scroll->ColorScrollbarThumb.A == 0xFF,
           "scrollbar-color's first colour reaches ColorScrollbarThumb");
    Expect(Scroll->ColorScrollbarTrack.R == 0x20 && Scroll->ColorScrollbarTrack.A == 0x80,
           "scrollbar-color's second colour reaches ColorScrollbarTrack");
    Expect(Scroll->ColorScrollbarThumbHovered.R == 0x4C && Scroll->ColorScrollbarThumbHovered.B == 0x87,
           "a :hover scrollbar-color reaches ColorScrollbarThumbHovered");
}

void TestTextAreaTakesItsColoursAndScrollbarFromLustre() {
    const char* Sheet = R"(
.editor {
    color: #ECECF1;
    height: 240px;
    scrollbar-width: 4px;
    scrollbar-color: #3A3A48;
}
)";
    StyledBuild Build(Sheet, MakeNode(IrisElementTag::TextArea, WithClass("editor")));
    auto* Editor = dynamic_cast<TextArea*>(Build.Root.get());
    if (Editor == nullptr) {
        Expect(false, "<TextArea> builds a TextArea");
        return;
    }
    Expect(Editor->ColorText.R == 0xEC && Editor->ColorCaret.R == 0xEC && Editor->ColorSelection.A == 0x55,
           "color reaches TextArea's text, caret and selection colours");
    Expect(Editor->CaretWidthLogical > 0.0f, "a styled TextArea gets a visible caret width");
    Expect(Near(Editor->ScrollbarWidthLogical, 4.0f) && Editor->ColorScrollbarThumb.R == 0x3A,
           "scrollbar-width and scrollbar-color reach TextArea's own scrollbar fields");
    const Penumbra::Point Size = Editor->Measure({800.0f, 600.0f});
    Expect(Near(Size.Y, 240.0f), "height sizes the TextArea");
}

} // namespace

void RunStackFillAndScrollbarTests() {
    TestAFlexGrowScrollGetsTheHeightItsSiblingsLeave();
    TestTheLastCardOfAFlexGrowScrollCanBeScrolledFullyIntoView();
    TestFlexGrowSplitsTheRemainingWidthByWeight();
    TestAStackWithoutFlexGrowStillPacksChildrenAtTheirOwnSize();
    TestAScrollHonoursAnExplicitWidthAndHeight();
    TestScrollbarPropertiesReachTheScrollablePanel();
    TestTextAreaTakesItsColoursAndScrollbarFromLustre();
}
