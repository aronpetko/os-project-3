// CS4348 Project 3
// Aron Petkovski (axp220196)

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr size_t BLOCK_SIZE = 512;
constexpr size_t MIN_DEGREE = 10;               // t
constexpr size_t MAX_KEYS = 2 * MIN_DEGREE - 1; // 19
constexpr size_t MAX_CHILDREN = 2 * MIN_DEGREE; // 20
static const char MAGIC[8] = {'4', '3', '4', '8', 'P', 'R', 'J', '3'};

static void write_u64_be(uint8_t *buf, uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    buf[i] = static_cast<uint8_t>(v & 0xFF);
    v >>= 8;
  }
}

static uint64_t read_u64_be(const uint8_t *buf) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v = (v << 8) | buf[i];
  return v;
}

struct Header {
  uint64_t root_id = 0;       // 0 = empty tree
  uint64_t next_block_id = 1; // block 0 is the header itself

  void serialize(uint8_t *buf) const {
    std::memset(buf, 0, BLOCK_SIZE);
    std::memcpy(buf, MAGIC, 8);
    write_u64_be(buf + 8, root_id);
    write_u64_be(buf + 16, next_block_id);
  }

  bool deserialize(const uint8_t *buf) {
    if (std::memcmp(buf, MAGIC, 8) != 0)
      return false;
    root_id = read_u64_be(buf + 8);
    next_block_id = read_u64_be(buf + 16);
    return true;
  }
};

struct Node {
  uint64_t block_id = 0;
  uint64_t parent_id = 0;
  uint64_t num_keys = 0;
  uint64_t keys[MAX_KEYS] = {0};
  uint64_t values[MAX_KEYS] = {0};
  uint64_t children[MAX_CHILDREN] = {0};

  // A node is a leaf when it has no children; child id 0 is reserved
  // (block 0 is the header), so children[0] == 0 means "no child".
  bool is_leaf() const { return children[0] == 0; }

  void serialize(uint8_t *buf) const {
    std::memset(buf, 0, BLOCK_SIZE);
    write_u64_be(buf, block_id);
    write_u64_be(buf + 8, parent_id);
    write_u64_be(buf + 16, num_keys);
    uint8_t *p = buf + 24;

    for (size_t i = 0; i < MAX_KEYS; ++i) {
      write_u64_be(p, keys[i]);
      p += 8;
    }

    for (size_t i = 0; i < MAX_KEYS; ++i) {
      write_u64_be(p, values[i]);
      p += 8;
    }

    for (size_t i = 0; i < MAX_CHILDREN; ++i) {
      write_u64_be(p, children[i]);
      p += 8;
    }
  }

  void deserialize(const uint8_t *buf) {
    block_id = read_u64_be(buf);
    parent_id = read_u64_be(buf + 8);
    num_keys = read_u64_be(buf + 16);

    const uint8_t *p = buf + 24;

    for (size_t i = 0; i < MAX_KEYS; ++i) {
      keys[i] = read_u64_be(p);
      p += 8;
    }

    for (size_t i = 0; i < MAX_KEYS; ++i) {
      values[i] = read_u64_be(p);
      p += 8;
    }

    for (size_t i = 0; i < MAX_CHILDREN; ++i) {
      children[i] = read_u64_be(p);
      p += 8;
    }
  }
};

class BTreeFile {
public:
  Header header;
  std::fstream file;
  std::string filename;

  // Create a brand-new index file (errors if it already exists).
  static bool create_new(const std::string &fname) {
    if (fs::exists(fname)) {
      std::cerr << "Error: file '" << fname << "' already exists.\n";
      return false;
    }

    std::ofstream out(fname, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "Error: cannot create file '" << fname << "'.\n";
      return false;
    }

    uint8_t buf[BLOCK_SIZE];

    Header h;
    h.serialize(buf);

    out.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
    return static_cast<bool>(out);
  }

  // Open and validate an existing index file.
  bool open_existing(const std::string &fname) {
    if (!fs::exists(fname)) {
      std::cerr << "Error: file '" << fname << "' does not exist.\n";
      return false;
    }

    file.open(fname, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
      std::cerr << "Error: cannot open file '" << fname << "'.\n";
      return false;
    }

    filename = fname;

    uint8_t buf[BLOCK_SIZE];
    file.read(reinterpret_cast<char *>(buf), BLOCK_SIZE);

    if (file.gcount() != static_cast<std::streamsize>(BLOCK_SIZE)) {
      std::cerr << "Error: '" << fname
                << "' is not a valid index file (header too short).\n";
      return false;
    }

    if (!header.deserialize(buf)) {
      std::cerr << "Error: '" << fname
                << "' is not a valid index file (bad magic number).\n";
      return false;
    }

    return true;
  }

  void write_header() {
    uint8_t buf[BLOCK_SIZE];
    header.serialize(buf);
    file.seekp(0);
    file.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
    file.flush();
  }

  void read_node(uint64_t id, Node &n) {
    uint8_t buf[BLOCK_SIZE];
    file.seekg(static_cast<std::streamoff>(id) * BLOCK_SIZE);
    file.read(reinterpret_cast<char *>(buf), BLOCK_SIZE);
    n.deserialize(buf);
  }

  void write_node(const Node &n) {
    uint8_t buf[BLOCK_SIZE];
    n.serialize(buf);
    file.seekp(static_cast<std::streamoff>(n.block_id) * BLOCK_SIZE);
    file.write(reinterpret_cast<char *>(buf), BLOCK_SIZE);
    file.flush();
  }

  uint64_t allocate_block() {
    uint64_t id = header.next_block_id++;
    write_header();
    return id;
  }

  // Split parent.children[index] (which is full) in two; pushes the
  // median key/value up into parent. parent must NOT be full.
  // In-memory nodes during this call: parent + child + sibling = 3.
  // The trailing parent_id fix-up reuses the `sibling` slot.
  void split_child(Node &parent, size_t index, Node &child) {
    const bool child_was_internal = !child.is_leaf();

    Node sibling;
    sibling.block_id = allocate_block();
    sibling.parent_id = parent.block_id;
    sibling.num_keys = MIN_DEGREE - 1; // 9

    // Move upper half of child's keys/values into sibling.
    for (size_t i = 0; i < MIN_DEGREE - 1; ++i) {
      sibling.keys[i] = child.keys[i + MIN_DEGREE];
      sibling.values[i] = child.values[i + MIN_DEGREE];
    }
    if (child_was_internal) {
      for (size_t i = 0; i < MIN_DEGREE; ++i) {
        sibling.children[i] = child.children[i + MIN_DEGREE];
      }
    }

    const uint64_t median_key = child.keys[MIN_DEGREE - 1];
    const uint64_t median_value = child.values[MIN_DEGREE - 1];

    // Truncate child to its lower half.
    child.num_keys = MIN_DEGREE - 1;
    for (size_t i = MIN_DEGREE - 1; i < MAX_KEYS; ++i) {
      child.keys[i] = child.values[i] = 0;
    }
    if (child_was_internal) {
      for (size_t i = MIN_DEGREE; i < MAX_CHILDREN; ++i)
        child.children[i] = 0;
    }

    // Make room in parent and insert median + sibling pointer.
    for (size_t i = parent.num_keys + 1; i > index + 1; --i)
      parent.children[i] = parent.children[i - 1];
    parent.children[index + 1] = sibling.block_id;

    for (size_t i = parent.num_keys; i > index; --i) {
      parent.keys[i] = parent.keys[i - 1];
      parent.values[i] = parent.values[i - 1];
    }
    parent.keys[index] = median_key;
    parent.values[index] = median_value;
    parent.num_keys++;

    // Save the children we'll need to fix up before reusing `sibling`.
    uint64_t children_to_fix[MIN_DEGREE];
    size_t nfix = 0;
    if (child_was_internal) {
      for (size_t i = 0; i < MIN_DEGREE; ++i) {
        if (sibling.children[i] != 0)
          children_to_fix[nfix++] = sibling.children[i];
      }
    }
    const uint64_t sibling_id = sibling.block_id;

    write_node(parent);
    write_node(child);
    write_node(sibling);

    // Fix parent_id of every child we moved to the sibling.
    // We REUSE the `sibling` Node as scratch storage so we still have
    // only 3 Node objects live (parent, child, sibling).
    for (size_t i = 0; i < nfix; ++i) {
      read_node(children_to_fix[i], sibling);
      sibling.parent_id = sibling_id;
      write_node(sibling);
    }
  }

  // Insert a key/value pair. Top-down preemptive splitting.
  // Returns false if the key already exists.
  bool insert(uint64_t key, uint64_t value) {
    // Empty tree — create the root.
    if (header.root_id == 0) {
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

    Node current;
    read_node(header.root_id, current);

    // If the root is full, grow the tree by 1 level. Use an inner
    // scope so `new_root` is gone before we enter the descent loop.
    if (current.num_keys == MAX_KEYS) {
      {
        Node new_root;
        new_root.block_id = allocate_block();
        new_root.parent_id = 0;
        new_root.num_keys = 0;
        new_root.children[0] = current.block_id;
        current.parent_id = new_root.block_id;

        header.root_id = new_root.block_id;
        write_header();

        split_child(new_root, 0, current); // 3 nodes live here

        if (key == new_root.keys[0]) {
          std::cerr << "Error: key " << key << " already exists.\n";
          return false;
        }
        uint64_t next = (key > new_root.keys[0]) ? new_root.children[1]
                                                 : new_root.children[0];
        read_node(next, current);
      } // new_root released
    }

    // Descend, splitting any full child encountered.
    while (!current.is_leaf()) {
      size_t i = 0;
      while (i < current.num_keys && key > current.keys[i])
        ++i;
      if (i < current.num_keys && current.keys[i] == key) {
        std::cerr << "Error: key " << key << " already exists.\n";
        return false;
      }

      Node child;
      read_node(current.children[i], child);

      if (child.num_keys == MAX_KEYS) {
        split_child(current, i, child); // 3 nodes live here
        if (key == current.keys[i]) {
          std::cerr << "Error: key " << key << " already exists.\n";
          return false;
        }
        uint64_t next = (key > current.keys[i]) ? current.children[i + 1]
                                                : current.children[i];
        read_node(next, child);
      }
      current = child; // child copied into current; child slot reused next loop
    }

    // Insert into the leaf (which is guaranteed not full).
    size_t pos = 0;
    while (pos < current.num_keys && current.keys[pos] < key)
      ++pos;
    if (pos < current.num_keys && current.keys[pos] == key) {
      std::cerr << "Error: key " << key << " already exists.\n";
      return false;
    }

    for (size_t i = current.num_keys; i > pos; --i) {
      current.keys[i] = current.keys[i - 1];
      current.values[i] = current.values[i - 1];
    }

    current.keys[pos] = key;
    current.values[pos] = value;
    current.num_keys++;
    write_node(current);

    return true;
  }

  // Iterative search; only ever holds 1 Node in memory.
  bool search(uint64_t key, uint64_t &value_out) {
    if (header.root_id == 0)
      return false;

    uint64_t cur = header.root_id;
    Node node;
    while (cur != 0) {
      read_node(cur, node);
      size_t i = 0;
      while (i < node.num_keys && key > node.keys[i])
        ++i;
      if (i < node.num_keys && key == node.keys[i]) {
        value_out = node.values[i];
        return true;
      }
      if (node.is_leaf())
        return false;
      cur = node.children[i];
    }

    return false;
  }

  // Iterative in-order traversal. Stack holds only (block_id, step);
  // exactly ONE Node is loaded at a time.
  template <typename Fn> void traverse(Fn fn) {
    if (header.root_id == 0)
      return;

    struct Frame {
      uint64_t id;
      size_t step;
    };
    std::vector<Frame> stack;

    stack.push_back({header.root_id, 0});
    Node node;

    while (!stack.empty()) {
      Frame f = stack.back();
      read_node(f.id, node);
      size_t total = 2 * node.num_keys + 1; // n+1 children + n keys
      if (f.step >= total) {
        stack.pop_back();
        continue;
      }

      stack.back().step++;

      if (f.step % 2 == 0) {
        size_t idx = f.step / 2;
        uint64_t cid = node.children[idx];
        if (cid != 0)
          stack.push_back({cid, 0});
      } else {
        size_t idx = (f.step - 1) / 2;
        fn(node.keys[idx], node.values[idx]);
      }
    }
  }

  void print_all() {
    traverse(
        [](uint64_t k, uint64_t v) { std::cout << k << ' ' << v << '\n'; });
  }

  bool extract_to(const std::string &outfile) {
    if (fs::exists(outfile)) {
      std::cerr << "Error: file '" << outfile << "' already exists.\n";
      return false;
    }
    std::ofstream out(outfile);
    if (!out) {
      std::cerr << "Error: cannot create '" << outfile << "'.\n";
      return false;
    }
    traverse([&out](uint64_t k, uint64_t v) { out << k << ',' << v << '\n'; });
    return static_cast<bool>(out);
  }

  bool load_csv(const std::string &csvfile) {
    if (!fs::exists(csvfile)) {
      std::cerr << "Error: file '" << csvfile << "' does not exist.\n";
      return false;
    }
    std::ifstream in(csvfile);
    if (!in) {
      std::cerr << "Error: cannot open '" << csvfile << "'.\n";
      return false;
    }
    std::string line;
    size_t lineno = 0;
    while (std::getline(in, line)) {
      ++lineno;
      // strip trailing whitespace / CR
      while (!line.empty() &&
             (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();

      if (line.empty())
        continue;

      auto comma = line.find(',');
      if (comma == std::string::npos) {
        std::cerr << "Warning: line " << lineno
                  << " missing comma; skipping.\n";
        continue;
      }

      std::string ks = line.substr(0, comma);
      std::string vs = line.substr(comma + 1);

      try {
        size_t p1, p2;
        unsigned long long k = std::stoull(ks, &p1);
        unsigned long long v = std::stoull(vs, &p2);
        if (p1 != ks.size() || p2 != vs.size() ||
            ks.find('-') != std::string::npos ||
            vs.find('-') != std::string::npos) {
          std::cerr << "Warning: line " << lineno
                    << " contains invalid integer; skipping.\n";
          continue;
        }
        insert(static_cast<uint64_t>(k), static_cast<uint64_t>(v));
      } catch (...) {
        std::cerr << "Warning: line " << lineno
                  << " contains invalid integer; skipping.\n";
      }
    }
    return true;
  }
};

static bool parse_uint64(const std::string &s, uint64_t &out) {
  if (s.empty() || s.find('-') != std::string::npos)
    return false;
  try {
    size_t pos;
    unsigned long long v = std::stoull(s, &pos);
    if (pos != s.size())
      return false;
    out = static_cast<uint64_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

static void usage(const char *prog) {
  std::cerr << "Usage: " << prog << " <command> <args>\n"
            << "Commands:\n"
            << "  create  <indexfile>\n"
            << "  insert  <indexfile> <key> <value>\n"
            << "  search  <indexfile> <key>\n"
            << "  load    <indexfile> <csvfile>\n"
            << "  print   <indexfile>\n"
            << "  extract <indexfile> <outfile>\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  const std::string cmd = argv[1];

  if (cmd == "create") {
    if (argc != 3) {
      usage(argv[0]);
      return 1;
    }
    return BTreeFile::create_new(argv[2]) ? 0 : 1;
  }

  if (cmd == "insert") {
    if (argc != 5) {
      usage(argv[0]);
      return 1;
    }
    uint64_t k, v;
    if (!parse_uint64(argv[3], k)) {
      std::cerr << "Error: invalid key '" << argv[3] << "'.\n";
      return 1;
    }
    if (!parse_uint64(argv[4], v)) {
      std::cerr << "Error: invalid value '" << argv[4] << "'.\n";
      return 1;
    }
    BTreeFile bt;
    if (!bt.open_existing(argv[2]))
      return 1;
    return bt.insert(k, v) ? 0 : 1;
  }

  if (cmd == "search") {
    if (argc != 4) {
      usage(argv[0]);
      return 1;
    }
    uint64_t k;
    if (!parse_uint64(argv[3], k)) {
      std::cerr << "Error: invalid key '" << argv[3] << "'.\n";
      return 1;
    }
    BTreeFile bt;
    if (!bt.open_existing(argv[2]))
      return 1;
    uint64_t v;
    if (bt.search(k, v)) {
      std::cout << k << ' ' << v << '\n';
      return 0;
    }
    std::cerr << "Error: key " << k << " not found.\n";
    return 1;
  }

  if (cmd == "load") {
    if (argc != 4) {
      usage(argv[0]);
      return 1;
    }
    BTreeFile bt;
    if (!bt.open_existing(argv[2]))
      return 1;
    return bt.load_csv(argv[3]) ? 0 : 1;
  }

  if (cmd == "print") {
    if (argc != 3) {
      usage(argv[0]);
      return 1;
    }
    BTreeFile bt;
    if (!bt.open_existing(argv[2]))
      return 1;
    bt.print_all();
    return 0;
  }

  if (cmd == "extract") {
    if (argc != 4) {
      usage(argv[0]);
      return 1;
    }
    BTreeFile bt;
    if (!bt.open_existing(argv[2]))
      return 1;
    return bt.extract_to(argv[3]) ? 0 : 1;
  }

  std::cerr << "Unknown command: " << cmd << '\n';
  usage(argv[0]);
  return 1;
}