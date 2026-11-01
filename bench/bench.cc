// Benchmark: lru::lru against the textbook LRU cache (a std::list of entries and a
// std::unordered_map from key to list iterator).
//
// The workload is a cache in front of a slower source: every operation looks a key up (and
// renews it on a hit) and inserts it on a miss. Keys are drawn from a universe twice the capacity,
// skewed towards small values, so about half the lookups hit. ns per operation, best of 5.
//
// lru::lru trims before it inserts, so it holds max_size + 1 entries after an insert; the textbook
// cache is built with the same bound (capacity + 1), and the hit counts of the two must agree.
//
// usage: lru_cache_bench
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lru.h"

static volatile std::size_t sink;

template <class F>
static double best_ns(F&& f, double ops, int reps = 5) {
	f();
	double best = 1e18;
	for (int i = 0; i < reps; ++i) {
		auto a = std::chrono::steady_clock::now();
		f();
		auto b = std::chrono::steady_clock::now();
		best = std::min(best, std::chrono::duration<double, std::nano>(b - a).count() / ops);
	}
	return best;
}

// The structure every tutorial gives: most recent first in a list, a map from key to its place.
template <class K, class V>
class ListMapLru {
  public:
	explicit ListMapLru(std::size_t capacity) : capacity_(capacity) {}

	// Renews and returns the value on a hit.
	V* find(const K& key) {
		auto it = map_.find(key);
		if (it == map_.end()) {
			return nullptr;
		}
		list_.splice(list_.begin(), list_, it->second);
		return &it->second->second;
	}

	void emplace(const K& key, V value) {
		list_.emplace_front(key, std::move(value));
		map_[key] = list_.begin();
		if (map_.size() > capacity_) {
			map_.erase(list_.back().first);
			list_.pop_back();
		}
	}

  private:
	std::size_t capacity_;
	std::list<std::pair<K, V>> list_;
	std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
};

// Hits of the workload on lru::lru (find_and_renew, emplace on a miss).
template <class K, class Key>
static std::size_t run_lru(std::size_t capacity, const std::vector<std::uint32_t>& ops, const Key& key) {
	lru::lru<K, std::uint64_t> cache(capacity);
	std::size_t hits = 0;
	for (auto op : ops) {
		const K& k = key(op);
		if (cache.find_and_renew(k) != cache.end()) {
			++hits;
		} else {
			cache.emplace(k, op);
		}
	}
	return hits;
}

template <class K, class Key>
static std::size_t run_list_map(std::size_t capacity, const std::vector<std::uint32_t>& ops, const Key& key) {
	ListMapLru<K, std::uint64_t> cache(capacity + 1);
	std::size_t hits = 0;
	for (auto op : ops) {
		const K& k = key(op);
		if (cache.find(k)) {
			++hits;
		} else {
			cache.emplace(k, op);
		}
	}
	return hits;
}

template <class K, class Key>
static void row(const char* label, std::size_t capacity, const std::vector<std::uint32_t>& ops, const Key& key) {
	std::size_t h1 = run_lru<K>(capacity, ops, key), h2 = run_list_map<K>(capacity, ops, key);
	double ours = best_ns([&] { sink = run_lru<K>(capacity, ops, key); }, double(ops.size()));
	double reference = best_ns([&] { sink = run_list_map<K>(capacity, ops, key); }, double(ops.size()));
	std::printf("  %-14s cap=%-8zu lru::lru %7.1f ns   list+map %7.1f ns   x%.2f   hits %zu %s\n", label, capacity, ours,
	            reference, reference / ours, h1, h1 == h2 ? "(same)" : "(MISMATCH)");
	std::fflush(stdout);
}

// Keys skewed towards 0: universe * u^2 for u uniform in [0, 1).
static std::vector<std::uint32_t> skewed(std::size_t universe, std::size_t count, std::uint64_t seed) {
	std::mt19937_64 rng(seed);
	std::vector<std::uint32_t> ops(count);
	for (auto& op : ops) {
		double u = double(rng() >> 11) * (1.0 / 9007199254740992.0);
		op = std::uint32_t(double(universe) * u * u);
	}
	return ops;
}

int main() {
	std::printf("find_and_renew, emplace on a miss, skewed keys from twice the capacity\n");
	for (std::size_t capacity : {std::size_t(1024), std::size_t(65536), std::size_t(1) << 20}) {
		auto ops = skewed(capacity * 2, std::size_t(1) << 20, 3);
		row<std::uint64_t>("u64 keys", capacity, ops, [](std::uint32_t op) { return std::uint64_t(op) * 2654435761u; });
	}
	{
		std::size_t capacity = 65536;
		std::vector<std::string> keys(capacity * 2);
		for (std::size_t i = 0; i < keys.size(); ++i) {
			keys[i] = "user:session:" + std::to_string(i * 7919);
		}
		auto ops = skewed(keys.size(), std::size_t(1) << 19, 4);
		row<std::string>("string keys", capacity, ops, [&](std::uint32_t op) -> const std::string& { return keys[op]; });
	}
}
