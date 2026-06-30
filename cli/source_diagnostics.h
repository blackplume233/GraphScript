#pragma once

#include <cstddef>
#include <string>

#include "graphscript/core/result.h"
#include "graphscript/registry/environment.h"

namespace gs {

/// Options controlling source diagnostics and optional import-aware dry-run resolution.
struct SourceDiagnosticsOptions {
    bool resolve_imports = false;
    std::string base_dir;
    size_t max_imports = 32;
    size_t max_import_depth = 8;
    size_t max_file_bytes = 1024 * 1024;
    size_t max_total_bytes = 4 * 1024 * 1024;
};

/// Parses and lints source into JSON diagnostics, optionally resolving `.d.gs`
/// imports into a dry-run asset environment without mutating the session Environment.
std::string source_diagnostics_to_json(const std::string& source,
                                       const Environment& session_env,
                                       const SourceDiagnosticsOptions& options = {});

/// Computes the environment hash that source diagnostics would use for `source`.
/// Returns an error when parsing or resolver diagnostics prevent a trustworthy hash.
Result<std::string, std::string> source_diagnostics_environment_hash(
    const std::string& source,
    const Environment& session_env,
    const SourceDiagnosticsOptions& options = {});

} // namespace gs
