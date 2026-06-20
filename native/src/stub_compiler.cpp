#include "mkpro/core/result.hpp"

#include <utility>

namespace mkpro {

CompileResult compile_source_stub(std::string source, const CompileOptions& options) {
  (void)source;
  (void)options;
  return CompileResult{
      .implemented = false,
      .diagnostics = {
          Diagnostic{
              .severity = DiagnosticSeverity::Error,
              .code = "native-not-implemented",
              .message = "native MK-Pro compilation is not implemented in the phase-1 scaffold",
          },
      },
  };
}

std::string diagnostic_severity_name(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Note:
      return "note";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Error:
      return "error";
  }
  return "error";
}

}  // namespace mkpro
