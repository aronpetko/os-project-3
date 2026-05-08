// CS4348 Project 3
// Aron Petkovski (axp220196)
// =======================================
//
// What's working:
//   - Argument parsing for all 6 commands.
//   - `create` writes a valid 512-byte header (magic + zeros).
//   - `open_existing` validates that a file is a real index file.
//   - All other commands open/validate the file, then say "not implemented".
//
// What's still missing:
//   - Node struct, B-tree logic, every command except `create`.
//
// Compile:
//   g++ -std=c++17 -O2 -Wall -o project3 project3.cpp

#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

constexpr size_t BLOCK_SIZE = 512;
static const char MAGIC[8] = { '4', '3', '4', '8', 'P', 'R', 'J', '3' };

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
    uint64_t root_id = 0;   // 0 = empty tree
    uint64_t next_block_id = 1;   // block 0 is the header itself

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

// ---------- BTreeFile ----------
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
        if (!out)
        {
            std::cerr << "Error: cannot create file '" << fname << "'.\n";
            return false;
        }
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
        if (!file)
        {
            std::cerr << "Error: cannot open file '" << fname << "'.\n";
            return false;
        }
        filename = fname;
        uint8_t buf[BLOCK_SIZE];
        file.read(reinterpret_cast<char *>(buf), BLOCK_SIZE);
        if (file.gcount() != static_cast<std::streamsize>(BLOCK_SIZE))
        {
            std::cerr << "Error: '" << fname << "' is not a valid index file.\n";
            return false;
        }
        if (!header.deserialize(buf))
        {
            std::cerr << "Error: '" << fname << "' is not a valid index file (bad magic).\n";
            return false;
        }
        return true;
    }
};

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <command> <args>\n"
              << "Commands:\n"
              << "  create  <indexfile>\n"
              << "  insert  <indexfile> <key> <value>\n"
              << "  search  <indexfile> <key>\n"
              << "  load    <indexfile> <csvfile>\n"
              << "  print   <indexfile>\n"
              << "  extract <indexfile> <outfile>\n";
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

    // Every other command: validate the file is a real index file,
    // then admit we haven't implemented the operation yet.
    if (cmd == "insert" || cmd == "search" || cmd == "load" ||
        cmd == "print" || cmd == "extract")
    {
        if (argc < 3)
        {
            usage(argv[0]);
            return 1;
        }
        BTreeFile bt;
        if (!bt.open_existing(argv[2])) return 1;
        std::cerr << "Command '" << cmd << "' is not yet implemented.\n";
        return 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    usage(argv[0]);
    return 1;
}
