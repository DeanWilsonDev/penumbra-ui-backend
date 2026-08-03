#include "PenumbraUiBackend/Lustre/StyleResolution.h"

namespace PenumbraUiBackend::Lustre {

::Lustre::ResolvedStyle ResolveStyle(const ::Lustre::IStyleTarget& Target, const ::Lustre::StylesheetSet& Sheets,
                                       std::vector<::Lustre::ResolveDiagnostic>* OutDiagnostics) {
    std::vector<::Lustre::ResolveDiagnostic> LocalDiagnostics;
    std::vector<::Lustre::ResolveDiagnostic>& Diagnostics = OutDiagnostics ? *OutDiagnostics : LocalDiagnostics;
    return ::Lustre::ResolveStyle(Target, Sheets, Diagnostics);
}

} // namespace PenumbraUiBackend::Lustre
