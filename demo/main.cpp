// A small, real running app exercising both wiring points
// docs/penumbra_ui_backend_lustre_bridge_decision.md's "Wiring into the mount and
// reconcile paths" describes: BuildWidgetTree/Walker.cpp resolving Lustre style at
// mount, and PenumbraWidgetAdapter::ApplyPropDiff re-resolving it on a live class
// change. Not shipped as part of the library -- purely for visually confirming the
// wiring works against a real window/renderer, not just test assertions.
//
// Recreates docs/lustre_core_spec.md §4's HealthBar worked example: an outer
// `.health-bar` container wrapping an inner bar whose class -- and therefore its
// background-color -- switches between `.bar-normal` and `.bar-critical`. Click
// anywhere to toggle it; the color change you see is PenumbraWidgetAdapter::
// ApplyPropDiff re-resolving and re-applying style through the exact path a real
// Iris reconcile (`iris::Signal`/`iris::Tick`) would drive in a full app.
#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"
#include "PenumbraUiBackend/Walker.h"

#include "Lustre/Parser.h"

#include "Iris/IrisComponent.h"

#include "Penumbra/Platform/PlatformWindow.h"
#include "Penumbra/Render/Renderer.h"
#include "Penumbra/Render/SdlTtfFontBackend.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/Label.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace PenumbraUiBackend;
using namespace Penumbra::Widgets;

namespace {

constexpr int WindowLogicalWidth = 480;
constexpr int WindowLogicalHeight = 240;

// Self-contained automation hooks for driving this demo from a script with no
// external tooling (no xdotool/import/scrot needed, none of which were
// available when this was written) -- SDL3 already has everything needed to
// screenshot and to drive input via the exact same code path a real click
// takes. All three env vars are optional; unset, the demo behaves exactly as
// it always did (interactive only). See
// .claude/skills/run-penumbra-ui-backend/SKILL.md for the driver this powers.
//   DEMO_SCREENSHOT_BEFORE=<path>  -- BMP written a few frames after launch,
//                                     before any click (mount-time render).
//   DEMO_SCREENSHOT_AFTER=<path>   -- BMP written a few frames after an
//                                     auto-triggered click (reconcile-time
//                                     re-render). Implies the auto-click.
//   DEMO_AUTO_EXIT=1               -- quit automatically once every
//                                     requested screenshot has been written,
//                                     instead of waiting for window close.
bool EnvFlagSet(const char* Name) {
    const char* Value = std::getenv(Name);
    return Value != nullptr && Value[0] != '\0';
}

// Grabs the current frame straight from the renderer (whatever was last
// presented) and writes it out as a BMP -- no PNG/image library needed,
// SDL3 has both pixel readback and BMP encoding built in.
void SaveScreenshot(SDL_Renderer* SdlRenderer, const std::string& Path) {
    SDL_Surface* Frame = SDL_RenderReadPixels(SdlRenderer, nullptr);
    if (Frame == nullptr) {
        std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
        return;
    }
    if (!SDL_SaveBMP(Frame, Path.c_str())) {
        std::fprintf(stderr, "Failed to write screenshot to %s: %s\n", Path.c_str(), SDL_GetError());
    } else {
        std::printf("Wrote screenshot: %s\n", Path.c_str());
    }
    SDL_DestroySurface(Frame);
}

Iris::IrisComponent MakeNode(Iris::IrisElementTag Tag, Iris::IrisProps Props = {},
                              std::vector<Iris::IrisComponent> Children = {}) {
    return Iris::IrisComponent(Tag, std::move(Props), std::move(Children), nullptr);
}

Iris::IrisProps WithClass(const std::string& ClassName) {
    Iris::IrisProps Props;
    Props["class"] = Iris::IrisPropValue{ClassName};
    return Props;
}

// The bar's own IrisComponent subtree -- the part this demo's stylesheet targets.
// `BarClass` is the one thing that changes between mount and a later click, matching
// HealthBar.iris's own `barClass` local in the spec's worked example.
Iris::IrisComponent MakeHealthBarNode(const std::string& BarClass) {
    std::vector<Iris::IrisComponent> BarChildren;
    BarChildren.push_back(MakeNode(Iris::IrisElementTag::Text, WithClass("bar-label")));
    std::vector<Iris::IrisComponent> Children;
    Children.push_back(MakeNode(Iris::IrisElementTag::Frame, WithClass(BarClass), std::move(BarChildren)));
    return MakeNode(Iris::IrisElementTag::Frame, WithClass("health-bar"), std::move(Children));
}

} // namespace

int main() {
    const std::string StylesheetSource = R"(
.health-bar {
    display: stack;
    background-color: #222222;
    border-radius: 10px;
    padding: 16px;
}

.bar-normal {
    display: stack;
    background-color: #4CAF50;
    border-radius: 6px;
    padding: 10px 20px;
}

.bar-critical {
    display: stack;
    background-color: #E8593C;
    border-radius: 6px;
    padding: 10px 20px;
}

.bar-label {
    color: #FFFFFF;
}
)";
    ::Lustre::Parser       Parser(StylesheetSource, "HealthBar.lustre");
    ::Lustre::ParseResult ParseResult = Parser.Parse();
    if (!ParseResult.Errors.empty() || !ParseResult.Sheet.has_value()) {
        std::fprintf(stderr, "Failed to parse the demo stylesheet:\n");
        for (const auto& Error : ParseResult.Errors) {
            std::fprintf(stderr, "  %s\n", Error.Message.c_str());
        }
        return 1;
    }
    const ::Lustre::Stylesheet             ComponentSheet = std::move(*ParseResult.Sheet);
    const ::Lustre::StylesheetSet          Sheets{nullptr, &ComponentSheet};
    const PenumbraUiBackend::Lustre::LustreStyleApplier Applier;

    Penumbra::Platform::PlatformWindow Window;
    if (!Window.Initialise("Lustre Style Wiring Demo", WindowLogicalWidth, WindowLogicalHeight)) {
        std::fprintf(stderr, "Failed to initialise platform window: %s\n", Window.GetLastError().c_str());
        return 1;
    }

    Penumbra::Render::SdlTtfFontBackend FontBackend;
    const std::string                   FontPath = std::string(DEMO_ASSET_DIR) + "/JetBrainsMonoNerdFontMono-Regular.ttf";
    float                                DpiScaleFactor = Window.GetDpiScaleFactor();
    Penumbra::Render::FontHandle         BodyFont = FontBackend.LoadFont(FontPath.c_str(), 16.0F, DpiScaleFactor);

    Penumbra::Render::Renderer Renderer;
    if (!Renderer.Initialise(Window.GetSdlRenderer(), DpiScaleFactor, &FontBackend)) {
        std::fprintf(stderr, "Failed to initialise renderer\n");
        Window.Shutdown();
        return 1;
    }

    BuildContext Context;
    Context.FontBackend = &FontBackend;
    Context.Font = BodyFont;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    bool Critical = false;

    std::unique_ptr<WidgetBase> Built = BuildWidgetTree(MakeHealthBarNode("bar-normal"), Context);
    std::unique_ptr<PenumbraWidget> HealthBarWrapper =
        WrapExistingTree(std::move(Built), nullptr, nullptr, Context.Style, Context.StyleApplier);

    // The Text child of the bar element -- set once here, same as any ordinary
    // non-Lustre-styled prop (HealthBar.iris's own `{props.current}/{props.max}`
    // interpolation, docs/lustre_core_spec.md §4).
    if (auto* HealthBarBox = dynamic_cast<Box*>(HealthBarWrapper->RawWidget())) {
        if (HealthBarBox->GetChildCount() == 1) {
            if (auto* BarBox = dynamic_cast<Box*>(HealthBarBox->GetChildAt(0))) {
                if (BarBox->GetChildCount() == 1) {
                    if (auto* Text = dynamic_cast<Label*>(BarBox->GetChildAt(0))) {
                        Text->Text = "HP: 72 / 100";
                        Text->FontBackend = &FontBackend;
                        Text->Font = BodyFont;
                    }
                }
            }
        }
    }

    Umbra::IWidget* BarWrapper = HealthBarWrapper->GetChildCount() == 1 ? HealthBarWrapper->GetChildAt(0) : nullptr;
    if (BarWrapper == nullptr) {
        std::fprintf(stderr, "Expected the health bar to have exactly one child.\n");
        return 1;
    }

    auto StatusLabel = std::make_unique<Label>();
    StatusLabel->FontBackend = &FontBackend;
    StatusLabel->Font = BodyFont;
    StatusLabel->ColorText = {200, 200, 200, 255};

    auto Root = std::make_unique<Box>();
    Root->Layout = LayoutMode::VerticalStack;
    Root->ChildGap = 16.0F;
    Root->Style.Padding = {24.0F, 24.0F, 24.0F, 24.0F};
    Label* StatusLabelPtr = StatusLabel.get();
    Root->AddChild(std::move(StatusLabel));
    Root->AddChild(HealthBarWrapper->DetachOwnership());

    std::printf("Click anywhere to toggle the health bar between .bar-normal and .bar-critical.\n");
    std::fflush(stdout);

    auto ToggleClass = [&]() {
        Critical = !Critical;
        Umbra::IrisPropDiff Diff;
        Diff.ClassName = Critical ? "bar-critical" : "bar-normal";
        BarWrapper->ApplyPropDiff(Diff); // the exact reconcile-time re-styling path, real click or automated
    };

    const std::string ScreenshotBeforePath = std::getenv("DEMO_SCREENSHOT_BEFORE") ? std::getenv("DEMO_SCREENSHOT_BEFORE") : "";
    const std::string ScreenshotAfterPath = std::getenv("DEMO_SCREENSHOT_AFTER") ? std::getenv("DEMO_SCREENSHOT_AFTER") : "";
    const bool         AutoExit = EnvFlagSet("DEMO_AUTO_EXIT");
    bool               TookBeforeShot = ScreenshotBeforePath.empty();
    bool               TookAfterShot = ScreenshotAfterPath.empty();
    bool               DidAutoClick = ScreenshotAfterPath.empty();

    // Wall-clock, not frame-count: the window can take an arbitrary, WM-
    // dependent number of frames before it's actually mapped and presenting
    // real content (confirmed empirically -- a 5-frame threshold captured a
    // solid black backbuffer). 500ms is generous for a window this trivial
    // while still keeping the automated run well under a second.
    const Uint64 StartTicks = SDL_GetTicks();

    Penumbra::Platform::InputState Input;
    bool                            KeepRunning = true;
    while (KeepRunning) {
        KeepRunning = Window.PumpEventsAndBuildInput(Input);
        const Uint64 ElapsedMs = SDL_GetTicks() - StartTicks;

        const float CurrentDpiScaleFactor = Window.GetDpiScaleFactor();
        Renderer.SetDpiScaleFactor(CurrentDpiScaleFactor);

        if (Input.MouseButtonPressedThisFrame[0]) {
            ToggleClass();
        }
        if (!DidAutoClick && ElapsedMs >= 1000) {
            ToggleClass();
            DidAutoClick = true;
        }

        StatusLabelPtr->Text =
            std::string("class = \"") + (Critical ? "bar-critical" : "bar-normal") + "\"  (click to toggle)";

        const Penumbra::Point WindowSize = Window.GetLogicalWindowSize();
        const Penumbra::Rect  FullRect{0.0F, 0.0F, WindowSize.X, WindowSize.Y};
        Root->Measure({FullRect.W, FullRect.H});
        Root->Arrange(FullRect);

        Renderer.BeginFrame({20, 20, 24, 255});
        Root->Draw(Renderer);

        // Captured between Draw and Present, not after -- SDL_RenderReadPixels
        // reads the current render target, which at this point definitely
        // holds what Draw just wrote. Capturing *after* EndFrameAndPresent
        // (which calls SDL_RenderPresent) raced the backbuffer swap on an
        // accelerated/double-buffered renderer: confirmed empirically across
        // several runs to intermittently read the *other*, not-yet-drawn-to
        // buffer, producing a solid-black screenshot roughly half the time
        // even with a generous wall-clock delay already elapsed.
        if (!TookBeforeShot && ElapsedMs >= 500) {
            SaveScreenshot(Window.GetSdlRenderer(), ScreenshotBeforePath);
            TookBeforeShot = true;
        }
        if (!TookAfterShot && ElapsedMs >= 1500) {
            SaveScreenshot(Window.GetSdlRenderer(), ScreenshotAfterPath);
            TookAfterShot = true;
        }

        Renderer.EndFrameAndPresent();

        if (AutoExit && TookBeforeShot && TookAfterShot) {
            KeepRunning = false;
        }
    }

    Window.Shutdown();
    return 0;
}
