#include "common.h"

std::string print_segment_data(std::vector<size_t> data, std::string name) {
	std::string str = ""; 
	str += "\"" + name + "\": ["; 
	for (size_t i = 0; i < data.size() - 1; ++i) {
		str += std::to_string(data[i]) + ", "; 
	}
	str += std::to_string(data.back()) + "]"; 
	return str;
}

