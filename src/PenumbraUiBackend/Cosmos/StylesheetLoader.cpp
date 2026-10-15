#include "PenumbraUiBackend/Cosmos/StylesheetLoader.h"

#include "Cosmos/Parser.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace PenumbraUiBackend::Cosmos {

namespace {

void ResolveRelativeFontPaths(std::vector<::Cosmos::RulePtr>& Rules, const std::filesystem::path& SheetDir) {
    for (::Cosmos::RulePtr& R : Rules) {
        for (::Cosmos::Declaration& Decl : R->Declarations) {
            if (Decl.Property != "font-family") continue;
            for (::Cosmos::ValuePart& Value : Decl.Values) {
                if (!Value.StringValue) continue;
                const std::filesystem::path FontPath(*Value.StringValue);
                if (FontPath.is_relative()) Value.StringValue = (SheetDir / FontPath).lexically_normal().string();
            }
        }
        ResolveRelativeFontPaths(R->NestedRules, SheetDir);
    }
}

} // namespace

::Cosmos::Stylesheet LoadStylesheetFromFile(const char* Path, const char* LabelForErrors) {
    std::ifstream file(Path);
    if (!file) {
        std::fprintf(stderr, "%s: could not open %s\n", LabelForErrors, Path);
        return ::Cosmos::Stylesheet{};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string     source = buffer.str();
    ::Cosmos::Parser      parser(source, Path);
    ::Cosmos::ParseResult result = parser.Parse();
    if (!result.Errors.empty()) {
        std::fprintf(stderr, "%s: %s failed to parse:\n", LabelForErrors, Path);
        for (const auto& error : result.Errors) {
            std::fprintf(stderr, "  %s\n", error.Message.c_str());
        }
    }

    if (!result.Sheet.has_value()) return ::Cosmos::Stylesheet{};
    ResolveRelativeFontPaths(result.Sheet->Rules, std::filesystem::path(Path).parent_path());
    return std::move(*result.Sheet);
}

} // namespace PenumbraUiBackend::Cosmos
