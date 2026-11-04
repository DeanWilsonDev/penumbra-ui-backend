#pragma once

#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/FocusState.h"
#include "Equinox/IViewportRenderer.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

namespace PenumbraUiBackend {

// The widget a `<viewport renderer="Name">` builds: a Box whose content is whatever the
// host's Equinox::IViewportRenderer hands back each frame. Each draw asks the renderer for a
// frame at the content's size in device pixels, uploads it into a streaming texture and
// draws that. Pointer input over the content, and a drag that started there, reaches the
// renderer in viewport pixels; keys reach it while the pointer is over the content or a
// drag is under way, named as Chaos names them, unless another widget has keyboard focus.
// A press on the content takes focus. The renderer outlives the widget: the host
// registered it with the ChaosApplication and owns it.
class ViewportHostWidget : public Penumbra::Widgets::Box {
public:
    explicit ViewportHostWidget(Equinox::IViewportRenderer* Renderer);
    ~ViewportHostWidget() override;

    ViewportHostWidget(const ViewportHostWidget&)            = delete;
    ViewportHostWidget& operator=(const ViewportHostWidget&) = delete;

    Equinox::IViewportRenderer* Renderer() const { return Renderer_; }

    Penumbra::Widgets::FocusState* Focus{nullptr};

    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override;
    void            Arrange(Penumbra::Rect FinalRectLogical) override;
    bool            UpdateInteractionState(const Penumbra::Platform::InputState& Input) override;

protected:
    void DrawContent(Penumbra::Render::Renderer& Renderer, Penumbra::Rect ContentRect) override;

private:
    static constexpr int PointerButtonCount = 3;

    void ForwardPointer(const Penumbra::Platform::InputState& Input, Penumbra::Rect ContentRect, bool PointerIsOver);
    void ForwardKeys(const Penumbra::Platform::InputState& Input);
    bool AnotherWidgetHasFocus() const;
    bool UploadFrame(SDL_Renderer* SdlRenderer, const Equinox::ViewportFrame& Frame);

    Equinox::IViewportRenderer* Renderer_;
    SDL_Texture*              Texture_{nullptr};
    std::uint32_t             TextureWidth_{0};
    std::uint32_t             TextureHeight_{0};
    Penumbra::Point           ContentSizeLogical_{0.0f, 0.0f};
    float                     DpiScaleFactor_{1.0f};
    bool                      PointerWasOver_{false};

    std::array<bool, PointerButtonCount> ButtonHeldByViewport_{};
};

} // namespace PenumbraUiBackend
