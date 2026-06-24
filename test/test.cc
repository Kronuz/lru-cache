// Smoke test for the standalone lru-cache library.
// Build: c++ -std=c++17 -I.. test.cc -o test && ./test
#include <cassert>
#include <cstdio>
#include <string>
#include "lru.h"

int main() {
	lru::lru<int, std::string> cache(2);   // capacity 2

	for (int k = 1; k <= 5; ++k) {
		cache.emplace(k, std::to_string(k));
	}

	// The oldest keys are evicted as new ones arrive; the newest survive.
	assert(cache.find(1) == cache.end());  // evicted
	assert(cache.find(2) == cache.end());  // evicted
	assert(cache.find(4) != cache.end());  // recent
	assert(cache.find(5) != cache.end());  // newest
	assert(cache.at(5) == "5");

	// trim runs before each insert, so the live size settles at capacity (+1
	// transiently right after an overflowing insert).
	size_t n = 0;
	for (auto it = cache.begin(); it != cache.end(); ++it) ++n;
	assert(n >= 2 && n <= 3);

	std::printf("lru-cache OK: oldest evicted, %zu live entries (cap 2)\n", n);
	return 0;
}
