// Smoke + behavior test for the standalone lru-cache library.
// Build: c++ -std=c++17 -I.. test.cc -o test && ./test
//
// Beyond basic eviction, this exercises the eviction-control surface
// (insert_and + DropAction) and in-place value mutation via at_and. These cases
// were ported from Xapiand's original oldtests/ LRU suite and adapted to this
// library's API; assertions check which keys survive rather than exact sizes, so
// they're robust to the trim-before-insert timing.
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "lru.h"

using Cache = lru::lru<std::string, int>;

// A key is "present" iff at() returns without throwing.
static bool present(Cache& c, const std::string& key) {
	try {
		c.at(key);
		return true;
	} catch (const std::out_of_range&) {
		return false;
	}
}

// Basic eviction: oldest keys leave as new ones arrive, newest survive.
static void test_eviction_order() {
	Cache cache(2);   // capacity 2
	for (int k = 1; k <= 5; ++k) {
		cache.emplace(std::to_string(k), k);
	}
	assert(!present(cache, "1"));   // evicted
	assert(!present(cache, "2"));   // evicted
	assert(present(cache, "5"));    // newest
	assert(cache.at("5") == 5);

	size_t n = cache.size();
	assert(n >= 2 && n <= 3);       // capacity, +1 transiently after overflow
	std::printf("  eviction order: oldest evicted, newest kept (size %zu)\n", n);
}

// insert_and with DropAction::leave keeps items that a plain insert would evict.
// With the cache already over capacity, the next plain insert trims the oldest;
// a leave-returning drop callback must instead keep everyone.
static void test_drop_leave() {
	Cache cache(2);
	cache.insert(std::make_pair("a", 1));
	cache.insert(std::make_pair("b", 2));
	cache.insert(std::make_pair("c", 3));   // size 3, over capacity 2
	cache.insert_and(
		[](const Cache::value_type&, bool /*overflowed*/, bool /*expired*/) {
			return Cache::DropAction::leave;
		},
		std::make_pair("d", 4));
	// leave kept the victim: all four survive, nothing evicted.
	assert(present(cache, "a") && present(cache, "b") &&
	       present(cache, "c") && present(cache, "d"));
	assert(cache.size() == 4);

	// Contrast: a plain insert now trims the oldest entries.
	cache.insert(std::make_pair("e", 5));
	assert(!present(cache, "a") && !present(cache, "b"));   // evicted
	assert(present(cache, "e"));
	std::printf("  DropAction::leave: keeps items a plain insert evicts\n");
}

// at_and hands the callback the live value_type, so its .second can be mutated
// in place (the key .first stays const).
static void test_mutate_in_place() {
	Cache cache(3);
	cache.insert(std::make_pair("a", 111));
	int seen = cache.at_and(
		[](Cache::value_type& v, bool, bool) {
			v.second = 456;                 // mutate the stored value
			return Cache::GetAction::leave;
		},
		"a");
	assert(seen == 456);
	assert(cache.at("a") == 456);           // mutation persisted
	std::printf("  at_and: in-place value mutation persists\n");
}

int main() {
	test_eviction_order();
	test_drop_leave();
	test_mutate_in_place();
	std::printf("lru-cache OK: eviction, DropAction::leave, in-place mutation\n");
	return 0;
}
