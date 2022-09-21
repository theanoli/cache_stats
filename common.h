#ifndef STATS_COMMON_H_
#define STATS_COMMON_H_

#include <cassert>
#include <bitset>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

typedef uint32_t okey_t;
typedef uint32_t osize_t;
typedef uint64_t counter_t; 

class Counter {
public: 
	counter_t byte_counter = 0;
	osize_t object_counter = 0;

	void increment(osize_t size) {
		byte_counter += size; 
		object_counter++;	
	}

	std::string to_json() {
		std::string str = "\t{\"bytes\": " + std::to_string(byte_counter) + ",\n" + 
			"\t\"objects\": " + std::to_string(object_counter) + "}"; 
		return str;
	}
};

std::string print_segment_data(std::vector<size_t>, std::string); 

#endif  // STATS_COMMON_H
