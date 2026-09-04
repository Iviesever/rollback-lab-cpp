#pragma once

#include <span>
#include <string>

namespace rollback_lab {

[[nodiscard]] auto run_cli(std::span<const std::string> arguments) -> int;

}  // namespace rollback_lab

