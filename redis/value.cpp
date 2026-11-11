#include "value.h"

#include <charconv>

namespace redis_plugin {
namespace {
[[noreturn]] void bad_utf8() {
    throw Error("invalid_utf8", "Value is not lossless UTF-8; use REDIS Get blob.", "received");
}
}

std::string utf8(std::u16string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t c = text[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                throw Error("invalid_arguments", "Text contains an unpaired UTF-16 surrogate.");
            c = 0x10000 + ((c - 0xd800) << 10) + (text[i] - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            throw Error("invalid_arguments", "Text contains an unpaired UTF-16 surrogate.");
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::u16string utf16(std::string_view bytes) {
    std::u16string out;
    for (std::size_t i = 0; i < bytes.size();) {
        auto b = static_cast<unsigned char>(bytes[i++]);
        std::uint32_t c;
        int extra;
        if (b < 0x80) { c = b; extra = 0; }
        else if (b >= 0xc2 && b <= 0xdf) { c = b & 0x1f; extra = 1; }
        else if (b >= 0xe0 && b <= 0xef) { c = b & 0xf; extra = 2; }
        else if (b >= 0xf0 && b <= 0xf4) { c = b & 7; extra = 3; }
        else bad_utf8();
        if (bytes.size() - i < static_cast<std::size_t>(extra)) bad_utf8();
        for (int j = 0; j < extra; ++j) {
            auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) bad_utf8();
            c = (c << 6) | (next & 0x3f);
        }
        if ((extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) ||
            (extra == 3 && c < 0x10000) || c > 0x10ffff ||
            (c >= 0xd800 && c <= 0xdfff)) bad_utf8();
        if (c < 0x10000) out += static_cast<char16_t>(c);
        else {
            c -= 0x10000;
            out += static_cast<char16_t>(0xd800 | (c >> 10));
            out += static_cast<char16_t>(0xdc00 | (c & 0x3ff));
        }
    }
    return out;
}

std::int64_t decimal(std::string_view text) {
    std::int64_t value = 0;
    if (text.empty())
        throw Error("invalid_arguments", "An exact signed decimal int64 Text is required.");
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec == std::errc::result_out_of_range)
        throw Error("integer_overflow", "Decimal Text is outside the Redis signed int64 range.");
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        std::to_string(value) != text)
        throw Error("invalid_arguments", "Use canonical decimal int64 Text (no spaces, plus or leading zeros).");
    return value;
}

void positive_decimal(std::string_view text) {
    if (decimal(text) <= 0)
        throw Error("invalid_arguments", "Milliseconds must be positive.");
}
}
