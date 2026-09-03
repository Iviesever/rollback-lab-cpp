#include "test_framework.hpp"

#include <rollback_lab/version.hpp>

RL_TEST(version_contract_is_nonzero) {
    RL_CHECK(rollback_lab::kSimulationVersion == 1U);
    RL_CHECK(rollback_lab::kProtocolVersion == 1U);
}

