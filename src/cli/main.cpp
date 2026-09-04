#include <rollback_lab/cli/commands.hpp>

#include <string>
#include <vector>

int main(const int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return rollback_lab::run_cli(arguments);
}

