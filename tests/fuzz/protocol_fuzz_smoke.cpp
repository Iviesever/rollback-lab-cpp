#include <rollback_lab/core/pcg32.hpp>
#include <rollback_lab/protocol/codec.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    rollback_lab::Pcg32 random{0xF0225EEDU};
    constexpr std::uint32_t iterations = 100'000U;
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        const auto size = static_cast<std::size_t>(random.bounded(1'201U));
        std::vector<std::byte> bytes(size);
        for (auto& byte : bytes) {
            byte = static_cast<std::byte>(random.next_u32() & 0xFFU);
        }
        const auto decoded = rollback_lab::decode_packet(bytes);
        if (decoded.ok()) {
            const auto encoded = rollback_lab::encode_packet(decoded.value());
            if (!encoded.ok() || !rollback_lab::decode_packet(encoded.value()).ok()) {
                std::cerr << "canonical round trip failed at iteration "
                          << iteration << '\n';
                return 1;
            }
        }
    }
    std::cout << "protocol fuzz smoke: " << iterations
              << " bounded inputs, 0 crashes\n";
    return 0;
}

