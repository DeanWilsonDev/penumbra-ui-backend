#include "PenumbraUiBackend/Cosmos/StyleApplier.h"
#include "PenumbraUiBackend/Walker.h"

#include "cosmos/Parser.h"

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Length.h"

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

using Chaos::Component;
using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Chaos::ChaosPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::Cosmos::CosmosStyleApplier;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::WidgetBase;

Component MakeNode(ChaosElementTag Tag, ChaosProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

ChaosProps WithClass(const std::string& ClassName) {
    ChaosProps Props;
    Props["class"] = ChaosPropValue{ClassName};
    return Props;
}

::Cosmos::Stylesheet ParseOrDie(const std::string& Source) {
    ::Cosmos::Parser      Parser(Source, "RelativeSizing.cosmos");
    ::Cosmos::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "relative-sizing fixture stylesheet parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Cosmos::Stylesheet{};
}

struct StyledBuild {
    ::Cosmos::Stylesheet        Sheet;
    ::Cosmos::StylesheetSet     Sheets;
    CosmosStyleApplier          Applier;
    BuildContext                Context;
    std::unique_ptr<WidgetBase> Root;

    StyledBuild(const std::string& Source, const Component& Node) : Sheet(ParseOrDie(Source)), Sheets{nullptr, &Sheet} {
        Context.Style = &Sheets;
        Context.StyleApplier = &Applier;
        Root = BuildWidgetTree(Node, Context);
    }

    Penumbra::Rect LayOut(float Width, float Height) {
        Root->Measure({Width, Height});
        Root->Arrange({0.0f, 0.0f, Width, Height});
        return Root->GetArrangedRect();
    }

    Penumbra::Rect ChildRect(std::size_t Index) const {
        auto* AsBox = dynamic_cast<Box*>(Root.get());
        return AsBox != nullptr && Index < AsBox->Children.size() ? AsBox->Children[Index]->GetArrangedRect()
                                                                  : Penumbra::Rect{};
    }
};

const char* kDialogSheet = R"(
.overlay {
    display: stack;
    flex-direction: column;
    align-items: center;
    padding: 40px;
}

.panel {
    width: 600px;
    max-width: 100%;
    min-width: 200px;
    height: 100px;
}
)";

Component MakeDialog() {
    return MakeNode(ChaosElementTag::Frame, WithClass("overlay"), {MakeNode(ChaosElementTag::Frame, WithClass("panel"))});
}

void TestAPanelKeepsItsWidthWhenTheWindowIsWideEnough() {
    StyledBuild Build(kDialogSheet, MakeDialog());
    Build.LayOut(1280.0f, 720.0f);
    const Penumbra::Rect Panel = Build.ChildRect(0);
    Expect(Near(Panel.W, 600.0f) && Near(Panel.X, 340.0f), "width: 600px stays 600px, centred, in a wide window");
}

void TestMaxWidthPercentShrinksAPanelToItsParent() {
    StyledBuild Build(kDialogSheet, MakeDialog());
    Build.LayOut(500.0f, 400.0f);
    const Penumbra::Rect Panel = Build.ChildRect(0);
    Expect(Near(Panel.W, 420.0f) && Near(Panel.X, 40.0f),
           "max-width: 100% caps the panel at the overlay's content width (500 - 2 x 40)");
}

void TestMinWidthWinsOverMaxWidth() {
    StyledBuild Build(kDialogSheet, MakeDialog());
    Build.LayOut(200.0f, 400.0f);
    Expect(Near(Build.ChildRect(0).W, 200.0f), "min-width: 200px wins over max-width: 100% once the parent is narrower");
}

void TestMaxWidthClampsAnExplicitPixelWidth() {
    StyledBuild Build(".box { width: 600px; max-width: 300px; height: 10px; }",
                      MakeNode(ChaosElementTag::Frame, WithClass("box")));
    const Penumbra::Point Size = Build.Root->Measure({1000.0f, 1000.0f});
    Expect(Near(Size.X, 300.0f), "max-width clamps an explicit width, as in CSS");
}

void TestPercentWidthResolvesAgainstTheParentsContentBox() {
    const char* Sheet = R"(
.parent {
    width: 400px;
    padding: 10px;
    display: stack;
    flex-direction: column;
}

.child {
    width: 50%;
    height: 10px;
}
)";
    StyledBuild Build(Sheet, MakeNode(ChaosElementTag::Frame, WithClass("parent"),
                                      {MakeNode(ChaosElementTag::Frame, WithClass("child"))}));
    Build.LayOut(400.0f, 1000.0f);
    Expect(Near(Build.ChildRect(0).W, 190.0f), "width: 50% is half of the parent's 380px content box");
}

void TestViewportUnitsResolveAgainstTheLayoutViewport() {
    Penumbra::Widgets::SetLayoutViewportLogical({1000.0f, 800.0f});
    StyledBuild Build(".box { width: 30vw; height: 25vh; }", MakeNode(ChaosElementTag::Frame, WithClass("box")));
    const Penumbra::Point Size = Build.Root->Measure({100.0f, 100.0f});
    Penumbra::Widgets::SetLayoutViewportLogical({0.0f, 0.0f});
    Expect(Near(Size.X, 300.0f) && Near(Size.Y, 200.0f),
           "vw/vh resolve against the layout viewport, not the space the parent offers");
}

void TestAFlexGrowInputFillsTheRowBesideItsButton() {
    const char* Sheet = R"(
.row {
    width: 400px;
    display: stack;
    flex-direction: row;
    gap: 10px;
}

.field {
    flex-grow: 1;
}

.button {
    width: 90px;
    height: 20px;
}
)";
    StyledBuild Build(Sheet, MakeNode(ChaosElementTag::Frame, WithClass("row"),
                                      {MakeNode(ChaosElementTag::Input, WithClass("field")),
                                       MakeNode(ChaosElementTag::Frame, WithClass("button"))}));
    Build.LayOut(400.0f, 100.0f);
    Expect(Near(Build.ChildRect(0).W, 300.0f) && Near(Build.ChildRect(1).X, 310.0f),
           "an <Input> with flex-grow: 1 takes the row's width left beside its button");
}

} // namespace

void RunRelativeSizingTests() {
    TestAPanelKeepsItsWidthWhenTheWindowIsWideEnough();
    TestMaxWidthPercentShrinksAPanelToItsParent();
    TestMinWidthWinsOverMaxWidth();
    TestMaxWidthClampsAnExplicitPixelWidth();
    TestPercentWidthResolvesAgainstTheParentsContentBox();
    TestViewportUnitsResolveAgainstTheLayoutViewport();
    TestAFlexGrowInputFillsTheRowBesideItsButton();
}
