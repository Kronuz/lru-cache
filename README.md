# lru-cache (`lru`)

A header-only LRU cache for C++17: an intrusive doubly-linked list threaded through nodes stored in a `std::unordered_map`, with optional TTL eviction.

## What it is

`lru::lru<Key, T, ...>` is a capacity-bounded, header-only cache that gives O(1) lookup and O(1) LRU reordering by keeping each value in a `std::unordered_map` and threading an intrusive doubly-linked list through the map's nodes. The list tracks recency, the map tracks identity, and the two stay in sync. An optional second age-ordered list (`aging_lru`) adds time-based (TTL) eviction. Eviction and access are expressed through policy callbacks, so one container can behave as plain LRU, renew-on-touch, expire-on-age, or a custom mix.

## When to use it / when not

Use it when you want a small, dependency-free cache with predictable O(1) operations, when you need TTL eviction alongside LRU, or when you want to drive eviction from your own policy via callbacks rather than being locked into one strategy.

Don't reach for it when you need a thread-safe cache out of the box (it has no internal locking; wrap it yourself), when you need exact-capacity invariants at every instant (see the trim-before-insert caveat below), or when a plain `std::unordered_map` already covers you and you don't need recency or expiry at all.

## Install

Header-only. Drop `lru.h` somewhere on your include path and include it:

```cpp
#include "lru.h"
```

The only requirement is a C++17 compiler. No build step, no linking.

With CMake via FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
  lru_cache
  GIT_REPOSITORY https://github.com/Kronuz/lru-cache.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(lru_cache)

target_link_libraries(your_target PRIVATE lru_cache)
```

The `lru_cache` target is an `INTERFACE` library that just adds the include directory and requests `cxx_std_17` (see `CMakeLists.txt`).

## Usage

### Basic capacity-bounded cache

```cpp
#include "lru.h"
#include <string>

// Capacity 2. Pass 0 (the default) for an unbounded cache.
lru::lru<int, std::string> cache(2);

cache.emplace(1, "one");
cache.emplace(2, "two");
cache.emplace(3, "three");   // inserting 3 trims the oldest entry (1)

cache.find(3) != cache.end();   // true, and touching 3 renews it
cache.at(3);                    // "three"; throws std::out_of_range if missing
cache.exists(2);                // true or false without renewing
```

### The policy-callback `*_and` variants

The `*_and` methods take a callback as their first argument. `emplace_and`,
`insert_and`, and `trim_and` take an `OnDrop` callback that returns a
`DropAction`; `find_and` (and `at_and`) take an `OnGet` callback returning a
`GetAction`.

```cpp
using cache_t = lru::lru<int, std::string>;

// Custom eviction: evict overflow and expired entries, otherwise stop scanning.
auto on_drop = [](const cache_t::value_type& kv, bool overflowed, bool expired) {
    if (overflowed || expired) {
        return cache_t::DropAction::evict;
    }
    return cache_t::DropAction::stop;   // nothing more to do, stop the scan
};

cache.emplace_and(on_drop, 42, "forty-two");

// Custom access policy: look without renewing recency.
auto found = cache.find_and(
    [](const cache_t::value_type&, bool, bool) {
        return cache_t::GetAction::leave;   // peek, don't move it to front
    },
    42);

// Convenience wrappers exist for the common cases:
cache.find_and_leave(42);    // peek, no renew
cache.find_and_renew(42);    // move to front of recency
cache.find_and_relink(42);   // move to front and reset the TTL clock
```

### TTL / aging construction

Pass a `max_age` to enable time-based eviction. Use `aging_lru`, which selects
the node type that carries an expiration timestamp and a second age-ordered
list.

```cpp
#include <chrono>
using namespace std::chrono_literals;

// Capacity 1000, entries expire 30 seconds after they were (re)linked.
lru::aging_lru<std::string, std::string> cache(1000, 30s);

cache.emplace("k", "v");

// trim() walks the age-ordered list internally and drops expired entries.
cache.trim();
```

The age-ordered list is exposed through the `iterator_by_age` typedef and is
walked internally by `trim_and` (`lru.h:690`); recency iteration via
`begin()`/`end()` skips expired entries when the node type supports it.

Constructing a non-aging `lru::lru` with a non-zero `max_age` trips an
`assert` (the base node type has no expiration field), so reach for
`aging_lru` whenever you pass a TTL.

## API reference

Class: `lru::lru<Key, T, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>, Node = node<std::pair<const Key, T>, base_node>, Allocator = ...>` (`lru.h:293`).

`aging_lru<...>` is the same template with `Node` defaulted to use
`aging_base_node` (`lru.h:803`).

### Construction

```cpp
lru(size_t max_size = 0, std::chrono::milliseconds max_age = 0);
```

`max_size` of 0 means unbounded (`lru.h:335`). `max_age` of 0 means no TTL.
A non-aging cache with non-zero `max_age` asserts (`lru.h:338`).

### Insertion

- `emplace(args...)` / `insert(value)` — construct/insert with the default drop
  policy: evict overflowed or expired entries, otherwise stop (`lru.h:374`,
  `lru.h:399`). Returns `std::pair<iterator, bool>` where the bool is `true` if
  a new entry was created. If the key already exists and is live, it is renewed
  (moved to front) and the bool is `false` (`lru.h:362`).
- `emplace_and(on_drop, args...)` / `insert_and(on_drop, value)` — same, but you
  supply the `OnDrop` callback that drives trimming (`lru.h:343`, `lru.h:384`).

### Lookup

- `find(key)` — returns an `iterator`; renews the entry on hit, evicts it if it
  was expired (`lru.h:458`). `end()` if absent.
- `find_and(on_get, key)` — look up and let the `OnGet` callback decide what to
  do with the entry (`lru.h:412`).
- `find_and_leave` / `find_and_renew` / `find_and_relink` — fixed-policy
  shorthands (`lru.h:437`).
- `at(key)` — like `find` but throws `std::out_of_range` if absent (`lru.h:509`).
  `at_and` / `at_and_leave` / `at_and_renew` / `at_and_relink` mirror the
  `find` family (`lru.h:500`).
- `operator[](key)` — `get(key)`, which finds or default-constructs the value
  (`lru.h:654`). `get` / `get_and` insert a default when the key is missing
  (`lru.h:585`).
- `exists(key)` — boolean presence check (`lru.h:784`).

### The callbacks

`OnDrop` is called as `on_drop(value_type&, bool overflowed, bool expired)` and
returns a `DropAction` (`lru.h:320`):

- `leave` — keep the entry as is.
- `renew` — move it to the front of the recency list.
- `relink` — move it to the front and reset its TTL.
- `evict` — remove it from the cache.
- `stop` — stop the trim scan immediately.

`OnGet` is called as `on_get(value_type&, bool over_capacity, bool expired)` and
returns a `GetAction` (`lru.h:328`): the same four as `DropAction` minus `stop`
(`leave`, `renew`, `relink`, `evict`).

### Removal

- `erase(key)` — remove by key, returns the count removed (0 or 1) (`lru.h:674`).
- `erase(const_iterator)` — remove by iterator, returns an iterator to the next
  entry (`lru.h:659`).
- `clear()` — drop everything (`lru.h:779`).
- `trim()` / `trim_and(on_drop)` — run the eviction pass on demand (`lru.h:684`,
  `lru.h:746`).

### Iteration

- `begin()` / `end()` (and `cbegin()` / `cend()`) iterate in recency order, most
  recent first (`lru.h:763`). On an `aging_lru`, the iterator skips expired
  entries as it advances (`lru.h:238`).
- Iterate-by-age is available through the `iterator_by_age` / `const_iterator_by_age`
  typedefs (`lru.h:312`), constructed as `iterator_by_age(this, &this->_end)`.
  These walk the second, age-ordered list that only `aging_base_node` maintains
  (`lru.h:267`). `trim_and` uses this to scan oldest-first (`lru.h:690`); there is
  no public `begin()` overload that returns one, so external age-ordered traversal
  means constructing the typedef directly.

### Capacity

- `size()` / `empty()` / `max_size()` (`lru.h:789`).

## Build & test

Header-only, so there is nothing to build for use. To run the smoke test:

```sh
c++ -std=c++17 -I. test/test.cc -o test/test && ./test/test
```

Or via CMake:

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

Expected output: `lru-cache OK: oldest evicted, 3 live entries (cap 2)`. The
"3 live entries" with capacity 2 is the trim-before-insert behavior in action
(see below), not a bug. The build emits one `-Wdeprecated-declarations`
warning from `std::iterator`; that is expected too.

## Notes & caveats

- Trim-before-insert: the cache trims to capacity at the start of each insert,
  not the end. Right after an overflowing insert the cache can transiently hold
  `capacity + 1` entries until the next operation trims again. The smoke test
  asserts the live count is between 2 and 3 for this reason (`test/test.cc:24`,
  `lru.h:345`).
- The iterator derives from `std::iterator`, which is deprecated in C++17
  (`lru.h:59`). It compiles, but emits a `-Wdeprecated-declarations` warning.
  This is a real modernization TODO: replace the base class with explicit
  iterator typedefs.
- No internal synchronization. The cache is not thread-safe; guard it with your
  own lock if shared across threads.
- Iterators are invalidated by operations that erase the underlying map node
  (`erase`, an evicting `find`, a trim that evicts). Treat them like
  `std::unordered_map` iterators.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/lru_cache_demo
```

It builds a capacity-3 cache and prints the cache state in recency order
(least-recently-used end first, most-recently-used last) after each operation:
filling to capacity, a `find` hit that renews a key (and watching it survive
past a neighbour because of it), a miss returning `end()`, an `exists` peek that
does not renew, an over-capacity insert that leaves the cache at `cap + 1` until
the next insert's trim evicts the LRU entry (the trim-before-insert behavior),
`at()` throwing `std::out_of_range` on the evicted key, and `operator[]`
reassigning an existing key versus default-constructing a new one. So you can
watch recency drive who lives and who gets evicted.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand), where it caches
schemas, compiled scripts, and similar derived data.

## License

MIT. Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
