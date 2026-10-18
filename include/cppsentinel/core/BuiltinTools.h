#pragma once

// Built-in checker metadata shared by the engine and `doctor`.

#include <string>

namespace cppsentinel
{

/// Default executable for a checker kind (empty for unknown kinds).
/// `[[checker]].executable` overrides this.
std::string defaultExecutableForKind(const std::string& kind);

/// True when the checker cannot run without [project].compilation_database.
bool checkerNeedsCompilationDatabase(const std::string& kind);

} // namespace cppsentinel
