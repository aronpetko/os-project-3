// CS4348 Project
// Aron Petkovski (axp220196)
// =============================================================
//
// What's new:
//   - Node struct with serialize/deserialize.
//   - read_node / write_node / allocate_block.
//   - insert: works as long as no node would need to be split.
//             Since we never split, the tree is always exactly one node
//             (the root), so insert succeeds for the first 19 keys and
//             fails on the 20th with a clear error.
//   - search: looks at the root.
//
// What's still missing:
//   - Splitting (split_child) — added in v3.
//   - print, load, extract — added in v3 / v4.

#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

constexpr size_t BLOCK_SIZE = 512;
constexpr size_t MIN_DEGREE = 10;
constexpr size_t MAX_KEYS = 2 * MIN_DEGREE - 1;     // 19
constexpr size_t MAX_CHILDREN = 2 * MIN_DEGREE;         // 20
static const char MAGIC[8] = { '4', '3', '4', '8', 'P', 'R', 'J', '3' };

// ---------- Big-endian I/O ----------
static void write_u64_be(uint8_t *buf, uint64_t v)
{
    for (int i = 7; i >= 0; --i)
    {
        buf[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
}

static uint64_t read_u64_be(const uint8_t *buf)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | buf[i];
    return v;
}

struct Header
{
    uint64_t root_id = 0;
    uint64_t next_block_id = 1;

    void serialize(uint8_t *buf) const
    {
        std::memset(buf, 0, BLOCK_SIZE);
        std::memcpy(buf, MAGIC, 8);
        write_u64_be(buf + 8, root_id);
        write_u64_be(buf + 16, next_block_id);
    }

    bool deserialize(const uint8_t *buf)
    {
        if (std::memcmp(buf, MAGIC, 8) != 0) return false;
        root_id = read_u64_be(buf + 8);
        next_block_id = read_u64_be(buf + 16);
        return true;
    }
};

struct Node
{
    uint64_t block_id = 0;
    uint64_t parent_id = 0;
    uint64_t num_keys = 0;
    uint64_t keys[MAX_KEYS] = { 0 };
    uint64_t values[MAX_KEYS] = { 0 };
    uint64_t children[MAX_CHILDREN] = { 0 };

    bool is_leaf() const
    { return children[0] == 0; }

    void serialize(uint8_t *buf) const
    {
        std::memset(buf, 0, BLOCK_SIZE);
        write_u64_be(buf, block_id);
        write_u64_be(buf + 8, parent_id);
        write_u64_be(buf + 16, num_keys);
        uint8_t *p = buf + 24;
        for (size_t i = 0; i < MAX_KEYS; ++i)
        {
            write_u64_be(p, keys[i]);
            p += 8;
        }
        for (size_t i = 0; i < MAX_KEYS; ++i)
        {
            write_u64_be(p, values[i]);
            p += 8;
        }
        for (size_t i = 0; i < MAX_CHILDREN; ++i)
        {
            write_u64_be(p, children[i]);
            p += 8;
        }
    }

    void deserialize(const uint8_t *buf)
    {
        block_id = read_u64_be(buf);
        parent_id = read_u64_be(buf + 8);
        num_keys = read_u64_be(buf + 16);
        const uint8_t *p = buf + 24;
        for (size_t i = 0; i < MAX_KEYS; ++i)
        {
            keys[i] = read_u64_be(p);
            p += 8;
        }
        for (size_t i = 0; i < MAX_KEYS; ++i)
        {
            values[i] = read_u64_be(p);
            p += 8;
        }
        for (size_t i = 0; i < MAX_CHILDREN; ++i)
        {
            children[i] = read_u64_be(p);
            p += 8;
        }
    }
};

class BTreeFile
{
public:
    Header header;
    std::fstream file;
    std::string filename;

    static bool create_new(const std::string &fname)
    {
        if (fs::exists(fname))
        {
            std::cerr << "Error: file '" << fname << "' already exists.\n";
            return false;
        }
        std::ofstream out(fname, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        Header h;
        uint8_t buf[BLOCK_SIZE];
        h.serialize(buf);
        out.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        return static_cast<bool>(out);
    }

    bool open_existing(const std::string &fname)
    {
        if (!fs::exists(fname))
        {
            std::cerr << "Error: file '" << fname << "' does not exist.\n";
            return false;
        }
        file.open(fname, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) return false;
        filename = fname;
        uint8_t buf[BLOCK_SIZE];
        file.read(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        if (file.gcount() != static_cast<std::streamsize>(BLOCK_SIZE) ||
            !header.deserialize(buf))
        {
            std::cerr << "Error: '" << fname << "' is not a valid index file.\n";
            return false;
        }
        return true;
    }

    void write_header()
    {
        uint8_t buf[BLOCK_SIZE];
        header.serialize(buf);
        file.seekp(0);
        file.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        file.flush();
    }

    void read_node(uint64_t id, Node &n)
    {
        uint8_t buf[BLOCK_SIZE];
        file.seekg(static_cast<std::streamoff>(id) * BLOCK_SIZE);
        file.read(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        n.deserialize(buf);
    }

    void write_node(const Node &n)
    {
        uint8_t buf[BLOCK_SIZE];
        n.serialize(buf);
        file.seekp(static_cast<std::streamoff>(n.block_id) * BLOCK_SIZE);
        file.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        file.flush();
    }

    uint64_t allocate_block()
    {
        uint64_t id = header.next_block_id++;
        write_header();
        return id;
    }

    // No-split insert: only works if the root has < 19 keys.
    bool insert(uint64_t key, uint64_t value)
    {
        // Empty tree: create the root.
        if (header.root_id == 0)
        {
            Node root;
            root.block_id = allocate_block();
            root.parent_id = 0;
            root.num_keys = 1;
            root.keys[0] = key;
            root.values[0] = value;
            header.root_id = root.block_id;
            write_header();
            write_node(root);
            return true;
        }

        Node root;
        read_node(header.root_id, root);

        // Without splitting, we can't grow the tree past one full node.
        if (root.num_keys == MAX_KEYS)
        {
            std::cerr << "Error: root is full and splitting is not yet implemented.\n";
            return false;
        }

        // Insert in sorted position.
        size_t pos = 0;
        while (pos < root.num_keys && root.keys[pos] < key) ++pos;
        if (pos < root.num_keys && root.keys[pos] == key)
        {
            std::cerr << "Error: key " << key << " already exists.\n";
            return false;
        }
        for (size_t i = root.num_keys; i > pos; --i)
        {
            root.keys[i] = root.keys[i - 1];
            root.values[i] = root.values[i - 1];
        }
        root.keys[pos] = key;
        root.values[pos] = value;
        root.num_keys++;
        write_node(root);
        return true;
    }

    // Search the (single-node) tree.
    bool search(uint64_t key, uint64_t &value_out)
    {
        if (header.root_id == 0) return false;
        Node root;
        read_node(header.root_id, root);
        for (size_t i = 0; i < root.num_keys; ++i)
        {
            if (root.keys[i] == key)
            {
                value_out = root.values[i];
                return true;
            }
            if (root.keys[i] > key) return false;   // sorted, can stop early
        }
        return false;
    }
};

static bool parse_uint64(const std::string &s, uint64_t &out)
{
    if (s.empty() || s.find('-') != std::string::npos) return false;
    try
    {
        size_t pos;
        unsigned long long v = std::stoull(s, &pos);
        if (pos != s.size()) return false;
        out = static_cast<uint64_t>(v);
        return true;
    } catch (...)
    { return false; }
}

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <command> <args>\n"
              << "Commands: create, insert, search, [load, print, extract NOT YET]\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }
    std::string cmd = argv[1];

    if (cmd == "create")
    {
        if (argc != 3)
        {
            usage(argv[0]);
            return 1;
        }
        return BTreeFile::create_new(argv[2]) ? 0 : 1;
    }
    if (cmd == "insert")
    {
        if (argc != 5)
        {
            usage(argv[0]);
            return 1;
        }
        uint64_t k, v;
        if (!parse_uint64(argv[3], k))
        {
            std::cerr << "Error: invalid key.\n";
            return 1;
        }
        if (!parse_uint64(argv[4], v))
        {
            std::cerr << "Error: invalid value.\n";
            return 1;
        }
        BTreeFile bt;
        if (!bt.open_existing(argv[2])) return 1;
        return bt.insert(k, v) ? 0 : 1;
    }
    if (cmd == "search")
    {
        if (argc != 4)
        {
            usage(argv[0]);
            return 1;
        }
        uint64_t k;
        if (!parse_uint64(argv[3], k))
        {
            std::cerr << "Error: invalid key.\n";
            return 1;
        }
        BTreeFile bt;
        if (!bt.open_existing(argv[2])) return 1;
        uint64_t v;
        if (bt.search(k, v))
        {
            std::cout << k << ' ' << v << '\n';
            return 0;
        }
        std::cerr << "Error: key " << k << " not found.\n";
        return 1;
    }
    if (cmd == "load" || cmd == "print" || cmd == "extract")
    {
        std::cerr << "Command '" << cmd << "' is not yet implemented (coming in v3/v4).\n";
        return 1;
    }
    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}