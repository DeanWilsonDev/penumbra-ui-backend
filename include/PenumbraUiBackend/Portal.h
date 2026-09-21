#pragma once

#include "PenumbraUiBackend/PenumbraWidgetAdapter.h"

#include "Iris/Portal.h"
#include "Penumbra/Widgets/Box.h"
#include "Penumbra/Widgets/OverlayHost.h"

#include <algorithm>
#include <memory>

namespace PenumbraUiBackend {

// CenterWithinWindow -- "fill AvailableSizeLogical inset by MarginLogical on each side,
// clamped to at least MinimumLogical on each axis, then center the (possibly clamped)
// result within AvailableSizeLogical" -- the generic centered-modal-of-size-X geometry a
// host app's own near-fullscreen dialog wants (Cairn's own CardModal.irisx sizing is the
// first caller; pharos-proto's own modals almost certainly want the same math), not
// specific to any one app or to Portal/OverlayHost's own mechanics -- just colocated here
// since a centered dialog is Portal's most common use.
inline Penumbra::Rect CenterWithinWindow(Penumbra::Point AvailableSizeLogical, float MarginLogical,
                                          float MinimumLogical = 200.0f) {
    const float Width  = std::max(MinimumLogical, AvailableSizeLogical.X - 2.0f * MarginLogical);
    const float Height = std::max(MinimumLogical, AvailableSizeLogical.Y - 2.0f * MarginLogical);
    return Penumbra::Rect{(AvailableSizeLogical.X - Width) / 2.0f, (AvailableSizeLogical.Y - Height) / 2.0f, Width,
                           Height};
}

// Zero-layout owner for a Portal's ordinary reconciled child. The real child remains
// here, in Iris's normal ownership tree; OverlayHost owns only a presentation surface
// that delegates layout/input/drawing to it. This keeps host-driven dismissal from
// destroying content while Iris still retains wrappers and SlotStates for that content.
class PortalAnchorWidget final : public Penumbra::Widgets::Box {
public:
    PortalAnchorWidget(Penumbra::Widgets::OverlayHost* Host, std::unique_ptr<Penumbra::Widgets::WidgetBase> Content,
                       const Iris::PortalProperties& Properties);
    ~PortalAnchorWidget() override;

    void ApplyPortalProperties(const Iris::PortalProperties& Properties);
    void AttachAdapter(Iris::IPortalTarget* Adapter);
    void DetachAdapter(Iris::IPortalTarget* Adapter);
    void PreparePortalUnmount();

    Penumbra::Point Measure(Penumbra::Point AvailableSizeLogical) override;
    void            Arrange(Penumbra::Rect FinalRectLogical) override;
    bool            UpdateInteractionState(const Penumbra::Platform::InputState& Input) override;
    void            Draw(Penumbra::Render::Renderer& Renderer) override;

private:
    struct State;
    std::shared_ptr<State> State_;
};

// The Umbra adapter Iris sees for a Portal. Ordinary child reconciliation continues
// through PenumbraWidget; this second interface carries portal props and explicit
// teardown ordering.
class PenumbraPortalWidget final : public PenumbraWidget, public Iris::IPortalTarget {
public:
    explicit PenumbraPortalWidget(std::unique_ptr<Penumbra::Widgets::WidgetBase> Widget);
    explicit PenumbraPortalWidget(PortalAnchorWidget* Widget);
    ~PenumbraPortalWidget() override;

    void ApplyPortalProperties(const Iris::PortalProperties& Properties) override;
    void PreparePortalUnmount() override;

private:
    PortalAnchorWidget* Anchor_{nullptr};
};

} // namespace PenumbraUiBackend
