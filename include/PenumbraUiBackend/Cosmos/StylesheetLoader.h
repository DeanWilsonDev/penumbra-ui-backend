#pragma once

#include "cosmos/Ast.h"

namespace PenumbraUiBackend::Cosmos {

::Cosmos::Stylesheet LoadStylesheetFromFile(const char* Path, const char* LabelForErrors);

} // namespace PenumbraUiBackend::Cosmos
