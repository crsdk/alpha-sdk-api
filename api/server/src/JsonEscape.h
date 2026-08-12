// =============================================================================
// JsonEscape — escape a raw std::string for embedding inside a JSON string.
//
// The server hand-builds some JSON payloads with ostringstream/string
// concatenation rather than jsoncpp. Any value interpolated into those payloads
// must be escaped, or a single backslash or quote produces a response the
// client cannot parse. Windows host paths (C:\Users\...) are the common case:
// unescaped, "C:\Users" emits the invalid escape \U and the whole response
// fails with "Bad escaped character in JSON".
//
// Header-only so both the controller and the device layer can share one
// implementation; src/ is already on the target's include path.
// =============================================================================
#ifndef JSON_ESCAPE_H
#define JSON_ESCAPE_H

#include <string>

/// Escape `s` for use inside a JSON string literal (per RFC 8259).
/// Bytes >= 0x80 pass through untouched so valid UTF-8 survives intact.
inline std::string jsonEscape(const std::string& s) {
    static const char* kHex = "0123456789abcdef";
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default: {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 0x20) {
                    // Remaining control characters have no shorthand escape.
                    result += "\\u00";
                    result += kHex[(uc >> 4) & 0xF];
                    result += kHex[uc & 0xF];
                } else {
                    result += c;
                }
                break;
            }
        }
    }
    return result;
}

#endif  // JSON_ESCAPE_H
