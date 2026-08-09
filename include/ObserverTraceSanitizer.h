// Test-build-only worker-side privacy sanitizer. Never call from packet producers.
#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace beammp::observer {

class TraceSanitizer final {
public:
    [[nodiscard]] std::optional<std::string> Sanitize(const std::int32_t rawPlayer, const std::int32_t rawVehicle, const std::uint64_t acceptedMonoNs, const std::string_view rawPose) {
        if (rawPlayer < 0 || rawVehicle < 0) {
            return std::nullopt;
        }
        const auto input = nlohmann::json::parse(rawPose, nullptr, false);
        if (input.is_discarded() || !input.is_object()) {
            return std::nullopt;
        }
        const auto pos = NumericArray(input, "pos", 3);
        const auto rot = NumericArray(input, "rot", 4);
        const auto vel = NumericArray(input, "vel", 3);
        const auto rvel = NumericArray(input, "rvel", 3);
        if (!pos || !rot || !vel || !rvel) {
            return std::nullopt;
        }

        nlohmann::json output = {
            {"dt_us", acceptedMonoNs / 1000},
            {"player", DenseId(mPlayers, rawPlayer)},
            {"vehicle", DenseId(mVehicles, std::make_pair(rawPlayer, rawVehicle))},
            {"pos", *pos}, {"rot", *rot}, {"vel", *vel}, {"rvel", *rvel},
        };
        if (input.contains("tim") && input["tim"].is_number() && std::isfinite(input["tim"].get<double>())) {
            output["tim"] = input["tim"];
        }
        return output.dump();
    }

private:
    static std::optional<nlohmann::json> NumericArray(const nlohmann::json& input, const char* key, const std::size_t expectedSize) {
        if (!input.contains(key) || !input[key].is_array() || input[key].size() != expectedSize) {
            return std::nullopt;
        }
        nlohmann::json output = nlohmann::json::array();
        for (const auto& value : input[key]) {
            if (!value.is_number()) {
                return std::nullopt;
            }
            const auto number = value.get<double>();
            if (!std::isfinite(number)) {
                return std::nullopt;
            }
            output.push_back(number);
        }
        return output;
    }

    template <typename T>
    static std::int32_t DenseId(std::map<T, std::int32_t>& ids, const T& raw) {
        const auto [iterator, inserted] = ids.emplace(raw, static_cast<std::int32_t>(ids.size()));
        return iterator->second;
    }

    std::map<std::int32_t, std::int32_t> mPlayers;
    std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t> mVehicles;
};

} // namespace beammp::observer
