// A runnable tour of lru-cache.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/lru_cache_demo
//
// The one idea worth taking away: this is a capacity-bounded cache where
// every lookup also reorders recency. A hit on a key splices it to the
// most-recently-used end, so the entry you touch most recently is the entry
// that survives the longest. When the cache is over capacity, the entry at the
// least-recently-used end is the one that gets evicted, and that trim runs at
// the START of the next insert (not the end), so the cache can carry one extra
// entry transiently. This demo fills a small cache, prints its state after
// each operation, shows a hit renewing an entry past an otherwise-doomed
// neighbour, and watches the LRU entry fall off when capacity is exceeded.
#include <cstdio>
#include <string>

#include "lru.h"

using cache_t = lru::lru<int, std::string>;

static void rule(const char* title) {
	std::printf("\n\033[1m-- %s --\033[0m\n", title);
}

// Print the cache in recency order. begin() walks from the least-recently-used
// end toward the most-recently-used end, so the leftmost entry is the next to
// be evicted and the rightmost is the freshest. A find/at hit, or a renewing
// insert, splices the touched entry to the right end.
static void dump(const char* label, cache_t& cache) {
	std::printf("  %-14s [LRU, next out] ", label);
	bool first = true;
	for (auto it = cache.begin(); it != cache.end(); ++it) {
		std::printf("%s%d=%s", first ? "" : " -> ", it->first, it->second.c_str());
		first = false;
	}
	std::printf(" [MRU]   (size %zu / cap %zu)\n",
		cache.size(), cache.max_size());
}

int main() {
	std::puts("lru-cache demo  (capacity 3, keys are ints, values are strings)");

	cache_t cache(3);

	// --- 1. put: fill the cache to capacity ----------------------------------
	rule("emplace() puts entries in, freshest at the MRU end");
	cache.emplace(1, "one");
	dump("put 1=one", cache);
	cache.emplace(2, "two");
	dump("put 2=two", cache);
	cache.emplace(3, "three");
	dump("put 3=three", cache);
	std::puts("  (1 is the least recently used and first in line to be evicted)");

	// --- 2. get: a hit renews recency, a miss returns end() ------------------
	rule("find() is a hit-or-miss lookup; a hit splices the key to the MRU end");
	auto hit = cache.find(1);
	std::printf("  find(1)  -> HIT, value \"%s\"\n", hit->second.c_str());
	dump("after hit 1", cache);
	std::puts("  (touching 1 renewed it; now 2 is the least-recently-used)");

	auto miss = cache.find(99);
	std::printf("  find(99) -> %s\n", miss == cache.end() ? "MISS (end())" : "hit?!");

	// exists() peeks without renewing, so recency order is unchanged after it.
	std::printf("  exists(3) = %s, exists(99) = %s   (peek, no renew)\n",
		cache.exists(3) ? "true" : "false",
		cache.exists(99) ? "true" : "false");
	dump("after peeks", cache);

	// --- 3. eviction: trim-before-insert drops the LRU entry -----------------
	rule("an over-capacity insert evicts the LRU entry, on the NEXT trim");
	std::puts("  current LRU is key 2, so it is the entry on the chopping block");
	cache.emplace(4, "four");
	dump("put 4=four", cache);
	std::puts("  note size is 4 with cap 3: trim runs at the START of an insert,");
	std::puts("  so the overflow is carried until the next insert trims it");
	cache.emplace(5, "five");
	dump("put 5=five", cache);
	std::printf("  find(2)  -> %s   (key 2 was the LRU, trimmed before 5 went in)\n",
		cache.find(2) == cache.end() ? "MISS (evicted)" : "still here?!");
	std::puts("  (key 1 survived because the earlier hit renewed it past key 2)");

	// --- 4. at(): like find but throws instead of returning end() ------------
	rule("at() renews on hit, throws std::out_of_range on miss");
	std::printf("  at(4)    -> \"%s\"\n", cache.at(4).c_str());
	try {
		cache.at(2);
	} catch (const std::out_of_range&) {
		std::puts("  at(2)    -> threw std::out_of_range (it was evicted)");
	}

	// --- 5. operator[]: find-or-default-construct ----------------------------
	rule("operator[] returns the value, inserting a default if the key is new");
	cache[4] = "FOUR";                           // key 4 exists: assigns in place
	std::printf("  cache[4] = \"FOUR\"  -> \"%s\"   (existing key, reassigned)\n",
		cache.at(4).c_str());
	std::string& fresh = cache[7];               // key 7 is new: default-constructed
	fresh = "seven";
	dump("after [4]/[7]", cache);

	std::puts("\ndone.");
	return 0;
}
