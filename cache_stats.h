#include "common.h"

class CacheStats {
public: 
	/*
	* === Various types of misses; first is bytes, second is objects
	* "total_misses": includes all miss types. 
	* "compulsory_misses": first accesses
	* "capacity_misses": misses from objects that got evicted b/c they didn't fit
	* "one_hit_misses": misses on objects not read again
	* "bad_choice_misses": misses on objects that we evicted but the caching 
		* algorithm might have kept
		* (i.e., we forced an eviction on the object)
		* Currently we're not getting any such misses; fill this in later
	*
	* === Various types of hits
	* "total_hits" : includes all hit types. 
	*
	* === Various types of objects & bytes written
	* "inserts": written into the cache by the algorithm
	* "reinserts": re-inserted by caching algorithm evictions (CLWA)
	* "skipped_inserts": ...skipped insertion
	*
	* === Bytes written
	* "objects_written"
	*/
	std::unordered_map<std::string, Counter> counters; 

	int inst_stats_period; 

	CacheStats(int m) 
		: inst_stats_period(m) {
		counters = {
			{"total_reads", {}},
			{"total_misses", {}}, 
			{"total_hits", {}},
			{"inserts", {}},
			{"skipped_inserts", {}}, 
			{"dram_hits", {}},
			{"dram_misses", {}},
		};
	}

	Counter last_reads; 
	Counter last_hits; 
	Counter last_inserts; 
	size_t last_bytes_written = 0; 
	
	// BMR 
	std::vector<size_t> segment_bytes_hit; 
	std::vector<size_t> segment_bytes_read; 

	// OMR
	std::vector<size_t> segment_objects_hit; 
	std::vector<size_t> segment_objects_read; 

	void collect_periodic_stats() {
		auto bytes_read = counters["total_reads"].byte_counter; 
		auto objects_read = counters["total_reads"].object_counter; 

		auto bytes_hit = counters["total_hits"].byte_counter;
		auto objects_hit = counters["total_hits"].object_counter;

		segment_bytes_read.push_back(bytes_read - last_reads.byte_counter);
		segment_bytes_hit.push_back(bytes_hit - last_hits.byte_counter);

		segment_objects_read.push_back(objects_read - last_reads.object_counter);
		segment_objects_hit.push_back(objects_hit - last_hits.object_counter);

		last_reads = counters["total_reads"];
		last_hits = counters["total_hits"]; 
	}

	void print_periodic_stats() {
		std::cout << "\tSegment BHR: " 
			<< (double)segment_bytes_hit.back()/segment_bytes_read.back() 
			<< ", overall " 
			<< (double)counters["total_hits"].byte_counter/counters["total_reads"].byte_counter
			<< "\n\tSegment OHR: " 
			<< (double)segment_objects_hit.back()/segment_objects_read.back() 
			<< ", overall " 
			<< (double)counters["total_hits"].object_counter/counters["total_reads"].object_counter; 
		std::cout << std::endl;
	}

	void on_miss(osize_t osize) {
		counters["total_misses"].increment(osize);
	}

	void on_insert_attempt(osize_t osize, bool was_inserted) {
		if (was_inserted) {
			counters["inserts"].increment(osize);
		} else {
			counters["skipped_inserts"].increment(osize);
		}
	}

	void on_access(osize_t osize) {
		counters["total_reads"].increment(osize);
	}

	void on_hit(osize_t osize) {
		counters["total_hits"].increment(osize);
	}

	void on_dram_hit(osize_t osize) {
		counters["dram_hits"].increment(osize);
	}

	void on_dram_miss(osize_t osize) {
		counters["dram_misses"].increment(osize);
	}

	std::string dump_counters_as_json() {
		std::string str = "{\n";
		
		for (auto it : counters) {
			str += "\"" + it.first + "\": \n"; 
			str += it.second.to_json(); 
			str += ",\n"; 
		}

		str += "\"segment_period\": " + std::to_string(inst_stats_period) + ",\n"; 

		str += print_segment_data(
				segment_bytes_hit, "segment_bytes_hit") + "\n"; 
		str += print_segment_data(
				segment_bytes_read, "segment_bytes_read") + "\n"; 

		str += print_segment_data(
				segment_objects_hit, "segment_objects_hit") + "\n"; 
		str += print_segment_data(
				segment_objects_read, "segment_objects_read") + "\n"; 

		str += "}"; 
		return str;
	}
};
