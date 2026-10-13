#include "PenumbraUiBackend/ChartNodes.h"
#include "PenumbraUiBackend/IrisApplication.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Iris/IrisConfig.h"
#include "Iris/IrisNyxDriver.h"
#include "Iris/Reconciler.h"
#include "Iris/Signal.h"
#include "Iris/SlotResolution.h"
#include "Lustre/Parser.h"

#include "Penumbra/Render/Renderer.h"
#include "Penumbra/Widgets/LayoutPass.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
using Penumbra::Widgets::BarChart;
using Penumbra::Widgets::BarOrientation;
using Penumbra::Widgets::DonutChart;
using Penumbra::Widgets::WidgetBase;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::Lustre::LustreStyleApplier;

Component MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

::Lustre::Stylesheet ParseOrDie(const std::string& Source) {
    ::Lustre::Parser      Parser(Source, "Charts.lustre");
    ::Lustre::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "the chart test stylesheet parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Lustre::Stylesheet{};
}

const char* const kChartSheet = R"(
.weekly {
    height: 120px;
    color: #888888;
    axis-color: #999999;
    axis-width: 1px;
    tick-spacing: 28px;
    bar-gap: 0.3;
    appear-duration: 0.6s;
    change-duration: 0.4s;
}
.done { color: #6193FF; }
.added { color: #F0A046; }
)";

// Lays a chart out (which is when it copies its data in from its children).
void LayOut(WidgetBase& Widget) {
    const Penumbra::Widgets::LayoutPass Pass;
    Widget.Measure({300.0F, 200.0F});
    Widget.Arrange({0.0F, 0.0F, 300.0F, 200.0F});
}

void TestABarChartTakesItsDataFromSeriesChildrenAndItsLookFromLustre() {
    const ::Lustre::Stylesheet    Sheet = ParseOrDie(kChartSheet);
    const LustreStyleApplier      Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &Sheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    IrisProps ChartProps;
    ChartProps["class"]       = IrisPropValue{std::string("weekly")};
    ChartProps["labels"]      = IrisPropValue(std::vector<std::string>({"Mon", "Tue", "Wed"}));
    ChartProps["orientation"] = IrisPropValue{std::string("horizontal")};
    ChartProps["stacked"]     = IrisPropValue{true};
    ChartProps["max"]         = IrisPropValue{20.0F};
    IrisProps Done;
    Done["class"]  = IrisPropValue{std::string("done")};
    Done["name"]   = IrisPropValue{std::string("Done")};
    Done["values"] = IrisPropValue(std::vector<float>({3.0F, 5.0F, 2.0F}));

    auto  Built = BuildWidgetTree(MakeNode(IrisElementTag::BarChart, ChartProps, {MakeNode(IrisElementTag::Series, Done)}),
                                  Context);
    auto* Chart = dynamic_cast<BarChart*>(Built.get());
    if (Chart == nullptr) {
        Expect(false, "<BarChart> builds a Penumbra BarChart");
        return;
    }
    LayOut(*Chart);

    Expect(Chart->Labels == std::vector<std::string>({"Mon", "Tue", "Wed"}) &&
               Chart->Orientation == BarOrientation::Horizontal && Chart->Stacked && Chart->MaxValue == 20.0F,
           "the chart's own props reach BarChart: labels, orientation, stacked, max");
    Expect(Chart->Series.size() == 1 && Chart->Series[0].Name == "Done" &&
               Chart->Series[0].Values == std::vector<float>({3.0F, 5.0F, 2.0F}),
           "its data comes from the <Series> child");
    Expect(!Chart->Series.empty() && Chart->Series[0].SeriesColor.R == 0x61 && Chart->Series[0].SeriesColor.B == 0xFF,
           "the series' colour is its own Lustre class's `color`");
    const Penumbra::Widgets::ChartStyle& Look = Chart->ChartVisualStyle;
    Expect(Look.ColorLabel.R == 0x88 && Look.ColorAxis.R == 0x99 && Look.AxisThicknessLogical == 1.0F &&
               Look.TickSpacingLogical == 28.0F && Look.BarGapFraction == 0.3F && Look.AppearSeconds == 0.6F &&
               Look.TransitionSeconds == 0.4F,
           "the chart properties reach ChartStyle, with `color` for its labels");
    Expect(Chart->Style.Height.IsSet() && Chart->Style.Height.Value == 120.0F,
           "and its box style reaches it like any Box's");
}

void TestASeriesFollowsItsValuesThroughTheReconciler() {
    const ::Lustre::Stylesheet    Sheet = ParseOrDie(kChartSheet);
    const LustreStyleApplier      Applier;
    const ::Lustre::StylesheetSet Sheets{nullptr, &Sheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;
    const iris::MountFn Mount = PenumbraUiBackend::MakeMountFn(Context);

    iris::Signal<float> Today = 3.0F;
    IrisProps           ChartProps;
    ChartProps["class"]  = IrisPropValue{std::string("weekly")};
    ChartProps["labels"] = IrisPropValue(std::vector<std::string>({"Mon", "Tue"}));
    const Component RootNode = MakeNode(
        IrisElementTag::BarChart, ChartProps,
        {Component(IrisElementTag::Slot, {}, {}, Iris::MakeSlotCallable([&]() -> Component {
                       IrisProps Series;
                       Series["class"]  = IrisPropValue{std::string("added")};
                       Series["values"] = IrisPropValue(std::vector<float>({1.0F, Today.get()}));
                       return MakeNode(IrisElementTag::Series, Series);
                   }))});
    std::unique_ptr<Umbra::IWidget> Root  = Mount(RootNode);
    auto                            Slots = iris::ResolveSlots(*Root, RootNode, Mount);
    auto* Chart = dynamic_cast<BarChart*>(static_cast<PenumbraWidget*>(Root.get())->RawWidget());
    if (Chart == nullptr) {
        Expect(false, "a <BarChart> with a <Slot> series mounts through MakeMountFn");
        return;
    }
    LayOut(*Chart);
    Expect(Chart->Series.size() == 1 && Chart->Series[0].Values == std::vector<float>({1.0F, 3.0F}) &&
               Chart->Series[0].SeriesColor.R == 0xF0,
           "a <Slot>-mounted <Series> becomes the chart's data, coloured by its class");

    Today.set(8.0F);
    iris::Tick();
    LayOut(*Chart);
    Expect(Chart->Series.size() == 1 && Chart->Series[0].Values == std::vector<float>({1.0F, 8.0F}),
           "a Signal change re-renders the Slot and the reconciler's diff carries the series' new values");
}

class TempProject {
public:
    TempProject() {
        Root_ = std::filesystem::temp_directory_path() / "penumbra_ui_backend_chart_test";
        std::filesystem::remove_all(Root_);
        std::filesystem::create_directories(Root_ / "demo");
    }
    ~TempProject() { std::filesystem::remove_all(Root_); }

    void Write(const std::string& Name, std::string_view Source) { std::ofstream(Root_ / "demo" / Name) << Source; }

    std::string RootPath() const { return Root_.string(); }
    std::string UiDir() const { return (Root_ / "demo").string(); }

private:
    std::filesystem::path Root_;
};

// The whole path a Nyx-authored app takes: a real .irisx with Nyx arrays, a .lustre beside
// it, mounted through IrisApplication, then drawn.
void TestAnIrisxChartMountsFromNyxArraysAndDraws() {
    TempProject Project;
    Project.Write("Stats.lustre", std::string(kChartSheet) + R"(
.page { display: stack; flex-direction: column; }
.status { donut-thickness: 18px; segment-gap: 3deg; }
.todo { color: #F0A046; }
.doing { color: #6193FF; }
)");
    Project.Write("Stats.irisx", "void Stats() {\n"
                                 "    Array<string> days = [\"Mon\", \"Tue\", \"Wed\"];\n"
                                 "    Array<int> done = [3, 5, 2];\n"
                                 "    render {\n"
                                 "        <Frame class=\"page\">\n"
                                 "            <BarChart ref=\"weekly\" class=\"weekly\" labels={days}>\n"
                                 "                <Series name=\"Done\" values={done} class=\"done\" />\n"
                                 "                <Series name=\"Added\" values={[4, 2, 6]} class=\"added\" />\n"
                                 "            </BarChart>\n"
                                 "            <DonutChart ref=\"status\" class=\"status\" centre={12} caption=\"tasks\">\n"
                                 "                <Slice label=\"Todo\" value={7} class=\"todo\" />\n"
                                 "                <Slice label=\"Doing\" value={5} class=\"doing\" />\n"
                                 "            </DonutChart>\n"
                                 "        </Frame>\n"
                                 "    }\n"
                                 "}\n");

    Iris::IrisConfig Config;
    Config.Target      = Iris::IrisBuildTarget::UmbraEngine;
    Config.SearchPaths = {"demo"};
    Iris::IrisNyxDriver            Driver(Config, Project.RootPath());
    PenumbraUiBackend::IrisApplication App;
    App.Attach(Driver, Project.UiDir());
    Expect(App.MountAppRoot("Stats.irisx", "Stats") && Driver.Errors().empty(),
           "a .irisx with <BarChart>/<DonutChart>, Nyx arrays and Series/Slice children mounts with no errors");

    auto* Weekly = dynamic_cast<BarChart*>(App.GetRef("weekly"));
    auto* Status = dynamic_cast<DonutChart*>(App.GetRef("status"));
    if (Weekly == nullptr || Status == nullptr || App.GetRootWidget() == nullptr) {
        Expect(false, "the charts are reachable by ref as Penumbra charts");
        return;
    }
    LayOut(*App.GetRootWidget());
    if (Weekly->Series.size() != 2 || Status->Slices.size() != 2) {
        Expect(false, "the bar chart has both series and the donut both slices");
        return;
    }
    Expect(Weekly->Labels.size() == 3 && Weekly->Series.size() == 2 &&
               Weekly->Series[0].Values == std::vector<float>({3.0F, 5.0F, 2.0F}) &&
               Weekly->Series[1].Values == std::vector<float>({4.0F, 2.0F, 6.0F}),
           "the bar chart's labels and both series come from the Nyx arrays");
    Expect(Weekly->Series[1].SeriesColor.R == 0xF0 && Weekly->ChartVisualStyle.TickSpacingLogical == 28.0F,
           "and its colours and chart properties from the .lustre beside it");
    Expect(Status->Slices.size() == 2 && Status->Slices[0].Value == 7.0F && Status->Slices[1].SliceColor.R == 0x61 &&
               Status->CentreText == "12" && Status->CentreCaption == "tasks" &&
               Status->ChartVisualStyle.DonutThicknessLogical == 18.0F,
           "the donut's slices, centre text and thickness come through the same way");

    SDL_Surface* Surface = SDL_CreateSurface(300, 200, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer* Sdl    = Surface ? SDL_CreateSoftwareRenderer(Surface) : nullptr;
    Penumbra::Render::Renderer Renderer;
    if (Sdl != nullptr && Renderer.Initialise(Sdl, 1.0F, nullptr)) {
        for (int Frame = 0; Frame < 3; ++Frame) {
            LayOut(*App.GetRootWidget());
            Renderer.BeginFrame({0, 0, 0, 255}, 0.1F);
            App.GetRootWidget()->Draw(Renderer);
            Renderer.EndFrameAndPresent();
        }
        Expect(true, "the mounted charts lay out and draw for several frames");
        SDL_DestroyRenderer(Sdl);
    }
    if (Surface) SDL_DestroySurface(Surface);
    App.TeardownRootWidget();
}

} // namespace

void RunChartTests() {
    TestABarChartTakesItsDataFromSeriesChildrenAndItsLookFromLustre();
    TestASeriesFollowsItsValuesThroughTheReconciler();
    TestAnIrisxChartMountsFromNyxArraysAndDraws();
}
