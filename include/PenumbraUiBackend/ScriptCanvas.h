#pragma once

#include "Penumbra/Widgets/Box.h"

#include <host/nyx-runtime.hpp>
#include <interpreter/interpreter.hpp>

#include <string>

namespace PenumbraUiBackend {

// Registers the Penumbra renderer surface used by Nyx-authored custom drawing.
// The returned descriptor wraps a live Renderer for calls into Nyx.
const nyx::runtime::TypeDescriptor* RegisterRendererType(nyx::host::NyxRuntime& Runtime);

// A reusable native canvas whose measurement and drawing decisions are supplied
// by Nyx functions. Interp and RendererDescriptor are non-owning and must
// outlive this widget.
class ScriptCanvas : public Penumbra::Widgets::Box {
public:
    nyx::interpreter::Interpreter*      Interp = nullptr;
    const nyx::runtime::TypeDescriptor* RendererDescriptor = nullptr;
    std::string                         OnDrawFunctionName;
    std::string                         MeasureWidthFunctionName;
    std::string                         MeasureHeightFunctionName;

protected:
    Penumbra::Point MeasureContent(Penumbra::Point AvailableContentSize) override;
    void DrawContent(Penumbra::Render::Renderer& Renderer, Penumbra::Rect ContentRect) override;
};

} // namespace PenumbraUiBackend
