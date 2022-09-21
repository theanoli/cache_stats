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
			{"dram_hits", {}},
			{"dram_misses", {}},
		};
	}

	Counter last_reads; 
	Counter last_hits; 
	Counter last_inserts; 
	size_t last_bytes_written = 0; 

	std::vector<double> segment_bhr; 
	std::vector<double> segment_ohr; 
	std::vector<double> segment_inserts; 

	void collect_periodic_stats() {
		auto bytes_read = counters["total_reads"].byte_counter; 
		auto objs_read = counters["total_reads"].object_counter; 

		auto bytes_hit = counters["total_hits"].byte_counter;
		auto objs_hit = counters["total_hits"].object_counter;

		double bhr = 0;
		if (bytes_read - last_reads.byte_counter) {
			bhr = (double)(bytes_hit - last_hits.byte_counter)
				/(bytes_read - last_reads.byte_counter); 
		}

		double ohr = 0;
	    if (objs_read - last_reads.object_counter) {
	    	   (double)(objs_hit - last_hits.object_counter)
	     		/(objs_read - last_reads.object_counter); 
	    }

		segment_bhr.push_back(bhr);
		segment_ohr.push_back(ohr);

		last_reads = counters["total_reads"];
		last_hits = counters["total_hits"]; 

		auto inserts = counters["inserts"].byte_counter;
		segment_inserts.push_back(inserts);
		last_inserts = counters["inserts"]; 
	}

	void print_periodic_stats() {
		std::cout << "\tSegment BHR: " << segment_bhr.back() << ", overall " 
			<< (double)counters["total_hits"].byte_counter/counters["total_reads"].byte_counter
			<< "\n\tSegment OHR: " << segment_ohr.back() << ", overall " 
			<< (double)counters["total_hits"].object_counter/counters["total_reads"].object_counter; 
		std::cout << std::endl;
	}

	void on_miss(osize_t osize) {
		counters["total_misses"].increment(osize);
	}

	void on_insert(osize_t osize) {
		counters["inserts"].increment(osize);
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

		str += "\"segment_byte_miss_ratio\": ["; 
		for (size_t i = 0; i < segment_bhr.size() - 1; ++i) {
			str += std::to_string(1-segment_bhr[i]) + ", "; 
		}
		str += std::to_string(segment_bhr.back()) + "],\n"; 

		str += "\"segment_obj_miss_ratio\": ["; 
		for (size_t i = 0; i < segment_ohr.size() - 1; ++i) {
			str += std::to_string(1-segment_ohr[i]) + ", "; 
		}
		str += std::to_string(segment_ohr.back()) + "]\n"; 

		str += "}"; 
		return str;
	}
};
