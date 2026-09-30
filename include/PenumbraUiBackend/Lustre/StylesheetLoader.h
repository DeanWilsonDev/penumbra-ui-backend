#pragma once

#include "Lustre/Ast.h"

namespace PenumbraUiBackend::Lustre {

::Lustre::Stylesheet LoadStylesheetFromFile(const char* Path, const char* LabelForErrors);

} // namespace PenumbraUiBackend::Lustre
