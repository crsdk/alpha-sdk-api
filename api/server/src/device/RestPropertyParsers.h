#ifndef REST_PROPERTY_PARSERS_H
#define REST_PROPERTY_PARSERS_H

// RestPropertyParsers — MIT-licensed REST value <-> SDK helpers.
//
// These string-to-SDK parsers and human-readable formatters were added by the
// REST server on top of Sony's stock PropertyValueTable. They are original
// (non-sample) code and live here in api/server so the public repo keeps only
// the stock PropertyValueTable in shared/core. They are declared in namespace
// `cli` so existing CameraWebController call sites (`cli::parse_*`,
// `cli::format_*`) resolve unchanged. The string parsers are overloads of the
// stock buffer-based parsers (distinct signatures — no conflict).

#include <cstdint>
#include <string>
#include <vector>

#include "Text.h"  // stock: text / text_stringstream

namespace cli {

// Buffer parser for the property's available-values array (REST addition;
// stock has the other parse_*(buf, nval) variants but not this one).
std::vector<std::uint16_t> parse_still_image_store_destination(unsigned char const* buf,
                                                               std::uint32_t nval);

// Human-readable formatters (REST additions).
text format_file_type(std::uint16_t file_type);
text format_still_image_quality(std::uint32_t image_quality);
text format_raw_file_compression(std::uint16_t raw_compression);
text format_still_image_store_destination(std::uint16_t store_destination);

// String -> SDK value parsers (REST additions; throw std::invalid_argument on
// unknown input).
std::uint16_t parse_position_key_setting(const std::string& value);
std::uint16_t parse_still_image_store_destination(const std::string& value);
std::uint16_t parse_white_balance(const std::string& value);
std::uint16_t parse_focus_mode(const std::string& value);
std::uint16_t parse_focus_area(const std::string& value);
std::uint16_t parse_raw_file_compression(const std::string& value);
std::uint16_t parse_file_type(const std::string& value);
std::uint32_t parse_exposure_program_mode(const std::string& value);
std::uint32_t parse_still_capture_mode(const std::string& value);
std::uint32_t parse_shutter_speed(const std::string& value);
// Accepts either a bare quality ("fine") or a file-type-qualified form
// ("raw+jpeg fine"), returning (fileType << 16) | quality for the latter.
std::uint32_t parse_still_image_quality(const std::string& value);

}  // namespace cli

#endif  // REST_PROPERTY_PARSERS_H
