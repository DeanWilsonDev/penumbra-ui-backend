#include "PenumbraUiBackend/Lustre/StylesheetLoader.h"

#include "Lustre/Parser.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace PenumbraUiBackend::Lustre {

namespace {

void ResolveRelativeFontPaths(std::vector<::Lustre::RulePtr>& Rules, const std::filesystem::path& SheetDir) {
    for (::Lustre::RulePtr& R : Rules) {
        for (::Lustre::Declaration& Decl : R->Declarations) {
            if (Decl.Property != "font-family") continue;
            for (::Lustre::ValuePart& Value : Decl.Values) {
                if (!Value.StringValue) continue;
                const std::filesystem::path FontPath(*Value.StringValue);
                if (FontPath.is_relative()) Value.StringValue = (SheetDir / FontPath).lexically_normal().string();
            }
        }
        ResolveRelativeFontPaths(R->NestedRules, SheetDir);
    }
}

} // namespace

::Lustre::Stylesheet LoadStylesheetFromFile(const char* Path, const char* LabelForErrors) {
    std::ifstream file(Path);
    if (!file) {
        std::fprintf(stderr, "%s: could not open %s\n", LabelForErrors, Path);
        return ::Lustre::Stylesheet{};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string     source = buffer.str();
    ::Lustre::Parser      parser(source, Path);
    ::Lustre::ParseResult result = parser.Parse();
    if (!result.Errors.empty()) {
        std::fprintf(stderr, "%s: %s failed to parse:\n", LabelForErrors, Path);
        for (const auto& error : result.Errors) {
            std::fprintf(stderr, "  %s\n", error.Message.c_str());
        }
    }

    if (!result.Sheet.has_value()) return ::Lustre::Stylesheet{};
    ResolveRelativeFontPaths(result.Sheet->Rules, std::filesystem::path(Path).parent_path());
    return std::move(*result.Sheet);
}

} // namespace PenumbraUiBackend::Lustre
