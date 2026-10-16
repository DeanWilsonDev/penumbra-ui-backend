#include "PenumbraUiBackend/Cosmos/StyleApplier.h"
#include "PenumbraUiBackend/Walker.h"

#include "Chaos/Component.h"
#include "Cosmos/Parser.h"
#include "Cosmos/Resolver.h"

#include "Penumbra/Platform/IClipboard.h"
#include "Penumbra/Platform/InputState.h"
#include "Penumbra/Render/IFontBackend.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/FocusState.h"
#include "Penumbra/Widgets/TextSelection.h"

#include <cstdio>
#include <functional>
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
using Penumbra::Platform::InputState;
using Penumbra::Platform::Key;
using Penumbra::Widgets::Box;
using Penumbra::Widgets::FocusState;
using Penumbra::Widgets::TextSelection;
using Penumbra::Widgets::WidgetBase;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::Cosmos::CosmosStyleApplier;

class TenPixelFont : public Penumbra::Render::IFontBackend {
public:
    Penumbra::Render::FontHandle LoadFont(const char*, float, float) override { return 1; }
    Penumbra::Render::TextMetrics MeasureText(Penumbra::Render::FontHandle, std::string_view Text) const override {
        return {10.0F * static_cast<float>(Text.size()), 20.0F, 15.0F};
    }
    float MeasureTextWidth(Penumbra::Render::FontHandle, std::string_view Text) const override {
        return 10.0F * static_cast<float>(Text.size());
    }
    SDL_Texture* AcquireTextTexture(SDL_Renderer*, Penumbra::Render::FontHandle, std::string_view, SDL_Color) override {
        return nullptr;
    }
};

class RecordingClipboard : public Penumbra::Platform::IClipboard {
public:
    std::string Copied;
    void        SetClipboardText(const std::string& Text) override { Copied = Text; }
    std::string GetClipboardText() const override { return Copied; }
};

const char* kBodySheet = R"(
.body {
    display: stack;
    flex-direction: column;
    gap: 10px;
    selection-color: #C8E64C66;
}

.para {
    gap: 4px;
}
)";

Component MakeNode(ChaosElementTag Tag, ChaosProps Props, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

Component MakeText(const std::string& Text) {
    ChaosProps Props;
    Props["text"] = ChaosPropValue{Text};
    return MakeNode(ChaosElementTag::Text, std::move(Props));
}

struct SelectableBody {
    TenPixelFont                Font;
    RecordingClipboard          Clipboard;
    FocusState                  Focus;
    ::Cosmos::Stylesheet        Sheet;
    ::Cosmos::StylesheetSet     Sheets;
    CosmosStyleApplier          Applier;
    std::unique_ptr<WidgetBase> Root;

    SelectableBody() : Sheets{nullptr, &Sheet}, Applier(&Font) {
        ::Cosmos::Parser      Parser(kBodySheet, "Body.cosmos");
        ::Cosmos::ParseResult Parsed = Parser.Parse();
        Expect(Parsed.Errors.empty() && Parsed.Sheet.has_value(), "the selectable body stylesheet parses cleanly");
        if (Parsed.Sheet) {
            Sheet = std::move(*Parsed.Sheet);
        }
        Applier.SetDefaultFont({"/fonts/Body.ttf", 14.0F});

        ChaosProps BodyProps;
        BodyProps["class"] = ChaosPropValue{std::string("body")};
        BodyProps["selectable"] = ChaosPropValue{true};
        ChaosProps ParaProps;
        ParaProps["class"] = ChaosPropValue{std::string("para")};
        ChaosProps LinkProps;
        LinkProps["text"] = ChaosPropValue{std::string("Open")};
        LinkProps["onRelease"] = ChaosPropValue{std::function<void()>([] {})};
        const Component Body = MakeNode(
            ChaosElementTag::Frame, BodyProps,
            {MakeNode(ChaosElementTag::Inline, ParaProps, {MakeText("Hello"), MakeText("world")}),
             MakeText("Second line"), MakeNode(ChaosElementTag::Text, LinkProps)});

        BuildContext Context;
        Context.FontBackend = &Font;
        Context.Font = 1;
        Context.Focus = &Focus;
        Context.Clipboard = &Clipboard;
        Context.Style = &Sheets;
        Context.StyleApplier = &Applier;
        Root = BuildWidgetTree(Body, Context);
        Root->Measure({400.0F, 300.0F});
        Root->Arrange({0.0F, 0.0F, 400.0F, 300.0F});
    }

    TextSelection* Selection() const {
        auto* AsBox = dynamic_cast<Box*>(Root.get());
        return AsBox != nullptr ? AsBox->GetTextSelection() : nullptr;
    }

    void Frame(InputState Input) { Root->UpdateInteractionState(Input); }

    void Drag(Penumbra::Point From, Penumbra::Point To) {
        InputState Press;
        Press.MousePosition = From;
        Press.MouseButtonDown[0] = true;
        Press.MouseButtonPressedThisFrame[0] = true;
        Frame(Press);
        InputState Move;
        Move.MousePosition = To;
        Move.MouseButtonDown[0] = true;
        Frame(Move);
        InputState Release;
        Release.MousePosition = To;
        Release.MouseButtonReleasedThisFrame[0] = true;
        Frame(Release);
    }

    void Shortcut(Key Pressed) {
        InputState Keys;
        Keys.ModifierState.Shortcut = true;
        Keys.KeysPressedThisFrame = {Pressed};
        Frame(Keys);
    }

    std::string Selected() const { return Selection() != nullptr ? Selection()->SelectedText(*Root) : "<none>"; }
};

void TestASelectableFrameGetsASelectionStyledFromCosmos() {
    SelectableBody Body;
    TextSelection* Selection = Body.Selection();
    Expect(Selection != nullptr, "<Frame selectable={true}> gets a text selection");
    if (Selection == nullptr) {
        return;
    }
    Expect(Selection->Focus == &Body.Focus && Selection->Clipboard == &Body.Clipboard,
           "the selection uses the app's focus and clipboard");
    Expect(Selection->ColorSelection.R == 0xC8 && Selection->ColorSelection.A == 0x66,
           "selection-color sets the selection's highlight");
}

void TestDraggingSelectsAcrossWordsAndBlocksAsPlainText() {
    SelectableBody Body;
    Body.Drag({1.0F, 10.0F}, {61.0F, 40.0F});
    Expect(Body.Selected() == "Hello world\nSecond",
           "a drag from a paragraph's first word into the next block joins words with a space and blocks with a "
           "new line, and stops at the character nearest the pointer");
    Expect(Body.Focus.Focused == Body.Root.get(), "starting a selection takes focus, so a focused field lets go");

    Body.Shortcut(Key::C);
    Expect(Body.Clipboard.Copied == "Hello world\nSecond", "Cmd+C copies the selection as plain text");
}

void TestDraggingBackwardsSelectsTheSameText() {
    SelectableBody Body;
    Body.Drag({61.0F, 40.0F}, {1.0F, 10.0F});
    Expect(Body.Selected() == "Hello world\nSecond", "a drag upwards selects the same text as one downwards");
}

void TestAClickClearsAndAPressOnALinkNeverSelects() {
    SelectableBody Body;
    Body.Drag({1.0F, 10.0F}, {61.0F, 40.0F});
    Body.Drag({20.0F, 40.0F}, {20.0F, 40.0F});
    Expect(Body.Selected().empty(), "a click without a drag clears the selection");

    Body.Drag({5.0F, 70.0F}, {61.0F, 40.0F});
    Expect(Body.Selected().empty(), "a drag that starts on a link doesn't select");
}

void TestSelectAllAndEscapeOnceTheBodyHasFocus() {
    SelectableBody Body;
    Body.Shortcut(Key::A);
    Expect(Body.Selected().empty(), "Cmd+A does nothing until the body has been clicked in");

    Body.Drag({20.0F, 40.0F}, {20.0F, 40.0F});
    Body.Shortcut(Key::A);
    Expect(Body.Selected() == "Hello world\nSecond line\nOpen", "Cmd+A then selects everything in the body");

    InputState Escape;
    Escape.KeysPressedThisFrame = {Key::Escape};
    Body.Frame(Escape);
    Expect(Body.Selected().empty(), "Escape clears the selection");
}

} // namespace

void RunTextSelectionTests() {
    TestASelectableFrameGetsASelectionStyledFromCosmos();
    TestDraggingSelectsAcrossWordsAndBlocksAsPlainText();
    TestDraggingBackwardsSelectsTheSameText();
    TestAClickClearsAndAPressOnALinkNeverSelects();
    TestSelectAllAndEscapeOnceTheBodyHasFocus();
}
