# Architecture

This document describes the internal design of the `lru` cache. It assumes you
have read the README. File and line references point at `lru.h`.

## The core idea: a map and an intrusive list, kept in sync

A naive LRU cache needs two things at once: O(1) lookup by key, and O(1) "move
this entry to the most-recent position." A `std::unordered_map` gives the first.
A doubly-linked list gives the second. The trick this library uses is to make
them the *same* objects rather than two parallel structures.

Each value lives inside a `node` that is stored by value in the map
(`lru.h:279`). That `node` derives from a `base_node` that carries the linked
list pointers (`_next`, `_prev`). So the list is *intrusive*: it is threaded
directly through the objects the map owns. There is no separate list allocation,
no separate node-to-value indirection, and a map entry and a list element are
one and the same memory.

```
_map: unordered_map<Key, Node>        intrusive list (recency order)
  "a" -> Node{ base_node, data } <----> _prev / _next <---->
  "b" -> Node{ base_node, data } <----> _prev / _next <---->
  "c" -> Node{ base_node, data } <----> _prev / _next <---->
```

Lookup goes through the map (O(1) average). Reordering for recency manipulates
the list pointers in place (O(1)), without touching the map at all. The map
guarantees pointer stability for its values (node-based container), so the list
pointers stay valid as long as the entry lives in the map. That stability is the
load-bearing property the whole design rests on.

## The node hierarchy

`detail::lru<BaseNode>` is the base class (`lru.h:49`). It holds only three
things: `_max_size`, `_max_age`, and `_end` — a sentinel `BaseNode`. The
sentinel is the head/tail of the circular list. An empty list is `_end` pointing
at itself; `_end._next` is the most-recent entry and `_end._prev` is the
least-recent.

`base_node` (`lru.h:134`) is the plain list node: `_next`, `_prev`, and the
link/unlink/renew primitives. Its `expired()` always returns `false` and its
`expiration()` is `time_point::max()` — a non-aging node never expires. The
`link(node, milliseconds)` overload ignores the timeout (`lru.h:167`); the TTL
parameter exists only so the same call sites work for both node types.

`aging_base_node` (`lru.h:192`) extends `base_node` with three extra fields: an
`_expiration` timestamp and a *second* pair of pointers, `_next_by_age` /
`_prev_by_age`. So an aging node is woven into two intrusive lists at once: the
recency list (inherited) and an age-ordered list. Its `link` overload stamps the
expiration (`now + timeout`, or `max()` if timeout is 0) and splices the node
into both lists (`lru.h:223`); its `unlink` unsplices from both (`lru.h:216`).

`node<Type, BaseNode>` (`lru.h:279`) is the leaf: it inherits the chosen base
node and adds the actual `data` payload (a `std::pair<const Key, T>`).

`lru::lru<...>` (`lru.h:293`) is the user-facing class. It owns the
`unordered_map` and inherits the sentinel and sizing fields from `detail::lru`.
`aging_lru<...>` (`lru.h:803`) is an alias that just swaps the default `Node` to
one built on `aging_base_node`. There is no separate aging class; selecting the
node type selects the behavior.

## Two lists, one container

The recency list is always present. The age-ordered list exists only when the
node type is `aging_base_node`. Why a separate list for age?

Recency order and age order are different. Touching an entry with `renew` moves
it to the front of the *recency* list but does not change when it expires, so it
keeps its place in age order. Eviction by TTL wants to scan entries in the order
they will expire, oldest first, which is exactly the age list. Eviction by
capacity wants to scan least-recently-used first, which is the recency list.
Keeping the two orders as two lists lets `trim_and` do each pass over the right
order in O(1) per step.

Iteration direction is chosen at the type level. The iterator carries a `Mode`
template parameter (`lru.h:58`). `base_node::next`/`prev` are templated but
ignore the mode and walk the recency pointers (`lru.h:176`). For
`aging_base_node`, the default `next`/`prev` walk recency *and skip expired
entries on the fly* (`lru.h:238`), while the `iterate_by_age` specializations
(`lru.h:267`) walk the `_next_by_age` / `_prev_by_age` pointers instead. So the
same iterator template traverses either list depending on its `Mode`, with no
runtime branch.

The iterator also derives from `detail::aging` (`lru.h:58`), which caches a
`now` time point. When an aging iterator first needs the current time it samples
the clock once and reuses it for the rest of the traversal (`lru.h:241`), so a
single scan sees a consistent notion of "now."

## The policy-callback design

The defining decision in this library is that there is one container, and every
eviction or access policy is expressed by a callback that returns an action
enum. Plain LRU, renew-on-touch, TTL expiry, and custom hybrids are all the same
code path with a different callback.

Two enums drive it (`lru.h:320`):

- `DropAction` { `leave`, `renew`, `relink`, `evict`, `stop` } — returned by the
  `OnDrop` callback during insertion/trimming.
- `GetAction` { `leave`, `renew`, `relink`, `evict` } — returned by the `OnGet`
  callback during lookup. Same as `DropAction` without `stop`, because a single
  lookup has no loop to stop.

`leave` keeps the entry untouched. `renew` splices it to the front of the
recency list. `relink` unlinks and re-links it, which on an aging node also
resets its expiration clock. `evict` unlinks it and erases it from the map.
`stop` (drop only) breaks the trim scan early — the default policies return it
to mean "the rest of the list is fine, don't keep walking."

The plain methods are thin wrappers that supply a default callback. `emplace`
supplies "evict if overflowed or expired, else stop" (`lru.h:374`); `find`
supplies "evict if expired, else renew" (`lru.h:458`); `find_and_leave` /
`find_and_renew` / `find_and_relink` supply a fixed action (`lru.h:437`). The
`*_and` variants take your callback directly. So adding a new eviction strategy
never means editing the container — it means writing a lambda.

The callback receives the context it needs to decide: the entry's `value_type`,
whether the cache is currently over capacity, and whether the entry is expired
(`lru.h:419`, `lru.h:695`). It does not get to mutate the cache structure
directly; it returns an action and the container performs the structural change.
That keeps the map/list coupling invariant inside the container, not spread
across user callbacks.

## The trim algorithm

`trim_and` (`lru.h:684`) runs the eviction pass. It runs in up to two phases:

1. If `_max_age` is set, walk the age-ordered list via an `iterator_by_age`,
   oldest first (`lru.h:690`). For each entry call `on_drop` and apply the
   returned action. This phase clears out expired entries.
2. If `_max_size` is bounded, walk the recency list via a plain `iterator`,
   least-recently-used first (`lru.h:718`). Same callback, same dispatch. This
   phase enforces capacity.

Each phase advances with a post-increment so the current node can be erased
without invalidating the walk (`it++` captures the node, then dispatch may erase
it — `lru.h:693`). `stop` short-circuits the loop by jumping the iterator to the
end sentinel.

The important structural choice: `trim_and` is called at the *start* of
`emplace_and`, before the new entry goes in (`lru.h:345`). It trims based on the
current size, then inserts. That is the trim-before-insert behavior. The
consequence is that immediately after an insert that pushes the cache over
capacity, the cache holds `capacity + 1` live entries; the overflow is reclaimed
on the next operation. The smoke test asserts the live count lands in `[2, 3]`
for a capacity-2 cache (`test/test.cc:24`) precisely because of this.

## Insertion path in detail

`emplace_and` (`lru.h:343`):

1. `trim_and(on_drop)` to make room based on current size.
2. `_map.find(key)`. On miss, `_map.emplace` the new node and `link` it at the
   front of the recency list (and age list, if aging) (`lru.h:349`).
3. On hit, if the existing node is expired, erase it and re-emplace fresh
   (`lru.h:355`); if it is live, `renew` it to the front and report
   `created == false` (`lru.h:361`).

`insert_and` forwards to `emplace_and` (`lru.h:384`); `get_and` is find-or-insert
on top of the same primitives (`lru.h:585`).

## Complexity

Average case, assuming a well-behaved hash:

| Operation | Time | Notes |
|---|---|---|
| `find` / `at` / `exists` | O(1) | one map lookup + O(1) list splice |
| `emplace` / `insert` | O(1) amortized + trim | insert is O(1); the trim it triggers is O(k) where k is the number of entries it evicts |
| `erase` | O(1) | map erase + O(1) unlink |
| `trim` | O(k) | k = entries evicted; `stop` lets the default policy bail after the first kept entry |
| iterate | O(n) | n = live entries |

Space is O(n): the map's own overhead plus, per entry, two list pointers
(`base_node`) or four pointers and a timestamp (`aging_base_node`). No separate
list-node allocations — the list lives inside the map's values.

## Design decisions and trade-offs

Intrusive list over the map's values, rather than a side list of iterators or
pointers, avoids a second allocation per entry and a second indirection per
reorder, and it leans on the map's pointer stability instead of duplicating
keys. The cost is the tight coupling: the list pointers are only valid because
the map promises not to move its values, so the design is wedded to a node-based
map.

One container for every policy, via callbacks, rather than separate
`lru_cache` / `ttl_cache` / `lfu_cache` types, keeps the structural code in one
place and lets callers compose behavior. The cost is an API with a `*_and`
variant of nearly every method and an action enum the caller has to learn.

Trim-before-insert rather than after keeps the insert path simple (trim, then a
single unconditional insert) and means the eviction policy sees a consistent
"current size" for the whole pass. The cost is the transient `capacity + 1`
overshoot, which callers who need a hard cap at every instant must account for.

Selecting aging via the node type (`aging_lru` alias) rather than a runtime flag
means the non-aging cache pays zero bytes and zero branches for TTL it does not
use, and the age list machinery compiles away entirely. The cost is that
`max_age` only works with the aging node type, enforced by an `assert` in the
constructor (`lru.h:338`) rather than the type system.

## Known limitations and sharp edges

- The iterator derives from `std::iterator`, deprecated in C++17 (`lru.h:59`).
  It compiles with a `-Wdeprecated-declarations` warning. The fix is to replace
  the base with explicit member typedefs (`iterator_category`, `value_type`,
  etc.).
- Not thread-safe. No internal locking anywhere; concurrent mutation corrupts
  the lists and the map.
- The transient `capacity + 1` overshoot after an overflowing insert (see the
  trim section).
- The `assert` guarding `max_age` on a non-aging cache (`lru.h:338`) is a
  runtime check compiled out in `NDEBUG` builds; a misuse there is silent in
  release.
- Iterator invalidation follows the underlying map: erasing a node (`erase`, an
  evicting `find`, a trim that evicts) invalidates iterators to it.

## Possible improvements

- Drop the `std::iterator` base for explicit typedefs to clear the deprecation.
- A thread-safe wrapper or sharded variant for concurrent use.
- A public `begin`/`end` overload (or a small range view) for age-ordered
  iteration, so callers don't have to construct the `iterator_by_age` typedef by
  hand.
- A constructor-time type check (or a static factory) so passing `max_age` to a
  non-aging cache is a compile error rather than a runtime `assert`.
