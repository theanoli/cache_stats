#ifndef FLASH_STATS_H
#define FLASH_STATS_H

#include "common.h"

class FlashStats {
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
	* "copy-forwards": 
	* "reinserts": re-inserted by caching algorithm evictions (CLWA)
	* "skipped_inserts": ...skipped insertion
	*
	* === Bytes written
	* "objects_written"
	* flash_bytes_written: object bytes written + headers, unused space in zones, etc.
	* unused_bytes: overhead in containers that isn't used for anything
	*/
	std::unordered_map<std::string, Counter> counters; 

	/* Bit mappings (if true...): 
	 * INSERTED: was at some point inserted
	 * READ: read since last insertion
	 * SKIPPED_INSERT: was read but skipped for WA
	 * SKIPPED_CF: came up for copyfwd but was evicted
	 * CF: was copied forward since last insert
	 *
	 * When to set to 1: 
	 * INSERTED: when an object is inserted
	 * READ: when a read is a hit
	 * SKIPPED_INSERT: when a read is a miss but is not inserted 
	 * SKIPPED_CF: when an object is skipped for copyforward
	 * CF: when object is copied forward 
	 *
	 * When to reset to 0: 
	 * INSERTED: never
	 * READ: when object is erased
	 * SKIPPED_INSERT: when next insertion occurs
	 * SKIPPED_CF: when next insert attempt occurs; once it is read, it will incur a 
	 * 		copyfwd miss, but sunsequent misses will be because insertion 
	 * 		was skipped for WA
	 * CF: when object is erased
	 */
	enum Bits {
		INSERTED,
		READ,
		SKIPPED_INSERT,
		SKIPPED_CF,
		CF, 
	};

	std::unordered_map<okey_t, std::bitset<8>> cached; 
	std::vector<uint32_t> copyfwd_hist; 
	std::unordered_map<okey_t, uint8_t> copyfwds; 

	int inst_stats_period; 

	FlashStats(int m) 
		: copyfwd_hist(256, 0), inst_stats_period(m) {
		counters = {
			{"total_reads", {}},
			{"total_misses", {}}, 
			{"total_hits", {}},
			{"compulsory_misses", {}},
			{"capacity_misses", {}},
			{"wa_skip_misses", {}}, 
			{"one_hit_misses", {}},
			{"copyfwd_hits", {}}, 
			{"copy_forwards", {}},
			{"inserts", {}},
			{"reinserts", {}},
			{"skipped_copyfwds", {}}, 
			{"skipped_inserts", {}},
			{"total_placements", {}}, 
		};
	}
	size_t containers_erased = 0; 
	size_t containers_written = 0;
	size_t flash_bytes_written = 0;

	Counter last_reads; 
	Counter last_hits; 
	Counter last_inserts; 
	size_t last_bytes_written = 0; 

	/*
	 * Want: 
	 * - X warmup flash bytes written
	 * - X warmup utilization 
	 * - warmup bmr: bytes missed, bytes read
	 * - warmup omr: objects missed, objects read
	 * - warmup wa: flash bytes written, bytes inserted
	 */
	std::vector<size_t> segment_util;
	
	// BMR 
	std::vector<size_t> segment_bytes_missed; 
	std::vector<size_t> segment_bytes_read; 

	// OMR
	std::vector<size_t> segment_objects_missed; 
	std::vector<size_t> segment_objects_read; 
	
	// For WA
	std::vector<size_t> segment_fbw; 
	std::vector<size_t> segment_inserts; 

	void collect_periodic_stats(size_t total_size) {
		auto bytes_read = counters["total_reads"].byte_counter; 
		auto objs_read = counters["total_reads"].object_counter; 

		auto bytes_hit = counters["total_hits"].byte_counter;
		auto objs_hit = counters["total_hits"].object_counter;

		segment_bytes_read.push_back(bytes_read - last_reads.byte_counter);
		segment_bytes_hit.push_back(bytes_hit - last_hits.byte_counter);

		segment_objects_read.push_back(objects_read - last_reads.object_counter);
		segment_objects_hit.push_back(objects_hit - last_hits.object_counter);

		last_reads = counters["total_reads"];
		last_hits = counters["total_hits"]; 

		segment_inserts.push_back(counters["inserts"].byte_counter - last_inserts.byte_counter); 
		segment_fbw.push_back(flash_bytes_written - last_bytes_written); 

		last_inserts = counters["inserts"]; 
		last_bytes_written = flash_bytes_written; 

		segment_util.push_back(total_size);
	}

	void print_periodic_stats() {
		std::cout << "\tSegment BHR: " << segment_bhr.back() << ", overall " 
			<< (double)counters["total_hits"].byte_counter/counters["total_reads"].byte_counter
			<< "\n\tSegment OHR: " << segment_ohr.back() << ", overall " 
			<< (double)counters["total_hits"].object_counter/counters["total_reads"].object_counter
			<< "\n\tSegment WA: " << segment_wa.back() << ", overall " 
			<< (double)flash_bytes_written/counters["inserts"].byte_counter << "\n";

		std::cout << "\tSegment utilization: " << segment_util.back() << "\n";
		std::cout << "\tSegment flash bytes written: " << segment_fbw.back() << "\n";
		std::cout << std::endl;
	}

	/* 
	 *
	 */
	void on_miss(okey_t key, osize_t osize) {
		counters["total_misses"].increment(osize);

		auto it = cached.find(key); 
		bool compulsory_miss = it == cached.end();

		if (compulsory_miss) {
			counters["compulsory_misses"].increment(osize); 
			cached[key] = 0; 
		} else {
			// We've seen this before
			// Check if this was a skipped insertion for WA
			auto flags = it->second; 

			if (flags[SKIPPED_INSERT] || flags[SKIPPED_CF]) {
				// An insert skipped because of redundancy would not
				// be a miss. 
				counters["wa_skip_misses"].increment(osize); 
				
				if (flags[SKIPPED_CF]) {
					// The INSERT bit MUST be set, else something went wrong, 
					// since we can't skip a CF for something never inserted.
					assert(flags[INSERTED]); 
					cached[key].reset(SKIPPED_CF); 
				}
				cached[key].reset(SKIPPED_INSERT);
			} else {
				// This was a capacity miss---we evicted it because there was 
				// no space for it. 
				assert(flags[INSERTED]); 
				counters["capacity_misses"].increment(osize);
			}
		}
	}

	// Objects written into the cache by the algorithm. 
	// An insert is redundant if the key was already in the cache (this only 
	// happens if the inserts are generated ahead of time). 
	// Evict-pending objects that get re-inserted are counted as algorithm inserts
	// (was_inserted) AND as a redundant insert
	void on_insert_attempt(okey_t key, osize_t osize, 
			bool was_inserted, bool was_redundant) {

		if (was_inserted) {
			// ...and we actually inserted it... 
			counters["inserts"].increment(osize);

			if (cached[key][INSERTED]) {
				counters["reinserts"].increment(osize); 
			}

			// The miss that led to this insert should unset the 
			// SKIPPED_INSERT and SKIPPED_CF flags
			cached[key].set(INSERTED);
		} else {
			// ...or we skipped the insert. 
			if (!was_redundant) {
				// Skipped insertion
				cached[key].set(SKIPPED_INSERT);
				counters["skipped_inserts"].increment(osize);
			}
		}
	}

	// skipped_copyfwd is for copy-forwards that got pruned
	void on_copyfwd_attempt(okey_t key, osize_t osize, 
			bool was_copied_forward, 
			bool skipped_copyfwd) {
		if (skipped_copyfwd) {
			cached[key].set(SKIPPED_CF);
			counters["skipped_copyfwds"].increment(osize);
		} else if (was_copied_forward) {
			cached[key].set(CF);
			counters["copy_forwards"].increment(osize); 
			if (copyfwds[key] < 0xff) {
				copyfwds[key]++; 
			}
		}
	}

	void on_erase(okey_t key, osize_t osize) {
		auto it = cached.find(key); 
		assert(it != cached.end()); 
		if (!it->second[INSERTED]) {
			std::cout << "Key: " << key << ", size: " << osize << std::endl;
		}
	   	assert(it->second[INSERTED]); 

		if (!it->second[READ]) {
			counters["one_hit_misses"].increment(osize); 
		}

		uint8_t mask = (1 << CF | 1 << READ);
		cached[key] &= ~mask; 

		// Record the copyforward info for this object and erase
		copyfwd_hist[copyfwds[key]]++; 
		copyfwds.erase(key);  
	}

	void on_container_erase() {
		containers_erased++;
	}

	void on_access(osize_t osize) {
		counters["total_reads"].increment(osize);
	}

	void on_hit(okey_t key, osize_t osize) {
		counters["total_hits"].increment(osize);

		if (cached[key][CF]) {
			counters["copyfwd_hits"].increment(osize);
		}

		cached[key].set(READ);
	}

	void on_evict([[maybe_unused]] okey_t key, 
			[[maybe_unused]] osize_t osize) {
	}

	// I.e., what is written to the medium. 
	// osize is object bytes written, while total_size is the full size of the 
	// write to flash. 
	void on_write(osize_t osize) {
		counters["objects_written"].increment(osize); 
		flash_bytes_written += osize;
	}

	// I.e., when container is closed or flushed to DRAM
	void on_container_flush(size_t unused_capacity) {
		flash_bytes_written += unused_capacity;
		containers_written++;
	}

	void on_zone_insert(osize_t osize) {
		counters["total_placements"].increment(osize);
	}

	std::string print_segment_data(std::vector<size_t> data, std::string name) {
		std::string str = ""; 
		str += "\"" + name + "\": ["; 
		for (size_t i = 0; i < data.size() - 1; ++i) {
			str += std::to_string(data[i]) + ", "; 
		}
		str += std::to_string(data.back()) + "]"; 
		return str;
	}

	std::string dump_counters_as_json() {
		std::string str = "{\n";
		
		for (auto it : counters) {
			str += "\"" + it.first + "\": \n"; 
			str += it.second.to_json(); 
			str += ",\n"; 
		}
		str += "\"flash_bytes_written\": " + std::to_string(flash_bytes_written) + ",\n"; 
		str += "\"containers_erased\": " + std::to_string(containers_erased) + ",\n"; 
		str += "\"containers_written\": " + std::to_string(containers_written) + ",\n"; 

		str += "\"copyfwd_hist\": ["; 
		for (size_t i = 0; i < copyfwd_hist.size() - 1; ++i) {
			str += std::to_string(copyfwd_hist[i]) + ", "; 
		}
		str += std::to_string(copyfwd_hist[copyfwd_hist.size() - 1]) + "],\n"; 

		str += "\"segment_period\": " + std::to_string(inst_stats_period) + ",\n"; 
		str += print_segment_data(segment_util, "segment_util") + "\n"; 

		str += print_segment_data(
				segment_bytes_missed, "segment_bytes_missed") + "\n"; 
		str += print_segment_data(
				segment_bytes_read, "segment_bytes_read") + "\n"; 

		str += print_segment_data(
				segment_objects_missed, "segment_objects_missed") + "\n"; 
		str += print_segment_data(
				segment_objects_read, "segment_objects_read") + "\n"; 

		str += print_segment_data(segment_fbw, "segment_fbw") + "\n"; 
		str += print_segment_data(segment_inserts, "segment_inserts") + "\n"; 

		str += "\"average_occupancy\": " + std::to_string(util_sum/segment_util.size()) + "\n"; 

		str += "}"; 
		return str;
	}
};

#endif  // FLASH_STATS_H
