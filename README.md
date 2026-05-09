# CS4348 Project 3 — B-Tree Index File

## Build

This project can be compiled with either CMake or Make.

```bash
mkdir build
cd build
cmake ..
make
```

## Commands

```
./file create  <indexfile>
./file insert  <indexfile> <key> <value>
./file search  <indexfile> <key>
./file load    <indexfile> <csvfile>
./file print   <indexfile>
./file extract <indexfile> <outfile>
```

## File format (per the spec)

- 512-byte blocks. Block 0 is the header.
- Header: 8-byte magic `4348PRJ3`, 8-byte root block id (0 = empty),
  8-byte next-free block id.
- Node: 8-byte block id, 8-byte parent id (0 = root), 8-byte key count,
  19 keys × 8 bytes, 19 values × 8 bytes, 20 child pointers × 8 bytes.
- All multi-byte integers are big-endian (manually serialized — no
  endianness assumption about the host).

## B-tree

Minimum degree t = 10, so each node holds 9–19 keys (root may hold 1–19)
and 10–20 children, with leaves having all-zero child pointers.

Insertion is **top-down with preemptive splitting**: any full child is
split before descending into it. This bounds the number of Node objects
in memory to **3** (parent + child + new sibling) and removes the need
for a back-up pass after insertion.

Search and traversal (print/extract) are iterative; they hold only one
Node at a time. Traversal uses an explicit stack of `(block_id, step)`
pairs so the recursion stack never holds Node objects.