#include "Globals.hpp"
#include <cstring>

uint64_t getTimeNow() noexcept {
	auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now());
	auto count = now.time_since_epoch().count();
	return count;
}

uint64_t getProcessorNow() noexcept {
#ifndef WIN32
	unsigned int lo, hi;
	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return (static_cast<uint64_t>(hi) << 32) | lo;
#else
	return getTimeNow();
#endif
}

std::tuple<char*, unsigned long long> getFileData(std::string filename) {
	auto dummy = std::make_tuple(nullptr, -1);

	std::ifstream input(filename, std::ios::in | std::ios::binary | std::ios::ate);
	if (!input.good()) {
		std::cerr << "File is not good!" << std::endl;
		return dummy;
	}

	auto position = input.tellg();

	if (position < 0) {
		std::cerr << "tellg returned < 0!" << std::endl;
		return dummy;
	}

	std::vector<char> characters(position);
	char* raw_data = characters.data();

	input.seekg(0, std::ios::beg);
	input.read(raw_data, position);

	if ((characters[0] != 'B') || (characters[1] != 'M')) {
		std::cerr << "File didn't start with BM! It started with: " << characters[0] << characters[1] << std::endl;
		return dummy;
	}

	unsigned short bpp;
	std::memcpy(&bpp, raw_data + 28, sizeof(unsigned short));

	if (bpp != 24) {
		std::cerr << "bpp is not 24! It is " << bpp << std::endl;
		return dummy;
	}

	unsigned int comp;
	std::memcpy(&comp, raw_data + 30, sizeof(unsigned int));

	if (comp != 0) {
		std::cerr << "comp is not 0! It is: " << comp << std::endl;
		return dummy;
	}

	unsigned int offset;
	std::memcpy(&offset, raw_data + 10, sizeof(unsigned int));

	auto len = characters.size() - offset;

	char* raw_ptr = new char[len];

	std::memcpy(raw_ptr, characters.data() + offset, len);

	return std::make_tuple(raw_ptr, len);
}

std::tuple<char*, unsigned long long> loadFile(std::string path) {
	auto dummy = std::make_tuple(nullptr, -1);

	if (!std::filesystem::exists(path)) {
		std::cout << "Path: " << path << " doesn't exist!" << std::endl;
		return dummy;
	}

	if (std::filesystem::is_empty(path)) {
		std::cout << "The path: " << path << " is empty." << std::endl;
		return dummy;
	}

	for (const auto& entry : std::filesystem::directory_iterator(path)) {
		auto str = entry.path().string();

		auto tuple = getFileData(str);

		if (std::get<0>(tuple) == nullptr) {
			continue;
		}

		return tuple;
	}

	return dummy;
}
