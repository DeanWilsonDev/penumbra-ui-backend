#include "PenumbraUiBackend/ViewportHostWidget.h"

#include "PenumbraUiBackend/Walker.h"
#include "Penumbra/Widgets/KeyDispatch.h"

#include <cmath>

namespace PenumbraUiBackend {

namespace {

Umbra::ViewportModifiers ToViewportModifiers(const Penumbra::Platform::Modifiers& Modifiers) {
    Umbra::ViewportModifiers Result;
    Result.Shift    = Modifiers.Shift;
    Result.Control  = Modifiers.Ctrl;
    Result.Alt      = Modifiers.Alt;
    Result.Shortcut = Modifiers.Shortcut;
    return Result;
}

} // namespace

ViewportHostWidget::ViewportHostWidget(Umbra::IViewportRenderer* Renderer) : Renderer_(Renderer) {
    Style.FlexGrow = 1.0f;
}

bool ViewportHostWidget::AnotherWidgetHasFocus() const {
    return Focus != nullptr && Focus->Focused != nullptr && Focus->Focused != this;
}

ViewportHostWidget::~ViewportHostWidget() {
    if (Focus != nullptr && Focus->Focused == this) {
        Focus->Focused = nullptr;
    }
    if (Texture_ != nullptr) {
        SDL_DestroyTexture(Texture_);
    }
}

Penumbra::Point ViewportHostWidget::Measure(Penumbra::Point AvailableSizeLogical) {
    return AvailableSizeLogical;
}

void ViewportHostWidget::Arrange(Penumbra::Rect FinalRectLogical) {
    Box::Arrange(FinalRectLogical);
    const Penumbra::Rect Content = ContentRectFrom(FinalRectLogical);
    ContentSizeLogical_          = {Content.W, Content.H};
}

bool ViewportHostWidget::UpdateInteractionState(const Penumbra::Platform::InputState& Input) {
    const bool BoxConsumed = Box::UpdateInteractionState(Input);
    if (Renderer_ == nullptr) {
        return BoxConsumed;
    }
    Renderer_->Tick(Input.DeltaTimeSeconds);

    const Penumbra::Rect Content       = ContentRectFrom(ArrangedRect);
    const bool           PointerIsOver = IsEnabled && Input.IsPointerOver(Content);
    bool                 DragUnderway  = false;
    for (const bool Held : ButtonHeldByViewport_) {
        DragUnderway = DragUnderway || Held;
    }

    if (PointerIsOver != PointerWasOver_) {
        Renderer_->HoverChanged(PointerIsOver);
        PointerWasOver_ = PointerIsOver;
    }
    if (PointerIsOver && Focus != nullptr) {
        for (const bool Pressed : Input.MouseButtonPressedThisFrame) {
            if (Pressed) {
                Focus->Focused = this;
            }
        }
    }
    if (PointerIsOver || DragUnderway) {
        ForwardPointer(Input, Content, PointerIsOver);
        ForwardKeys(Input);
    }
    return BoxConsumed || PointerIsOver || DragUnderway;
}

void ViewportHostWidget::ForwardPointer(const Penumbra::Platform::InputState& Input, Penumbra::Rect ContentRect,
                                        bool PointerIsOver) {
    Renderer_->PointerMoved((Input.MousePosition.X - ContentRect.X) * DpiScaleFactor_,
                            (Input.MousePosition.Y - ContentRect.Y) * DpiScaleFactor_);

    const Umbra::ViewportModifiers Modifiers = ToViewportModifiers(Input.ModifierState);
    for (int Button = 0; Button < PointerButtonCount; ++Button) {
        const auto ViewportButton = static_cast<Umbra::ViewportPointerButton>(Button);
        if (Input.MouseButtonPressedThisFrame[Button] && PointerIsOver && !ButtonHeldByViewport_[Button]) {
            Renderer_->PointerButton(ViewportButton, true, Modifiers);
            ButtonHeldByViewport_[Button] = true;
        }
        if (Input.MouseButtonReleasedThisFrame[Button] && ButtonHeldByViewport_[Button]) {
            Renderer_->PointerButton(ViewportButton, false, Modifiers);
            ButtonHeldByViewport_[Button] = false;
        }
    }
    if (PointerIsOver && (Input.MouseWheelDelta != 0.0f || Input.MouseWheelDeltaX != 0.0f)) {
        Renderer_->Wheel(Input.MouseWheelDelta, Input.MouseWheelDeltaX);
    }
}

void ViewportHostWidget::ForwardKeys(const Penumbra::Platform::InputState& Input) {
    for (const Penumbra::Platform::KeyEvent& Event : Input.KeyEvents) {
        if (Event.Code == Penumbra::Platform::Key::Unknown || Event.Repeat) {
            continue;
        }
        if (Event.Down && AnotherWidgetHasFocus()) {
            continue;
        }
        const bool Used = Renderer_->Key(Penumbra::Platform::KeyName(Event.Code), Event.Down,
                                         ToViewportModifiers(Event.ModifierState));
        if (Used && Event.Down) {
            Penumbra::Widgets::KeyDispatch::MarkUsed(Event.Code);
        }
    }
}

bool ViewportHostWidget::UploadFrame(SDL_Renderer* SdlRenderer, const Umbra::ViewportFrame& Frame) {
    if (Frame.Pixels == nullptr || Frame.Width == 0 || Frame.Height == 0 || SdlRenderer == nullptr) {
        return false;
    }
    if (Texture_ == nullptr || Frame.Width != TextureWidth_ || Frame.Height != TextureHeight_) {
        if (Texture_ != nullptr) {
            SDL_DestroyTexture(Texture_);
        }
        Texture_ = SDL_CreateTexture(SdlRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(Frame.Width), static_cast<int>(Frame.Height));
        TextureWidth_  = Texture_ != nullptr ? Frame.Width : 0;
        TextureHeight_ = Texture_ != nullptr ? Frame.Height : 0;
        if (Texture_ == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(Texture_, SDL_SCALEMODE_LINEAR);
    }
    return SDL_UpdateTexture(Texture_, nullptr, Frame.Pixels, static_cast<int>(Frame.Width) * 4);
}

void ViewportHostWidget::DrawContent(Penumbra::Render::Renderer& Renderer, Penumbra::Rect ContentRect) {
    if (Renderer_ == nullptr) {
        return;
    }
    DpiScaleFactor_           = Renderer.GetDpiScaleFactor();
    const auto WidthPhysical  = static_cast<std::uint32_t>(std::lround(ContentSizeLogical_.X * DpiScaleFactor_));
    const auto HeightPhysical = static_cast<std::uint32_t>(std::lround(ContentSizeLogical_.Y * DpiScaleFactor_));
    if (WidthPhysical == 0 || HeightPhysical == 0) {
        return;
    }
    const std::optional<Umbra::ViewportFrame> Frame = Renderer_->Render(WidthPhysical, HeightPhysical);
    if (!Frame || !UploadFrame(Renderer.GetSdlRenderer(), *Frame)) {
        return;
    }
    Renderer.DrawTexture(Texture_, ContentRect);
}

} // namespace PenumbraUiBackend
