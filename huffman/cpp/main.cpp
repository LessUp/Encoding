#include <cstdint>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <queue>

class BitWriter {
public:
    explicit BitWriter(std::ostream& s) : stream(s), buffer(0), bitsInBuffer(0) {}

    void write_bit(int bit) {
        buffer = static_cast<uint8_t>((buffer << 1) | (bit & 1));
        bitsInBuffer++;
        if (bitsInBuffer == 8) {
            stream.put(static_cast<char>(buffer));
            bitsInBuffer = 0;
            buffer = 0;
        }
    }

    void flush() {
        if (bitsInBuffer > 0) {
            buffer <<= (8 - bitsInBuffer);
            stream.put(static_cast<char>(buffer));
            bitsInBuffer = 0;
            buffer = 0;
        }
    }

private:
    std::ostream& stream;
    uint8_t buffer;
    int bitsInBuffer;
};

class BitReader {
public:
    explicit BitReader(std::istream& s) : stream(s), currentByte(0), bitsRemaining(0), reachedEof(false) {}

    int read_bit() {
        if (bitsRemaining == 0) {
            int c = stream.get();
            if (c == EOF) {
                reachedEof = true;
                return 0;
            }
            currentByte = static_cast<uint8_t>(c);
            bitsRemaining = 8;
        }
        bitsRemaining--;
        return (currentByte >> bitsRemaining) & 1;
    }

    bool eof() const {
        return reachedEof;
    }

private:
    std::istream& stream;
    uint8_t currentByte;
    int bitsRemaining;
    bool reachedEof;
};

static const uint32_t SYMBOL_LIMIT = 257;
static const uint32_t EOF_SYMBOL = SYMBOL_LIMIT - 1;

struct Node {
    uint32_t symbol;
    uint64_t freq;
    Node* left;
    Node* right;
};

static bool is_leaf(const Node* node) {
    return node->left == nullptr && node->right == nullptr;
}

struct NodeCmp {
    bool operator()(const Node* a, const Node* b) const {
        if (a->freq != b->freq) {
            return a->freq > b->freq;
        }
        return a->symbol > b->symbol;
    }
};

static Node* build_tree(const std::vector<uint32_t>& freq) {
    std::priority_queue<Node*, std::vector<Node*>, NodeCmp> pq;
    for (uint32_t s = 0; s < SYMBOL_LIMIT; s++) {
        if (freq[s] == 0) {
            continue;
        }
        Node* n = new Node;
        n->symbol = s;
        n->freq = freq[s];
        n->left = nullptr;
        n->right = nullptr;
        pq.push(n);
    }
    if (pq.empty()) {
        Node* n = new Node;
        n->symbol = EOF_SYMBOL;
        n->freq = 1;
        n->left = nullptr;
        n->right = nullptr;
        return n;
    }
    if (pq.size() == 1) {
        Node* only = pq.top();
        pq.pop();
        Node* parent = new Node;
        parent->symbol = 0;
        parent->freq = only->freq;
        parent->left = only;
        parent->right = nullptr;
        pq.push(parent);
    }
    while (pq.size() > 1) {
        Node* a = pq.top();
        pq.pop();
        Node* b = pq.top();
        pq.pop();
        Node* parent = new Node;
        parent->symbol = 0;
        parent->freq = a->freq + b->freq;
        parent->left = a;
        parent->right = b;
        pq.push(parent);
    }
    return pq.top();
}

static void destroy_tree(Node* node) {
    if (!node) {
        return;
    }
    destroy_tree(node->left);
    destroy_tree(node->right);
    delete node;
}

static void build_codes(Node* node, std::vector<std::string>& codes, std::string& prefix) {
    if (!node) {
        return;
    }
    if (is_leaf(node)) {
        if (prefix.empty()) {
            codes[node->symbol] = "0";
        } else {
            codes[node->symbol] = prefix;
        }
        return;
    }
    prefix.push_back('0');
    build_codes(node->left, codes, prefix);
    prefix.pop_back();
    prefix.push_back('1');
    build_codes(node->right, codes, prefix);
    prefix.pop_back();
}

static std::vector<uint32_t> build_frequencies_from_file(const std::string& inputPath) {
    std::vector<uint32_t> freq(SYMBOL_LIMIT, 0);
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        return freq;
    }
    char c;
    while (in.get(c)) {
        unsigned char uc = static_cast<unsigned char>(c);
        freq[static_cast<uint32_t>(uc)]++;
    }
    freq[EOF_SYMBOL] = 1;
    return freq;
}

static void write_frequencies(std::ostream& out, const std::vector<uint32_t>& freq) {
    uint32_t count = static_cast<uint32_t>(freq.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (uint32_t v : freq) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
}

static bool read_frequencies(std::istream& in, std::vector<uint32_t>& freq) {
    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in) {
        std::cerr << "Failed to read frequency table\n";
        return false;
    }
    if (count != SYMBOL_LIMIT) {
        std::cerr << "Bad frequency table size: " << count << "\n";
        return false;
    }
    freq.assign(count, 0);
    in.read(reinterpret_cast<char*>(freq.data()), freq.size() * sizeof(uint32_t));
    if (!in) {
        std::cerr << "Failed to read frequency table\n";
        return false;
    }
    return true;
}

static bool compress_file(const std::string& inputPath, const std::string& outputPath) {
    std::vector<uint32_t> freq = build_frequencies_from_file(inputPath);
    Node* root = build_tree(freq);
    std::vector<std::string> codes(SYMBOL_LIMIT);
    std::string prefix;
    build_codes(root, codes, prefix);

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file for reading\n";
        destroy_tree(root);
        return false;
    }
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file for writing\n";
        destroy_tree(root);
        return false;
    }

    const char magic[4] = {'H', 'F', 'M', 'N'};
    out.write(magic, sizeof(magic));
    write_frequencies(out, freq);

    BitWriter bitWriter(out);
    char c;
    while (in.get(c)) {
        uint32_t sym = static_cast<uint8_t>(c);
        const std::string& code = codes[sym];
        for (char b : code) {
            bitWriter.write_bit(b == '1' ? 1 : 0);
        }
    }
    const std::string& eofCode = codes[EOF_SYMBOL];
    for (char b : eofCode) {
        bitWriter.write_bit(b == '1' ? 1 : 0);
    }
    bitWriter.flush();

    if (in.bad()) {
        std::cerr << "Failed to read input file\n";
        destroy_tree(root);
        return false;
    }
    if (!out) {
        std::cerr << "Failed to write output file\n";
        destroy_tree(root);
        return false;
    }

    destroy_tree(root);
    return true;
}

static bool decompress_file(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file for reading\n";
        return false;
    }
    char magic[4] = {};
    in.read(magic, sizeof(magic));
    if (!in || magic[0] != 'H' || magic[1] != 'F' || magic[2] != 'M' || magic[3] != 'N') {
        std::cerr << "Invalid input file format\n";
        return false;
    }

    std::vector<uint32_t> freq;
    if (!read_frequencies(in, freq)) {
        return false;
    }
    Node* root = build_tree(freq);
    if (!root) {
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file for writing\n";
        destroy_tree(root);
        return false;
    }

    BitReader bitReader(in);
    Node* node = root;
    bool sawEOF = false;
    bool ok = true;
    while (true) {
        int bit = bitReader.read_bit();
        if (bit == 0) {
            node = node->left;
        } else {
            node = node->right;
        }
        if (!node) {
            std::cerr << "Input data corrupted or truncated\n";
            ok = false;
            break;
        }
        if (is_leaf(node)) {
            if (node->symbol == EOF_SYMBOL) {
                sawEOF = true;
                break;
            }
            unsigned char b = static_cast<unsigned char>(node->symbol);
            out.put(static_cast<char>(b));
            if (!out) {
                std::cerr << "Failed to write output file\n";
                ok = false;
                break;
            }
            node = root;
        }
        if (bitReader.eof() && node == root) {
            break;
        }
    }

    if (!sawEOF) {
        std::cerr << "Input data corrupted or truncated\n";
        ok = false;
    }
    destroy_tree(root);
    return ok;
}

void huffman_encode_file(const std::string& inputPath, const std::string& outputPath) {
    (void)compress_file(inputPath, outputPath);
}

void huffman_decode_file(const std::string& inputPath, const std::string& outputPath) {
    (void)decompress_file(inputPath, outputPath);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " encode|decode input output\n";
        return 1;
    }
    std::string mode = argv[1];
    std::string inputPath = argv[2];
    std::string outputPath = argv[3];

    bool ok = true;

    if (mode == "encode") {
        ok = compress_file(inputPath, outputPath);
    } else if (mode == "decode") {
        ok = decompress_file(inputPath, outputPath);
    } else {
        std::cerr << "Unknown mode\n";
        return 1;
    }

    return ok ? 0 : 1;
}
