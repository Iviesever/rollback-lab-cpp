#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/report/trace.hpp>

#include <filesystem>
#include <string>

namespace rollback_lab {

[[nodiscard]] auto generate_viewer_html(const Trace& trace)
    -> Result<std::string>;
[[nodiscard]] auto write_viewer(const Trace& trace,
                                const std::filesystem::path& path)
    -> Result<void>;

}  // namespace rollback_lab

