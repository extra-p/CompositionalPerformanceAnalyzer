#pragma once 

#include <chrono>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>

uint64_t getTimeNow() noexcept;

uint64_t getProcessorNow() noexcept;

template <typename T>
T getDuration(const T& begin, const T& end) noexcept {
	return end - begin;
}

std::tuple<char*, unsigned long long> getFileData(std::string filename);

std::tuple<char*, unsigned long long> loadFile(std::string path);

