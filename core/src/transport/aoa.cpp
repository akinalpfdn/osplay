#include "transport/aoa.hpp"

#include <cstddef>
#include <utility>

namespace osplay::transport::aoa {

namespace {

constexpr std::size_t kProtocolAnswerSize = 2;

} // namespace

bool is_accessory(std::uint16_t vendor_id, std::uint16_t product_id) {
    return vendor_id == kGoogleVendorId &&
           (product_id == kAccessoryProductId || product_id == kAccessoryAdbProductId);
}

ControlRequest protocol_request() {
    return {.request_type = kRequestTypeIn,
            .request = kGetProtocol,
            .value = 0,
            .index = 0,
            .data = std::vector<std::uint8_t>(kProtocolAnswerSize)};
}

Expected<std::uint16_t, Error> parse_protocol(std::span<const std::uint8_t> answer) {
    if (answer.size() < kProtocolAnswerSize) {
        return Expected<std::uint16_t, Error>::failure(Error::Truncated);
    }
    const auto version = static_cast<std::uint16_t>(answer[0] | (answer[1] << 8U));
    if (version == 0) {
        return Expected<std::uint16_t, Error>::failure(Error::UnsupportedVersion);
    }
    return version;
}

std::vector<ControlRequest> start_requests() {
    std::vector<ControlRequest> requests;
    requests.reserve(kIdentity.size() + 1);
    std::uint16_t index = 0;
    for (const auto text : kIdentity) {
        std::vector<std::uint8_t> data(text.begin(), text.end());
        data.push_back(0);
        requests.push_back({.request_type = kRequestTypeOut,
                            .request = kSendString,
                            .value = 0,
                            .index = index++,
                            .data = std::move(data)});
    }
    requests.push_back(
        {.request_type = kRequestTypeOut, .request = kStart, .value = 0, .index = 0, .data = {}});
    return requests;
}

} // namespace osplay::transport::aoa
