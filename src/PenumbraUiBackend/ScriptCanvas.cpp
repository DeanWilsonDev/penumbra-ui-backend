#include "PenumbraUiBackend/ScriptCanvas.h"

#include "Penumbra/Render/Renderer.h"

#include <host/marshal.hpp>

#include <cstdint>
#include <memory>

namespace {

void DrawLineRaw(Penumbra::Render::Renderer& Renderer, float X1, float Y1, float X2, float Y2,
                 int R, int G, int B, int A, float Thickness) {
    Renderer.DrawLine({X1, Y1}, {X2, Y2},
                      Penumbra::Render::Color{static_cast<std::uint8_t>(R), static_cast<std::uint8_t>(G),
                                               static_cast<std::uint8_t>(B), static_cast<std::uint8_t>(A)},
                      Thickness);
}

} // namespace

namespace PenumbraUiBackend {

const nyx::runtime::TypeDescriptor* RegisterRendererType(nyx::host::NyxRuntime& Runtime) {
    Runtime.RegisterType<Penumbra::Render::Renderer>("Renderer").Method("DrawLine", &DrawLineRaw);
    return std::get<std::shared_ptr<nyx::runtime::HostObject>>(Runtime.Globals().at("Renderer").data)->descriptor;
}

Penumbra::Point ScriptCanvas::MeasureContent(Penumbra::Point /*AvailableContentSize*/) {
    if (!Interp) return {0.0f, 0.0f};
    float Width = 0.0f;
    float Height = 0.0f;
    if (!MeasureWidthFunctionName.empty()) {
        Width = nyx::host::FromValue<float>(Interp->CallFunction(MeasureWidthFunctionName, {}));
    }
    if (!MeasureHeightFunctionName.empty()) {
        Height = nyx::host::FromValue<float>(Interp->CallFunction(MeasureHeightFunctionName, {}));
    }
    return {Width, Height};
}

void ScriptCanvas::DrawContent(Penumbra::Render::Renderer& Renderer, Penumbra::Rect ContentRect) {
    if (!Interp || OnDrawFunctionName.empty() || !RendererDescriptor) return;
    Interp->CallFunction(OnDrawFunctionName,
                         {nyx::host::ToValue(&Renderer, RendererDescriptor),
                          nyx::host::ToValue(ContentRect.X), nyx::host::ToValue(ContentRect.Y),
                          nyx::host::ToValue(ContentRect.W), nyx::host::ToValue(ContentRect.H)});
}

} // namespace PenumbraUiBackend
