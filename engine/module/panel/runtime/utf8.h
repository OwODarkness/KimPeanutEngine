#ifndef KPENGINE_MODULE_PANEL_UTF8_H
#define KPENGINE_MODULE_PANEL_UTF8_H

#include <cstdint>
#include <string>
#include <string_view>

namespace kpengine::panel
{
    inline constexpr char32_t kReplacementCodepoint = 0xFFFDu;

    // Decodes UTF-8 into codepoints. A malformed sequence yields U+FFFD, and
    // decoding resumes at the byte after the sequence start rather than
    // abandoning the string: one bad byte must not swallow the rest of a line,
    // since a display that loses everything after the error is worse than one
    // that shows replacement characters.
    std::u32string DecodeUtf8(std::string_view utf8);
}

#endif
