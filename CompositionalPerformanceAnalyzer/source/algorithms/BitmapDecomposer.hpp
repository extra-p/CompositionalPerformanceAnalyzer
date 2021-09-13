#pragma once

#include "../interfaces/AlgorithmInterface.hpp"
#include "../Commons.hpp"

#include <tuple>
#include <vector>
#include <string>
#include <list>
#include <fstream>
#include <sstream>
#include <cstring>

class BitmapDecomposer : public AlgorithmInterface<std::string, std::vector<std::tuple<histogram, RGB>>> {
protected:
	std::vector<std::tuple<histogram, RGB>> Compute(std::string&& filename) const override {
		std::vector<std::tuple<histogram, RGB>> result;
		std::ifstream input(filename, std::ios::in | std::ios::ate | std::ios::binary);

		if (!input.good()) {
			return result;
		}

		std::ifstream::pos_type position = input.tellg();

		if (position < 0) {
			return result;
		}

		std::vector<char> characters(position);
		char* raw_data = characters.data();

		input.seekg(0, std::ios::beg);
		input.read(raw_data, position);

		if (characters[0] != 'B' || characters[1] != 'M') {
			return result;
		}

		unsigned short bpp;
		std::memcpy(&bpp, raw_data + 28, sizeof(unsigned short));

		if (bpp != 24) {
			return result;
		}

		unsigned int comp;
		std::memcpy(&comp, raw_data + 30, sizeof(unsigned int));

		if (comp != 0) {
			return result;
		}

		unsigned int offset;
		std::memcpy(&offset, raw_data + 10, sizeof(unsigned int));

		std::vector<size_t> blue_histogram(256);
		std::vector<size_t> green_histogram(256);
		std::vector<size_t> red_histogram(256);

		const char* const pixel = raw_data + offset;

		for (int i = 0; i < characters.size() - offset; i += 3) {
			unsigned char blue = static_cast<unsigned char>(pixel[i]);
			unsigned char green = static_cast<unsigned char>(pixel[i + 1]);
			unsigned char red = static_cast<unsigned char>(pixel[i + 2]);

			blue_histogram[blue]++;
			green_histogram[green]++;
			red_histogram[red]++;
		}

		auto blue = std::make_tuple(std::move(blue_histogram), RGB::B);
		auto green = std::make_tuple(std::move(green_histogram), RGB::G);
		auto red = std::make_tuple(std::move(red_histogram), RGB::R);

		result.emplace_back(std::move(blue));
		result.emplace_back(std::move(green));
		result.emplace_back(std::move(red));

		return result;
	}

public:
	BitmapDecomposer() = default;

	BitmapDecomposer(BitmapDecomposer& other) = default;
	BitmapDecomposer(BitmapDecomposer&& other) = default;

	BitmapDecomposer& operator=(const BitmapDecomposer& other) = default;
	BitmapDecomposer& operator=(BitmapDecomposer&& other) = default;

	virtual ~BitmapDecomposer() = default;

	std::string Name() const override {
		return std::string("bitmapDecomposer");
	}
};

class BitmapDecomposerRaw : public AlgorithmInterface<std::tuple<char*, unsigned long long>, std::map<int, size_t>> {
public:
	BitmapDecomposerRaw() = default;

	BitmapDecomposerRaw(BitmapDecomposerRaw& other) = default;
	BitmapDecomposerRaw(BitmapDecomposerRaw&& other) = default;

	BitmapDecomposerRaw& operator=(const BitmapDecomposerRaw& other) = default;
	BitmapDecomposerRaw& operator=(BitmapDecomposerRaw&& other) = default;

	virtual ~BitmapDecomposerRaw() = default;

	std::map<int, size_t> Compute(std::tuple<char*, unsigned long long>&& input) const override {
		auto raw_data = std::get<0>(input);
		auto size = std::get<1>(input);

		std::vector<size_t> vector(768);
		using st = std::vector<size_t>::size_type;

		for (auto i = 0; i < size; i += 3) {
			unsigned char b = raw_data[i];
			unsigned char g = raw_data[i + 1];
			unsigned char r = raw_data[i + 2];

			const auto b_idx = static_cast<st>(b);
			const auto g_idx = static_cast<st>(g) + 256;
			const auto r_idx = static_cast<st>(r) + 512;

			vector[b_idx] += 1;
			vector[g_idx] += 1;
			vector[r_idx] += 1;
		}

		std::map<int, size_t> result;

		for (auto i = 0; i < 768; i++) {
			result.emplace(i, std::move(vector[i]));
		}

		return result;
	}

	std::string Name() const override {
		return std::string("BitmapDecomposerRaw");
	}
};

class BitmapDecomposerRawVector : public AlgorithmInterface<std::tuple<char*, unsigned long long>, std::map<int, std::vector<size_t>>> {
public:
	BitmapDecomposerRawVector() = default;

	BitmapDecomposerRawVector(BitmapDecomposerRawVector& other) = default;
	BitmapDecomposerRawVector(BitmapDecomposerRawVector&& other) = default;

	BitmapDecomposerRawVector& operator=(const BitmapDecomposerRawVector& other) = default;
	BitmapDecomposerRawVector& operator=(BitmapDecomposerRawVector&& other) = default;

	virtual ~BitmapDecomposerRawVector() = default;

	std::map<int, std::vector<size_t>> Compute(std::tuple<char*, unsigned long long>&& input) const override {		
		auto raw_data = std::get<0>(input);
		auto size = std::get<1>(input);

		std::vector<size_t> vector(768);
		using st = std::vector<size_t>::size_type;

		for (auto i = 0; i < size; i += 3) {
			unsigned char b = raw_data[i];
			unsigned char g = raw_data[i + 1];
			unsigned char r = raw_data[i + 2];

			const auto b_idx = static_cast<st>(b);
			const auto g_idx = static_cast<st>(g) + 256;
			const auto r_idx = static_cast<st>(r) + 512;

			vector[b_idx]++;
			vector[g_idx]++;
			vector[r_idx]++;
		}

		std::map<int, std::vector<size_t>> result;

		for (auto i = 0; i < 768; i++) {
			result.emplace(i, std::vector<size_t>(vector[i], 1));
		}

		return result;
	}

	std::string Name() const override {
		return std::string("BitmapDecomposerRawVector");
	}
};
