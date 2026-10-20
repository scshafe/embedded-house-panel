#pragma once
#include "voice_lab_recorder/result.h"
#include <nlohmann/json.hpp>
#include <string>

/**
 * @canonical voice_lab_canonical_json -- voice-lab's canonical JSON, byte for byte.
 *
 * corpus-client.v1 PROTOCOL.md section 8: object keys sorted by code point
 * (UTF-8 byte order), no whitespace, text unescaped beyond JSON's own escapes
 * (control characters as \b \f \n \r \t or \u00xx, DEL raw), integers as
 * integers, and floats as Python's repr: the shortest round-trip digits, in
 * fixed notation for decimal exponents from -4 to 15 and exponent notation
 * outside them, always with a "." or an exponent. NaN and infinities are
 * refused (VOICE_LAB.CANONICAL_JSON_INVALID). nlohmann's own dump differs for
 * floats, so floats are formatted here.
 */
namespace voice_lab_recorder
{
/** The canonical text of `value`, without a final newline. */
Result<std::string> canonical_json(const nlohmann::json &value);
/** A record's bytes: the canonical text and one "\n". */
Result<std::string> canonical_record(const nlohmann::json &value);
/** Python's repr of a finite double. */
std::string python_float_repr(double value);
} // namespace voice_lab_recorder
