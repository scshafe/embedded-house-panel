#include "vocabulary_panel/study_models.h"
#include <array>
#include <fstream>
#include <stdexcept>
namespace vocabulary_panel
{
std::string new_study_uuid()
{
	std::array<unsigned char, 16> bytes{};
	std::ifstream entropy("/dev/urandom", std::ios::binary);
	if (!entropy.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
		throw std::runtime_error("STUDY.ENTROPY_UNAVAILABLE");
	bytes[6] = (bytes[6] & 15) | 64;
	bytes[8] = (bytes[8] & 63) | 128;
	constexpr char hex[] = "0123456789abcdef";
	std::string result;
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			result += '-';
		result += hex[bytes[i] >> 4];
		result += hex[bytes[i] & 15];
	}
	return result;
}
} // namespace vocabulary_panel
