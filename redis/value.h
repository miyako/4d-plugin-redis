#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace redis_plugin {

struct Error : std::runtime_error {
    std::string code;
    std::string outcome;
    bool connectionUsable;
    Error(std::string code, std::string message,
          std::string outcome = "not_submitted", bool usable = true)
        : std::runtime_error(std::move(message)), code(std::move(code)),
          outcome(std::move(outcome)), connectionUsable(usable) {}
};

std::string utf8(std::u16string_view text);
std::u16string utf16(std::string_view bytes);
std::int64_t decimal(std::string_view text);
void positive_decimal(std::string_view text);

}
