#pragma once

#include "Penumbra/Widgets/BarChart.h"
#include "Penumbra/Widgets/DonutChart.h"
#include "Penumbra/Widgets/LineChart.h"
#include "Penumbra/Widgets/WidgetBase.h"

#include <string>
#include <vector>

namespace PenumbraUiBackend {

// Chaos's chart primitives (`Chaos/ChaosElementTag.h`'s BarChart..Slice) on Penumbra. A
// chart's data is its children: one `<Series>` per series of a bar or line chart, one
// `<Slice>` per slice of a donut. Those build to the two leaf nodes below, which draw
// nothing and only hold their data and colour (Cosmos's `color`, applied by
// CosmosStyleApplier). The charts are Penumbra's own, extended to copy that data in from
// their children at the start of every layout -- so whatever the reconciler does to the
// children (a `<Slot>`'s list of series growing, a series' values changing) reaches the
// chart without anything else keeping the two in step. Header-only on purpose: the
// Cosmos bridge target needs the node types too, and it doesn't link `chaos`.

namespace Detail {

class ChartDataLeaf : public Penumbra::Widgets::WidgetBase {
public:
    Penumbra::Point Measure(Penumbra::Point) override { return {}; }
    void            Arrange(Penumbra::Rect FinalRectLogical) override { ArrangedRect = FinalRectLogical; }
    bool            UpdateInteractionState(const Penumbra::Platform::InputState&) override { return false; }
    void            Draw(Penumbra::Render::Renderer&) override {}
};

} // namespace Detail

// A `<Series name values>`.
class SeriesNode final : public Detail::ChartDataLeaf {
public:
    std::string             Name;
    std::vector<float>      Values;
    Penumbra::Render::Color SeriesColor{0, 0, 0, 0};
};

// A `<Slice label value>`.
class SliceNode final : public Detail::ChartDataLeaf {
public:
    std::string             Label;
    float                   Value{0.0f};
    Penumbra::Render::Color SliceColor{0, 0, 0, 0};
};

namespace Detail {

inline std::vector<Penumbra::Widgets::ChartSeries> SeriesFromChildren(const Penumbra::Widgets::Box& Chart) {
    std::vector<Penumbra::Widgets::ChartSeries> Series;
    for (const auto& Child : Chart.Children) {
        if (const auto* Node = dynamic_cast<const SeriesNode*>(Child.get()); Node && Node->GetIsVisible()) {
            Series.push_back({Node->Name, Node->Values, Node->SeriesColor});
        }
    }
    return Series;
}

} // namespace Detail

class DataBarChart final : public Penumbra::Widgets::BarChart {
public:
    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override {
        Series = Detail::SeriesFromChildren(*this);
        return BarChart::Measure(AvailableSizeLogical);
    }
};

class DataLineChart final : public Penumbra::Widgets::LineChart {
public:
    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override {
        Series = Detail::SeriesFromChildren(*this);
        return LineChart::Measure(AvailableSizeLogical);
    }
};

class DataDonutChart final : public Penumbra::Widgets::DonutChart {
public:
    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override {
        Slices.clear();
        for (const auto& Child : Children) {
            if (const auto* Node = dynamic_cast<const SliceNode*>(Child.get()); Node && Node->GetIsVisible()) {
                Slices.push_back({Node->Label, Node->Value, Node->SliceColor});
            }
        }
        return DonutChart::Measure(AvailableSizeLogical);
    }
};

} // namespace PenumbraUiBackend
