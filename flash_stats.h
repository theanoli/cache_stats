#ifndef FLASH_STATS_H
#define FLASH_STATS_H

#include "common.h"
#include <numeric>
#include <cmath>

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
			{"flash_inserts", {}},
			{"reinserts", {}},
			{"skipped_copyfwds", {}}, 
			{"skipped_inserts", {}},
			{"total_placements", {}}, 
		};
	}
	size_t containers_erased = 0; 
	size_t containers_written = 0;
	size_t flash_bytes_written = 0;

	double write_amplification;

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
	
	// For WA
	std::vector<size_t> segment_fbw; 
	std::vector<size_t> segment_inserts; 

	void collect_periodic_stats(size_t total_size) {

		segment_inserts.push_back(counters["flash_inserts"].byte_counter - last_inserts.byte_counter); 
		segment_fbw.push_back(flash_bytes_written - last_bytes_written); 

		last_inserts = counters["flash_inserts"]; 
		last_bytes_written = flash_bytes_written; 

		write_amplification = (double)flash_bytes_written/counters["flash_inserts"].byte_counter; 

		segment_util.push_back(total_size);
	}

	void print_periodic_stats() {
		std::cout << "\tSegment utilization: " << segment_util.back() << "\n";
		std::cout << "\tSegment flash bytes written: " << segment_fbw.back() << "\n";
		std::cout << "\tWrite amplification: " << write_amplification << "\n"; 
		std::cout << std::endl;
	}

	/* 
	 *
	 */
	void on_miss(okey_t key, osize_t osize) {
		counters["total_misses"].increment(osize);

		/*
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
		*/
	}

	// Objects written into the cache by the algorithm. 
	// An insert is redundant if the key was already in the cache (this only 
	// happens if the inserts are generated ahead of time). 
	// Evict-pending objects that get re-inserted are counted as algorithm inserts
	// (was_inserted) AND as a redundant insert
	void on_insert_attempt(okey_t key, osize_t osize, 
			bool was_inserted) {

		if (was_inserted) {
			// ...and we actually inserted it... 
			counters["flash_inserts"].increment(osize);

			/*
			if (cached[key][INSERTED]) {
				counters["reinserts"].increment(osize); 
			}

			// The miss that led to this insert should unset the 
			// SKIPPED_INSERT and SKIPPED_CF flags
			cached[key].set(INSERTED);
			*/
		} else {
			// ...or we skipped the insert. 
			/*
			cached[key].set(SKIPPED_INSERT);
			*/
			counters["skipped_inserts"].increment(osize);
		}
	}

	// skipped_copyfwd is for copy-forwards that got pruned
	void on_copyfwd_attempt(okey_t key, osize_t osize, 
			bool was_copied_forward) {
		if (!was_copied_forward) {
			/*
			cached[key].set(SKIPPED_CF);
			*/
			counters["skipped_copyfwds"].increment(osize);
		} else {
			/*
			cached[key].set(CF);
			*/
			counters["copy_forwards"].increment(osize); 
			if (copyfwds[key] < 0xff) {
				copyfwds[key]++; 
			}
		}
	}

	void on_erase(okey_t key, osize_t osize) {
		/*
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
		*/

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

		/*
		if (cached[key][CF]) {
			counters["copyfwd_hits"].increment(osize);
		}

		cached[key].set(READ);
		*/
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

		str += print_segment_data(segment_util, "segment_util") + ",\n"; 
		str += print_segment_data(segment_fbw, "segment_fbw") + ",\n"; 
		str += print_segment_data(segment_inserts, "segment_inserts") + "\n"; 

		str += "}"; 
		return str;
	}

	void increment_custom_counter(std::string counter_name, size_t size)
	{
		counters[counter_name].increment(size);
	}

	// From https://stackoverflow.com/questions/7616511/calculate-mean-and-standard-deviation-from-a-vector-of-samples-in-c-using-boos
	std::pair<double, double>compute_container_stats(std::vector<size_t> const &exptimes)
	{
		// Compute std deviation
		double sum = std::accumulate(exptimes.begin(), exptimes.end(), 0.0);
		double mean = sum/exptimes.size();

		std::vector<double> diff(exptimes.size());
		std::transform(exptimes.begin(), exptimes.end(), diff.begin(), [mean](double x) { return x - mean; });
		double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
		double stddev = std::sqrt(sq_sum/exptimes.size());
		return {mean, stddev}; 
	}
};

#endif  // FLASH_STATS_H
