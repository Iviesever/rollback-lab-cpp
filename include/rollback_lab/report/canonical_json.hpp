#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/report/desync.hpp>
#include <rollback_lab/report/run_report.hpp>
#include <rollback_lab/report/trace.hpp>

#include <string>

namespace rollback_lab {

[[nodiscard]] auto canonical_json(const RunReport& report)
    -> Result<std::string>;
[[nodiscard]] auto canonical_json(const DesyncDiagnostic& diagnostic)
    -> Result<std::string>;
[[nodiscard]] auto canonical_json(const Trace& trace) -> Result<std::string>;

}  // namespace rollback_lab

