#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace font_fixtures {

constexpr std::string_view supported_text[] = {
    "House panel / 07:42\nFlip card", "Café — déjà vu…", "Łódź • Őrség – Straße",
    "‘Tomorrow’ and “today”", "20°C / 10 € / 6′ 2″ / ™", "\u00a0"
};

struct RejectedText {
    std::string_view text;
    std::size_t offset;
    std::uint32_t scalar;
};

constexpr RejectedText unsupported_text[] = {
    {"word 漢", 5, 0x6f22}, {"word 😀", 5, 0x1f600},
    {"e\u0301", 1, 0x0301}, {"word\t", 4, 0x09}, {"word\x7f", 4, 0x7f}
};

constexpr std::string_view malformed_text[] = {
    "\xc0\xaf", "\xe2\x80", "\xf4\x90\x80\x80", "\xed\xa0\x80", "\xe2!x"
};

constexpr std::uint32_t unsupported_scalars[] = {0x6f22, 0x1f600, 0x0301};

constexpr std::string_view supported_clock_text[] = {"07:42", "23:59", "0123456789:"};
constexpr RejectedText unsupported_clock_text[] = {
    {"07:42 AM", 5, 0x20}, {"07:42\n", 5, 0x0a}, {"é", 0, 0x00e9},
    {"A", 0, 0x41}, {"07–42", 2, 0x2013}, {"\u00a0", 0, 0x00a0}
};
constexpr std::string_view clock_metric_text[] = {"07:42", "23:59", "88:88"};

} // namespace font_fixtures
