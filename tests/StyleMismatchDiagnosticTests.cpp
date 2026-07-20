// docs/build_context_style_mismatch_gap.md: BuildWidgetTree's debug-mode
// zero-match check -- a component built through a BuildContext whose
// .Style points at a stylesheet that shares no class names with the
// component being built used to fail completely silently (every classed
// Frame stuck at Box::Layout::None, measuring to {0,0}). These tests drive
// the real stderr output the fix now produces, not just the underlying
// counters -- the whole point of the fix is a human-visible signal.
#include "PenumbraUiBackend/Walker.h"

#include "Lustre/Parser.h"

#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

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

using Iris::Component;
using Iris::IrisElementTag;
using Iris::IrisProps;
using Iris::IrisPropValue;
using PenumbraUiBackend::BuildContext;
using PenumbraUiBackend::BuildWidgetTree;
using PenumbraUiBackend::Lustre::LustreStyleApplier;

Component MakeNode(IrisElementTag Tag, IrisProps Props = {}, std::vector<Component> Children = {}) {
    return Component(Tag, std::move(Props), std::move(Children), nullptr);
}

IrisProps WithClass(const std::string& ClassName) {
    IrisProps Props;
    Props["class"] = IrisPropValue{ClassName};
    return Props;
}

::Lustre::Stylesheet ParseOrDie(const std::string& Source, const std::string& FilePath) {
    ::Lustre::Parser Parser(Source, FilePath);
    ::Lustre::ParseResult Result = Parser.Parse();
    Expect(Result.Errors.empty() && Result.Sheet.has_value(), "test fixture stylesheet `" + FilePath + "` parses cleanly");
    return Result.Sheet.has_value() ? std::move(*Result.Sheet) : ::Lustre::Stylesheet{};
}

// Redirects the real stderr file descriptor (not just the FILE* stream --
// BuildWidgetTree's diagnostic writes through std::fprintf(stderr, ...),
// so the fd itself has to move) to a temp file for the duration of Fn,
// then restores it and returns whatever landed there.
std::string CaptureStderr(const std::function<void()>& Fn) {
    std::fflush(stderr);
    const int SavedFd = dup(fileno(stderr));
    char TmpPath[] = "/tmp/penumbra_ui_backend_stderr_capture_XXXXXX";
    const int TmpFd = mkstemp(TmpPath);
    dup2(TmpFd, fileno(stderr));
    close(TmpFd);

    Fn();

    std::fflush(stderr);
    dup2(SavedFd, fileno(stderr));
    close(SavedFd);

    std::ostringstream Captured;
    if (FILE* TmpFile = std::fopen(TmpPath, "r")) {
        char Buffer[512];
        std::size_t Read = 0;
        while ((Read = std::fread(Buffer, 1, sizeof(Buffer), TmpFile)) > 0) {
            Captured.write(Buffer, static_cast<std::streamsize>(Read));
        }
        std::fclose(TmpFile);
    }
    std::remove(TmpPath);
    return Captured.str();
}

void TestMismatchedStylesheetWarns() {
    // A real stylesheet, just for a class this tree never uses -- exactly
    // the InspectorRow/InspectorChrome shape: a real, valid StylesheetSet,
    // just the wrong one for what's being built.
    const ::Lustre::Stylesheet WrongSheet = ParseOrDie(".unrelated-class { background-color: #E8593C; }", "Wrong.lustre");
    const ::Lustre::StylesheetSet Sheets{nullptr, &WrongSheet};
    const LustreStyleApplier Applier;

    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("my-row"),
                                {MakeNode(IrisElementTag::Frame, WithClass("my-row-inner"))});

    const std::string Stderr = CaptureStderr([&]() { BuildWidgetTree(Node, Context); });

    Expect(Stderr.find("BuildWidgetTree") != std::string::npos &&
               Stderr.find("likely the wrong StylesheetSet") != std::string::npos,
           "a stylesheet that matches none of the tree's classes prints the mismatch warning");
    Expect(Stderr.find("2 widget(s)") != std::string::npos,
           "the warning counts both classed nodes (my-row, my-row-inner), not just the root");
}

void TestMatchingStylesheetStaysQuiet() {
    const ::Lustre::Stylesheet RightSheet = ParseOrDie(".my-row { background-color: #E8593C; }", "Right.lustre");
    const ::Lustre::StylesheetSet Sheets{nullptr, &RightSheet};
    const LustreStyleApplier Applier;

    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("my-row"));

    const std::string Stderr = CaptureStderr([&]() { BuildWidgetTree(Node, Context); });

    Expect(Stderr.empty(), "a stylesheet that resolves the tree's own class prints nothing");
}

void TestPartiallyMatchingStylesheetStaysQuiet() {
    // Only the child's class matches; the root's own class matches nothing.
    // One real resolution anywhere in the tree is enough to suppress the
    // whole-tree warning -- see build_context_style_mismatch_gap.md's own
    // "why any node resolved something, not per-node warnings" reasoning.
    const ::Lustre::Stylesheet Sheet = ParseOrDie(".my-row-inner { background-color: #E8593C; }", "Partial.lustre");
    const ::Lustre::StylesheetSet Sheets{nullptr, &Sheet};
    const LustreStyleApplier Applier;

    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("my-row"),
                                {MakeNode(IrisElementTag::Frame, WithClass("my-row-inner"))});

    const std::string Stderr = CaptureStderr([&]() { BuildWidgetTree(Node, Context); });

    Expect(Stderr.empty(), "at least one real resolution anywhere in the tree suppresses the warning");
}

void TestNoStyleConfiguredStaysQuiet() {
    // BuildContext{} -- Context.Style left null is a supported "no styling
    // at all" mode (BuildContext's own doc comment), not a mismatch.
    const auto Node = MakeNode(IrisElementTag::Frame, WithClass("my-row"));

    const std::string Stderr = CaptureStderr([&]() { BuildWidgetTree(Node, BuildContext{}); });

    Expect(Stderr.empty(), "a BuildContext with no Style configured at all doesn't warn");
}

void TestNoClassedNodesStaysQuiet() {
    const ::Lustre::Stylesheet Sheet = ParseOrDie(".irrelevant { background-color: #E8593C; }", "Irrelevant.lustre");
    const ::Lustre::StylesheetSet Sheets{nullptr, &Sheet};
    const LustreStyleApplier Applier;

    BuildContext Context;
    Context.Style = &Sheets;
    Context.StyleApplier = &Applier;

    const auto Node = MakeNode(IrisElementTag::Frame); // no `class` prop at all

    const std::string Stderr = CaptureStderr([&]() { BuildWidgetTree(Node, Context); });

    Expect(Stderr.empty(), "a tree with no `class` props at all doesn't warn, even against an unrelated stylesheet");
}

} // namespace

void RunStyleMismatchDiagnosticTests() {
    TestMismatchedStylesheetWarns();
    TestMatchingStylesheetStaysQuiet();
    TestPartiallyMatchingStylesheetStaysQuiet();
    TestNoStyleConfiguredStaysQuiet();
    TestNoClassedNodesStaysQuiet();
}
