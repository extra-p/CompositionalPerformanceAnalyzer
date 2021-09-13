#pragma once

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <set>
#include <fstream>

typedef long long int timing_t;


class extrap_writer {
private:
	std::map<std::string, std::map<timing_t, std::map<timing_t, std::vector<timing_t>>>> data_points;
	std::string path;

	std::set<timing_t> thread_sizes;
	std::set<timing_t> array_sizes;
	std::set<std::string> names;

public:
	extrap_writer(std::string path_to_file) {
		path = path_to_file;
	}

	void add_data_point(std::string str, timing_t thread_count, timing_t array_size, timing_t data_point) {
		thread_sizes.emplace(thread_count);
		array_sizes.emplace(array_size);
		names.emplace(str);

		data_points[str][thread_count][array_size].emplace_back(data_point);
	}

	void add_data_points(std::string str, timing_t thread_count, timing_t array_size, std::vector<timing_t>& points) {
		thread_sizes.emplace(thread_count);
		array_sizes.emplace(array_size);
		names.emplace(str);

		for (timing_t point : points) {
			data_points[str][thread_count][array_size].emplace_back(point);
		}
	}

	void clear() {
		thread_sizes.clear();
		array_sizes.clear();
		names.clear();

		data_points.clear();
	}

	void flush() {
		for (auto& it0 : names) {
			std::ofstream out_stream_full;

			out_stream_full.open(path + it0 /*+ std::string("_") + std::to_string(MAXI_SCALE)*/ + ".txt");

			out_stream_full << "PARAMETER n" << std::endl;
			out_stream_full << "POINTS";

			for (auto& it : array_sizes) {
				out_stream_full << " " << it;
			}

			out_stream_full << std::endl << std::endl;

			for (auto& it : data_points[it0]) {
				out_stream_full << "REGION metric_" << it0 << "_" << it.first << std::endl;

				for (auto& it2 : it.second) {
					out_stream_full << "DATA";

					auto& vector = it2.second;
					auto length = vector.size();

					for (auto ctr = 0; ctr < length; ctr++) {
						out_stream_full << " " << vector[ctr];
					}

					out_stream_full << std::endl;
				}
			}

			out_stream_full.flush();
			out_stream_full.close();
		}
	}
};