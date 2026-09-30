#include "PenumbraUiBackend/Lustre/StylesheetLoader.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

extern int Failures; // defined in WalkerTests.cpp

namespace {

void Expect(bool Condition, const std::string& Description) {
    if (Condition) {
        std::printf("[PASS] %s\n", Description.c_str());
    } else {
        std::printf("[FAIL] %s\n", Description.c_str());
        ++Failures;
    }
}

using PenumbraUiBackend::Lustre::LoadStylesheetFromFile;

// A real temp file rather than an in-memory stream, since
// LoadStylesheetFromFile's whole job is the disk-reading step itself.
std::string WriteTempLustreFile(const char* Contents) {
    char      path[] = "/tmp/penumbra_ui_backend_stylesheet_loader_test_XXXXXX";
    const int fd = mkstemp(path);
    write(fd, Contents, std::string(Contents).size());
    close(fd);
    return path;
}

void TestValidFileParsesIntoAPopulatedStylesheet() {
    const std::string path = WriteTempLustreFile(".foo { background-color: #FF0000; }");

    const ::Lustre::Stylesheet sheet = LoadStylesheetFromFile(path.c_str(), "TestValidFileParsesIntoAPopulatedStylesheet");

    Expect(!sheet.Rules.empty(), "a valid .lustre file parses into a Stylesheet with at least one rule");

    std::remove(path.c_str());
}

void TestMissingFileReturnsAnEmptyStylesheet() {
    const ::Lustre::Stylesheet sheet =
        LoadStylesheetFromFile("/nonexistent/path/does-not-exist.lustre", "TestMissingFileReturnsAnEmptyStylesheet");

    Expect(sheet.Rules.empty(), "a missing file returns a default-constructed, empty Stylesheet rather than crashing");
}

void TestFileWithParseErrorsStillReturnsWithoutCrashing() {
    // Duplicate-selector rule blocks are one of Parser's own diagnosed
    // syntax errors (Lustre/Parser.h) -- Parser::Parse() hands back the
    // (possibly partial) tree it built regardless, so a caller can report
    // every error at once; LoadStylesheetFromFile mirrors that, it doesn't
    // discard the Sheet just because Errors is non-empty.
    const std::string path =
        WriteTempLustreFile(".foo { background-color: #FF0000; } .foo { background-color: #00FF00; }");

    const ::Lustre::Stylesheet sheet =
        LoadStylesheetFromFile(path.c_str(), "TestFileWithParseErrorsStillReturnsWithoutCrashing");

    Expect(sheet.Rules.size() == 2, "a file with a parse error still returns the rules the parser did manage to build");

    std::remove(path.c_str());
}

std::string FontFamilyOf(const ::Lustre::Rule& Rule) {
    for (const ::Lustre::Declaration& Decl : Rule.Declarations) {
        if (Decl.Property == "font-family" && !Decl.Values.empty() && Decl.Values[0].StringValue) {
            return *Decl.Values[0].StringValue;
        }
    }
    return {};
}

void TestRelativeFontFamilyPathsResolveAgainstTheStylesheetsDirectory() {
    const std::string path = WriteTempLustreFile(".body { font-family: \"fonts/Body.ttf\"; font-size: 14px; }\n"
                                                 ".code { font-family: \"/opt/fonts/Code.ttf\"; font-size: 13px; }\n"
                                                 ".card {\n"
                                                 "    .card-title { font-family: \"../shared/Title.ttf\"; font-size: 20px; }\n"
                                                 "}\n");
    const std::filesystem::path sheetDir = std::filesystem::path(path).parent_path();

    const ::Lustre::Stylesheet sheet =
        LoadStylesheetFromFile(path.c_str(), "TestRelativeFontFamilyPathsResolveAgainstTheStylesheetsDirectory");

    Expect(sheet.Rules.size() == 3 && sheet.Rules[2]->NestedRules.size() == 1,
           "the font-family fixture parses into three rules, one with a nested rule");
    if (sheet.Rules.size() == 3 && sheet.Rules[2]->NestedRules.size() == 1) {
        Expect(FontFamilyOf(*sheet.Rules[0]) == (sheetDir / "fonts/Body.ttf").string(),
               "a relative font-family path resolves against the stylesheet's own directory");
        Expect(FontFamilyOf(*sheet.Rules[1]) == "/opt/fonts/Code.ttf", "an absolute font-family path is left as written");
        Expect(FontFamilyOf(*sheet.Rules[2]->NestedRules[0]) ==
                   (sheetDir / "../shared/Title.ttf").lexically_normal().string(),
               "a relative font-family path in a nested rule resolves too, normalised");
    }

    std::remove(path.c_str());
}

} // namespace

void RunStylesheetLoaderTests() {
    TestValidFileParsesIntoAPopulatedStylesheet();
    TestMissingFileReturnsAnEmptyStylesheet();
    TestFileWithParseErrorsStillReturnsWithoutCrashing();
    TestRelativeFontFamilyPathsResolveAgainstTheStylesheetsDirectory();
}
