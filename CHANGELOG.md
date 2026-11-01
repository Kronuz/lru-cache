# Changelog

All notable changes to `lru-cache`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-11-01

### Added

- `bench/bench.cc` (`lru_cache_bench`, `-DLRU_CACHE_BUILD_BENCH=ON`): `lru::lru` against the
  textbook LRU, a `std::list` of entries and a `std::unordered_map` from key to list iterator,
  on `find_and_renew` and `emplace` on a miss, with skewed `uint64_t` keys at capacities of
  1024, 65536 and 2^20 and string keys at 65536. The textbook cache is built with the capacity
  plus one, because `emplace` trims before it inserts, and the hit counts of the two must agree.
