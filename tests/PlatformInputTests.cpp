#include "PenumbraUiBackend/ViewportHostWidget.h"

#include "Penumbra/Platform/InputState.h"
#include "Penumbra/Platform/PlatformWindow.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/FocusState.h"
#include "Penumbra/Widgets/TextInput.h"

#include <SDL3/SDL.h>

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

using Penumbra::Platform::InputState;
using Penumbra::Platform::Key;
using Penumbra::Platform::KeyEvent;
using Penumbra::Platform::PlatformWindow;

void PushMouseButton(SDL_Window* Window, bool Down) {
    SDL_Event Event{};
    Event.type             = Down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    Event.button.windowID  = SDL_GetWindowID(Window);
    Event.button.button    = SDL_BUTTON_LEFT;
    Event.button.down      = Down;
    Event.button.clicks    = 1;
    SDL_PushEvent(&Event);
}

void PushKey(SDL_Window* Window, SDL_Keycode Code, bool Down, SDL_Keymod Modifiers) {
    SDL_Event Event{};
    Event.type          = Down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    Event.key.windowID  = SDL_GetWindowID(Window);
    Event.key.key       = Code;
    Event.key.mod       = Modifiers;
    Event.key.down      = Down;
    SDL_PushEvent(&Event);
}

void PushTypedText(SDL_Window* Window, const char* Text) {
    SDL_Event Event{};
    Event.type          = SDL_EVENT_TEXT_INPUT;
    Event.text.windowID = SDL_GetWindowID(Window);
    Event.text.text     = Text;
    SDL_PushEvent(&Event);
}

SDL_Window* OnlyWindow() {
    int          Count   = 0;
    SDL_Window** Windows = SDL_GetWindows(&Count);
    SDL_Window*  Found   = (Windows != nullptr && Count > 0) ? Windows[0] : nullptr;
    SDL_free(Windows);
    return Found;
}

void TestAClickInsideOneFrameReachesAWidgetAsPressAndRelease(PlatformWindow& Window, SDL_Window* SdlWindow) {
    InputState Input;
    Window.PumpEventsAndBuildInput(Input);

    PushMouseButton(SdlWindow, true);
    PushMouseButton(SdlWindow, false);
    Window.PumpEventsAndBuildInput(Input);
    Expect(Input.MouseButtonPressedThisFrame[0] && Input.MouseButtonReleasedThisFrame[0] && !Input.MouseButtonDown[0],
           "a press and a release between two frames both reach the frame's input");

    Penumbra::Widgets::Box Target;
    int                    Presses  = 0;
    int                    Releases = 0;
    Target.OnPressed  = [&Presses]() { ++Presses; };
    Target.OnReleased = [&Releases]() { ++Releases; };
    Target.Arrange({Input.MousePosition.X - 10.0f, Input.MousePosition.Y - 10.0f, 20.0f, 20.0f});
    Target.UpdateInteractionState(Input);
    Expect(Presses == 1 && Releases == 1, "a click shorter than one frame fires the widget's onPress and onRelease");
}

void TestShortcutSelectAllThenTypingInOneFrameReplacesTheText(PlatformWindow& Window, SDL_Window* SdlWindow) {
    Penumbra::Widgets::FocusState Focus;
    Penumbra::Widgets::TextInput  Field;
    Field.Focus   = &Focus;
    Field.Text    = "12.5";
    Focus.Focused = &Field;

    InputState Input;
    Window.PumpEventsAndBuildInput(Input);

    PushKey(SdlWindow, SDLK_LCTRL, true, SDL_KMOD_LCTRL);
    PushKey(SdlWindow, SDLK_A, true, SDL_KMOD_LCTRL);
    PushKey(SdlWindow, SDLK_A, false, SDL_KMOD_LCTRL);
    PushKey(SdlWindow, SDLK_LCTRL, false, SDL_KMOD_NONE);
    PushKey(SdlWindow, SDLK_4, true, SDL_KMOD_NONE);
    PushTypedText(SdlWindow, "4");
    PushKey(SdlWindow, SDLK_4, false, SDL_KMOD_NONE);
    Window.PumpEventsAndBuildInput(Input);
    Field.UpdateInteractionState(Input);
    Expect(Field.Text == "4",
           "Ctrl+A then a typed key inside one frame selects all and replaces it (got \"" + Field.Text + "\")");
}

void TestTypedTextAndEditingKeysApplyInTheOrderTheyArrived() {
    Penumbra::Widgets::FocusState Focus;
    Penumbra::Widgets::TextInput  Field;
    Field.Focus   = &Focus;
    Field.Text    = "ab";
    Focus.Focused = &Field;
    Field.UpdateInteractionState(InputState{});
    InputState MoveToEnd;
    MoveToEnd.KeyEvents = {KeyEvent{Key::End, true, false, {}}};
    MoveToEnd.KeysPressedThisFrame = {Key::End};
    Field.UpdateInteractionState(MoveToEnd);

    InputState Input;
    Input.TextInputThisFrame   = "c";
    Input.KeysPressedThisFrame = {Key::Backspace};
    Input.KeyEvents            = {KeyEvent{Key::Backspace, true, false, {}}};
    Field.UpdateInteractionState(Input);
    Expect(Field.Text == "ac", "a Backspace typed before a character deletes before inserting (got \"" + Field.Text + "\")");
}

class KeyRecordingViewportRenderer final : public Umbra::IViewportRenderer {
public:
    std::vector<std::string> KeysDown;
    std::optional<Umbra::ViewportFrame> Render(std::uint32_t, std::uint32_t) override { return std::nullopt; }
    bool Key(std::string_view KeyName, bool Down, const Umbra::ViewportModifiers&) override {
        if (Down) {
            KeysDown.emplace_back(KeyName);
        }
        return true;
    }
};

InputState HoverWithKey(Key Code) {
    InputState Input;
    Input.MousePosition        = {50.0f, 50.0f};
    Input.KeysPressedThisFrame = {Code};
    Input.KeyEvents            = {KeyEvent{Code, true, false, {}}};
    return Input;
}

void TestAFocusedTextInputKeepsKeysFromAHoveredViewport() {
    Penumbra::Widgets::FocusState Focus;
    Penumbra::Widgets::TextInput  Field;
    Field.Focus = &Focus;

    KeyRecordingViewportRenderer          Renderer;
    PenumbraUiBackend::ViewportHostWidget Viewport(&Renderer);
    Viewport.Focus = &Focus;
    Viewport.Arrange({0.0f, 0.0f, 200.0f, 100.0f});

    Viewport.UpdateInteractionState(HoverWithKey(Key::W));
    Expect(Renderer.KeysDown == std::vector<std::string>{"w"}, "a hovered viewport gets keys while nothing has focus");

    Focus.Focused = &Field;
    Renderer.KeysDown.clear();
    Viewport.UpdateInteractionState(HoverWithKey(Key::W));
    Expect(Renderer.KeysDown.empty(), "a focused text input keeps keys away from a hovered viewport");

    InputState Click = HoverWithKey(Key::E);
    Click.MouseButtonPressedThisFrame[0] = true;
    Click.MouseButtonDown[0]             = true;
    Viewport.UpdateInteractionState(Click);
    Expect(Focus.Focused == &Viewport && Renderer.KeysDown == std::vector<std::string>{"e"},
           "a press on the viewport takes focus from the text input, and its keys reach the viewport");
}

} // namespace

void RunPlatformInputTests() {
    TestTypedTextAndEditingKeysApplyInTheOrderTheyArrived();
    TestAFocusedTextInputKeepsKeysFromAHoveredViewport();

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    PlatformWindow Window;
    const bool     Initialised = Window.Initialise("platform input test", 320, 240);
    Expect(Initialised, std::string("a PlatformWindow opens under SDL's dummy driver") +
                            (Initialised ? std::string() : std::string(" (SDL: ") + SDL_GetError() + ")"));
    if (Initialised) {
        SDL_Window* SdlWindow = OnlyWindow();
        TestAClickInsideOneFrameReachesAWidgetAsPressAndRelease(Window, SdlWindow);
        TestShortcutSelectAllThenTypingInOneFrameReplacesTheText(Window, SdlWindow);
        Window.Shutdown();
    }
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
}
