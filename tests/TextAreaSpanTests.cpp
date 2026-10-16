#include "PenumbraUiBackend/Cosmos/StyleApplier.h"
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Chaos/Component.h"
#include "Cosmos/Parser.h"
#include "Cosmos/Resolver.h"
#include "Umbra/IWidget.h"

#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/TextArea.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
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

using Chaos::ChaosElementTag;
using Chaos::ChaosProps;
using Chaos::ChaosPropValue;
using Chaos::Component;
using Penumbra::Widgets::TextArea;
using Penumbra::Widgets::TextAreaSpan;
using Penumbra::Widgets::WidgetBase;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::PenumbraWidget;
using PenumbraUiBackend::WrapExistingTree;
using PenumbraUiBackend::Cosmos::CosmosStyleApplier;

class SizeRecordingFontBackend : public Penumbra::Render::IFontBackend {
public:
    std::vector<float> LoadedSizes;

    Penumbra::Render::FontHandle LoadFont(const char* Path, float PointSizeLogical, float DpiScaleFactor) override {
        return LoadStyledFont(Path, PointSizeLogical, DpiScaleFactor, {});
    }
    Penumbra::Render::FontHandle LoadStyledFont(const char*, float PointSizeLogical, float,
                                                Penumbra::Render::FontStyle) override {
        LoadedSizes.push_back(PointSizeLogical);
        return static_cast<Penumbra::Render::FontHandle>(LoadedSizes.size());
    }
    Penumbra::Render::TextMetrics MeasureText(Penumbra::Render::FontHandle, std::string_view) const override {
        return {0.0F, 0.0F, 0.0F};
    }
    float MeasureTextWidth(Penumbra::Render::FontHandle, std::string_view) const override { return 0.0F; }
    SDL_Texture* AcquireTextTexture(SDL_Renderer*, Penumbra::Render::FontHandle, std::string_view, SDL_Color) override {
        return nullptr;
    }

    float SizeOf(Penumbra::Render::FontHandle Handle) const {
        return Handle >= 1 && Handle <= LoadedSizes.size() ? LoadedSizes[Handle - 1] : 0.0F;
    }
};

const char* kEditorSheet = R"(
.editor {
    color: #ECECF1;
    .marker {
        color: #6E6E7C;
    }
}

.editor-quiet {
    color: #ECECF1;
    .marker {
        color: #3A3A48;
    }
}

.h1 {
    color: #F5F5FA;
    font-size: 22px;
}

.code {
    background-color: #26262E;
}
)";

::Cosmos::Stylesheet ParseOrDie(const std::string& Source) {
    ::Cosmos::Parser      Parser(Source, "TextAreaSpans.cosmos");
    ::Cosmos::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "the span fixture stylesheet parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Cosmos::Stylesheet{};
}

Component MakeEditor(const std::string& ClassName, const std::string& Text, std::vector<float> Ranges,
                     std::vector<std::string> Classes) {
    ChaosProps Props;
    Props["class"] = ChaosPropValue{ClassName};
    Props["text"] = ChaosPropValue{Text};
    Props["spanRanges"] = ChaosPropValue{std::move(Ranges)};
    Props["spanClasses"] = ChaosPropValue{std::move(Classes)};
    return Component(ChaosElementTag::TextArea, std::move(Props), {}, nullptr);
}

struct EditorBuild {
    SizeRecordingFontBackend         Fonts;
    ::Cosmos::Stylesheet             Sheet;
    ::Cosmos::StylesheetSet          Sheets;
    CosmosStyleApplier               Applier;
    std::unique_ptr<PenumbraWidget>  Wrapper;
    TextArea*                        Editor{nullptr};

    explicit EditorBuild(const Component& Node)
        : Sheet(ParseOrDie(kEditorSheet)), Sheets{nullptr, &Sheet}, Applier(&Fonts) {
        Applier.SetDefaultFont({"/fonts/Body.ttf", 14.0F});
        BuildContext Context;
        Context.Style = &Sheets;
        Context.StyleApplier = &Applier;
        std::unique_ptr<WidgetBase> Root = BuildWidgetTree(Node, Context);
        Editor = dynamic_cast<TextArea*>(Root.get());
        Wrapper = WrapExistingTree(std::move(Root), nullptr, nullptr, &Sheets, &Applier);
    }
};

bool SpanIs(const TextAreaSpan& Span, std::size_t Start, std::size_t End, const std::string& ClassName) {
    return Span.Start == Start && Span.End == End && Span.ClassName == ClassName;
}

void TestSpanPropsBuildSpansStyledFromTheirClasses() {
    EditorBuild Build(MakeEditor("editor", "# Hi `x`", {0, 2, 2, 4, 5, 8}, {"h1 marker", "h1", "code"}));
    if (Build.Editor == nullptr) {
        Expect(false, "<TextArea> with span props builds a TextArea");
        return;
    }
    const std::vector<TextAreaSpan>& Spans = Build.Editor->GetSpans();
    if (Spans.size() != 3) {
        Expect(false, "spanRanges and spanClasses build one span per range pair");
        return;
    }
    Expect(SpanIs(Spans[0], 0, 2, "h1 marker") && SpanIs(Spans[1], 2, 4, "h1") && SpanIs(Spans[2], 5, 8, "code"),
           "each span takes its byte range from spanRanges and its classes from spanClasses");
    Expect(Spans[1].ColorText && Spans[1].ColorText->R == 0xF5, "a span's color comes from its class");
    Expect(Spans[0].ColorText && Spans[0].ColorText->R == 0x6E,
           "a class nested under the field's own class beats a top-level class declared after it");
    Expect(Spans[1].Font && Build.Fonts.SizeOf(*Spans[1].Font) == 22.0F, "a span's font-size picks its font");
    Expect(Spans[2].ColorBackground.R == 0x26 && Spans[2].ColorBackground.A == 0xFF,
           "a span's background-color fills its background");
    Expect(Spans[2].ColorText && Spans[2].ColorText->R == 0xEC,
           "a span whose classes set no color inherits the field's");
}

void TestASpanDiffWithOnlyClassesKeepsTheLastSpanRanges() {
    EditorBuild Build(MakeEditor("editor", "# Hi `x`", {0, 2, 2, 4}, {"h1 marker", "h1"}));
    if (Build.Editor == nullptr) {
        Expect(false, "<TextArea> with span props builds a TextArea");
        return;
    }
    Build.Editor->SetSpans({TextAreaSpan{0, 3, "h1 marker"}, TextAreaSpan{3, 5, "h1"}});

    Umbra::ChaosPropDiff Diff;
    Diff.StringLists["spanClasses"] = {"marker", "code"};
    Build.Wrapper->ApplyPropDiff(Diff);

    const std::vector<TextAreaSpan>& Spans = Build.Editor->GetSpans();
    Expect(Spans.size() == 2 && SpanIs(Spans[0], 0, 2, "marker") && SpanIs(Spans[1], 2, 4, "code"),
           "a diff carrying only spanClasses pairs them with the spanRanges last sent, not the field's shifted spans");
    Expect(Spans.size() == 2 && Spans[1].ColorBackground.R == 0x26, "spans from a diff are styled from their classes");
}

void TestRestylingTheFieldRestylesItsSpansWhereTheyAre() {
    EditorBuild Build(MakeEditor("editor", "# Hi `x`", {0, 2, 2, 4}, {"h1 marker", "h1"}));
    if (Build.Editor == nullptr) {
        Expect(false, "<TextArea> with span props builds a TextArea");
        return;
    }
    Build.Editor->SetSpans({TextAreaSpan{0, 3, "h1 marker"}, TextAreaSpan{3, 5, "h1"}});

    Umbra::ChaosPropDiff Diff;
    Diff.ClassName = "editor-quiet";
    Build.Wrapper->ApplyPropDiff(Diff);

    const std::vector<TextAreaSpan>& Spans = Build.Editor->GetSpans();
    Expect(Spans.size() == 2 && SpanIs(Spans[0], 0, 3, "h1 marker") && SpanIs(Spans[1], 3, 5, "h1"),
           "a field class change keeps its spans where local edits moved them");
    Expect(Spans.size() == 2 && Spans[0].ColorText && Spans[0].ColorText->R == 0x3A,
           "and re-resolves each span under the field's new class");
}

} // namespace

void RunTextAreaSpanTests() {
    TestSpanPropsBuildSpansStyledFromTheirClasses();
    TestASpanDiffWithOnlyClassesKeepsTheLastSpanRanges();
    TestRestylingTheFieldRestylesItsSpansWhereTheyAre();
}
