#include "PenumbraUiBackend/ChartNodes.h"
#include "PenumbraUiBackend/ChaosApplication.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Chaos/ChaosConfig.h"
#include "Chaos/ChaosNyxDriver.h"
#include "Chaos/Reconciler.h"
#include "Chaos/Signal.h"
#include "Chaos/SlotResolution.h"
#include "Cosmos/Parser.h"

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

using Chaos::Component;
using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Chaos::ChaosPropValue;
using Penumbra::Widgets::BarChart;
using Penumbra::Widgets::BarOrientation;
using Penumbra::Widgets::DonutChart;
using Penumbra::Widgets::WidgetBase;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::Cosmos::CosmosStyleApplier;

Component MakeNode(ChaosElementTag Tag, ChaosProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

::Cosmos::Stylesheet ParseOrDie(const std::string& Source) {
    ::Cosmos::Parser      Parser(Source, "Charts.cosmos");
    ::Cosmos::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "the chart test stylesheet parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Cosmos::Stylesheet{};
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

void TestABarChartTakesItsDataFromSeriesChildrenAndItsLookFromCosmos() {
    const ::Cosmos::Stylesheet    Sheet = ParseOrDie(kChartSheet);
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &Sheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;

    ChaosProps ChartProps;
    ChartProps["class"]       = ChaosPropValue{std::string("weekly")};
    ChartProps["labels"]      = ChaosPropValue(std::vector<std::string>({"Mon", "Tue", "Wed"}));
    ChartProps["orientation"] = ChaosPropValue{std::string("horizontal")};
    ChartProps["stacked"]     = ChaosPropValue{true};
    ChartProps["max"]         = ChaosPropValue{20.0F};
    ChaosProps Done;
    Done["class"]  = ChaosPropValue{std::string("done")};
    Done["name"]   = ChaosPropValue{std::string("Done")};
    Done["values"] = ChaosPropValue(std::vector<float>({3.0F, 5.0F, 2.0F}));

    auto  Built = BuildWidgetTree(MakeNode(ChaosElementTag::BarChart, ChartProps, {MakeNode(ChaosElementTag::Series, Done)}),
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
           "the series' colour is its own Cosmos class's `color`");
    const Penumbra::Widgets::ChartStyle& Look = Chart->ChartVisualStyle;
    Expect(Look.ColorLabel.R == 0x88 && Look.ColorAxis.R == 0x99 && Look.AxisThicknessLogical == 1.0F &&
               Look.TickSpacingLogical == 28.0F && Look.BarGapFraction == 0.3F && Look.AppearSeconds == 0.6F &&
               Look.TransitionSeconds == 0.4F,
           "the chart properties reach ChartStyle, with `color` for its labels");
    Expect(Chart->Style.Height.IsSet() && Chart->Style.Height.Value == 120.0F,
           "and its box style reaches it like any Box's");
}

void TestASeriesFollowsItsValuesThroughTheReconciler() {
    const ::Cosmos::Stylesheet    Sheet = ParseOrDie(kChartSheet);
    const CosmosStyleApplier      Applier;
    const ::Cosmos::StylesheetSet Sheets{nullptr, &Sheet};
    BuildContext                  Context;
    Context.Style        = &Sheets;
    Context.StyleApplier = &Applier;
    const chaos::MountFn Mount = PenumbraUiBackend::MakeMountFn(Context);

    chaos::Signal<float> Today = 3.0F;
    ChaosProps           ChartProps;
    ChartProps["class"]  = ChaosPropValue{std::string("weekly")};
    ChartProps["labels"] = ChaosPropValue(std::vector<std::string>({"Mon", "Tue"}));
    const Component RootNode = MakeNode(
        ChaosElementTag::BarChart, ChartProps,
        {Component(ChaosElementTag::Slot, {}, {}, Chaos::MakeSlotCallable([&]() -> Component {
                       ChaosProps Series;
                       Series["class"]  = ChaosPropValue{std::string("added")};
                       Series["values"] = ChaosPropValue(std::vector<float>({1.0F, Today.get()}));
                       return MakeNode(ChaosElementTag::Series, Series);
                   }))});
    std::unique_ptr<Umbra::IWidget> Root  = Mount(RootNode);
    auto                            Slots = chaos::ResolveSlots(*Root, RootNode, Mount);
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
    chaos::Tick();
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

// The whole path a Nyx-authored app takes: a real .chaos with Nyx arrays, a .cosmos beside
// it, mounted through ChaosApplication, then drawn.
void TestAChaosChartMountsFromNyxArraysAndDraws() {
    TempProject Project;
    Project.Write("Stats.cosmos", std::string(kChartSheet) + R"(
.page { display: stack; flex-direction: column; }
.status { donut-thickness: 18px; segment-gap: 3deg; }
.todo { color: #F0A046; }
.doing { color: #6193FF; }
)");
    Project.Write("Stats.chaos", "Stats :: () -> Penumbra:::Component {\n"
                                 "    days: Array<string> = [\"Mon\", \"Tue\", \"Wed\"];\n"
                                 "    done: Array<f32> = [3.0f, 5.0f, 2.0f];\n"
                                 "    return (\n"
                                 "        <frame class=\"page\">\n"
                                 "            <barChart ref=\"weekly\" class=\"weekly\" labels={days}>\n"
                                 "                <series name=\"Done\" values={done} class=\"done\" />\n"
                                 "                <series name=\"Added\" values={[4.0f, 2.0f, 6.0f]} class=\"added\" />\n"
                                 "            </barChart>\n"
                                 "            <donutChart ref=\"status\" class=\"status\" centre=\"12\" caption=\"tasks\">\n"
                                 "                <slice label=\"Todo\" value={7.0f} class=\"todo\" />\n"
                                 "                <slice label=\"Doing\" value={5.0f} class=\"doing\" />\n"
                                 "            </donutChart>\n"
                                 "        </frame>\n"
                                 "    );\n"
                                 "}\n");

    Chaos::ChaosConfig Config;
    Config.Target      = Chaos::ChaosBuildTarget::UmbraEngine;
    Config.SearchPaths = {"demo"};
    Chaos::ChaosNyxDriver            Driver(Config, Project.RootPath());
    PenumbraUiBackend::ChaosApplication App;
    App.Attach(Driver, Project.UiDir());
    Expect(App.MountAppRoot("Stats.chaos", "Stats") && Driver.Diagnostics().empty(),
           "a .chaos with <BarChart>/<DonutChart>, Nyx arrays and Series/Slice children mounts with no errors");

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
           "and its colours and chart properties from the .cosmos beside it");
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
    TestABarChartTakesItsDataFromSeriesChildrenAndItsLookFromCosmos();
    TestASeriesFollowsItsValuesThroughTheReconciler();
    TestAChaosChartMountsFromNyxArraysAndDraws();
}
