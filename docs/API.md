# lru-cache API reference

Class: `lru::lru<Key, T, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>, Node = node<std::pair<const Key, T>, base_node>, Allocator = ...>`.

`aging_lru<...>` is the same template with `Node` defaulted to use
`aging_base_node`.

## Construction

```cpp
lru(size_t max_size = 0, std::chrono::milliseconds max_age = 0);
```

`max_size` of 0 means unbounded. `max_age` of 0 means no TTL.
A non-aging cache with non-zero `max_age` asserts.

## Insertion

- `emplace(args...)` / `insert(value)` — construct/insert with the default drop
  policy: evict overflowed or expired entries, otherwise stop. Returns `std::pair<iterator, bool>` where the bool is `true` if
  a new entry was created. If the key already exists and is live, it is renewed
  (moved to front) and the bool is `false`.
- `emplace_and(on_drop, args...)` / `insert_and(on_drop, value)` — same, but you
  supply the `OnDrop` callback that drives trimming.

## Lookup

- `find(key)` — returns an `iterator`; renews the entry on hit, evicts it if it
  was expired. `end()` if absent.
- `find_and(on_get, key)` — look up and let the `OnGet` callback decide what to
  do with the entry.
- `find_and_leave` / `find_and_renew` / `find_and_relink` — fixed-policy
  shorthands.
- `at(key)` — like `find` but throws `std::out_of_range` if absent.
  `at_and` / `at_and_leave` / `at_and_renew` / `at_and_relink` mirror the
  `find` family.
- `operator[](../key)` — `get(key)`, which finds or default-constructs the value. `get` / `get_and` insert a default when the key is missing.
- `exists(key)` — boolean presence check.

## The callbacks

`OnDrop` is called as `on_drop(value_type&, bool overflowed, bool expired)` and
returns a `DropAction`:

- `leave` — keep the entry as is.
- `renew` — move it to the front of the recency list.
- `relink` — move it to the front and reset its TTL.
- `evict` — remove it from the cache.
- `stop` — stop the trim scan immediately.

`OnGet` is called as `on_get(value_type&, bool over_capacity, bool expired)` and
returns a `GetAction`: the same four as `DropAction` minus `stop`
(`leave`, `renew`, `relink`, `evict`).

## Removal

- `erase(key)` — remove by key, returns the count removed (0 or 1).
- `erase(const_iterator)` — remove by iterator, returns an iterator to the next
  entry.
- `clear()` — drop everything.
- `trim()` / `trim_and(on_drop)` — run the eviction pass on demand.

## Iteration

- `begin()` / `end()` (and `cbegin()` / `cend()`) iterate in recency order, least
  recently used first (the eviction end) and most recently used last.
  On an `aging_lru`, the iterator skips expired entries as it advances.
- Iterate-by-age is available through the `iterator_by_age` / `const_iterator_by_age`
  typedefs, constructed as `iterator_by_age(this, &this->_end)`.
  These walk the second, age-ordered list that only `aging_base_node` maintains. `trim_and` uses this to scan oldest-first; there is
  no public `begin()` overload that returns one, so external age-ordered traversal
  means constructing the typedef directly.

## Capacity

- `size()` / `empty()` / `max_size()`.

## Notes & caveats

- Trim-before-insert: the cache trims to capacity at the start of each insert,
  not the end. Right after an overflowing insert the cache can transiently hold
  `capacity + 1` entries until the next operation trims again. The smoke test
  asserts the live count is between 2 and 3 for this reason.
- The iterator derives from `std::iterator`, which is deprecated in C++17. It compiles, but emits a `-Wdeprecated-declarations` warning.
  This is a real modernization TODO: replace the base class with explicit
  iterator typedefs.
- No internal synchronization. The cache is not thread-safe; guard it with your
  own lock if shared across threads.
- Iterators are invalidated by operations that erase the underlying map node
  (`erase`, an evicting `find`, a trim that evicts). Treat them like
  `std::unordered_map` iterators.
