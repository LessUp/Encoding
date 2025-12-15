#include <cstdint>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>

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

class ArithmeticEncoder {
public:
    explicit ArithmeticEncoder(BitWriter& w)
        : writer(w), low(0), high(FULL_RANGE - 1), pendingBits(0) {}

    void encode_symbol(uint32_t symbol, const std::vector<uint32_t>& cumulative) {
        uint64_t range = high - low + 1;
        uint64_t total = cumulative.back();
        uint64_t symLow = cumulative[symbol];
        uint64_t symHigh = cumulative[symbol + 1];

        high = low + (range * symHigh) / total - 1;
        low = low + (range * symLow) / total;

        for (;;) {
            if (high < HALF_RANGE) {
                output_bit(0);
            } else if (low >= HALF_RANGE) {
                output_bit(1);
                low -= HALF_RANGE;
                high -= HALF_RANGE;
            } else if (low >= FIRST_QUARTER && high < THIRD_QUARTER) {
                pendingBits++;
                low -= FIRST_QUARTER;
                high -= FIRST_QUARTER;
            } else {
                break;
            }
            low <<= 1;
            high = (high << 1) | 1;
        }
    }

    void finish() {
        pendingBits++;
        if (low < FIRST_QUARTER) {
            output_bit(0);
        } else {
            output_bit(1);
        }
        writer.flush();
    }

private:
    static constexpr uint64_t STATE_BITS = 32;
    static constexpr uint64_t FULL_RANGE = (static_cast<uint64_t>(1) << STATE_BITS);
    static constexpr uint64_t HALF_RANGE = FULL_RANGE >> 1;
    static constexpr uint64_t FIRST_QUARTER = HALF_RANGE >> 1;
    static constexpr uint64_t THIRD_QUARTER = FIRST_QUARTER * 3;

    BitWriter& writer;
    uint64_t low;
    uint64_t high;
    uint64_t pendingBits;

    void output_bit(int bit) {
        writer.write_bit(bit);
        int complement = bit ^ 1;
        while (pendingBits > 0) {
            writer.write_bit(complement);
            pendingBits--;
        }
    }
};

class ArithmeticDecoder {
public:
    explicit ArithmeticDecoder(BitReader& r)
        : reader(r), low(0), high(FULL_RANGE - 1), code(0) {
        for (uint64_t i = 0; i < STATE_BITS; i++) {
            code = (code << 1) | static_cast<uint64_t>(reader.read_bit());
        }
    }

    uint32_t decode_symbol(const std::vector<uint32_t>& cumulative) {
        uint64_t range = high - low + 1;
        uint64_t total = cumulative.back();
        uint64_t offset = code - low;
        uint64_t value = ((offset + 1) * total - 1) / range;

        uint32_t lo = 0;
        uint32_t hi = static_cast<uint32_t>(cumulative.size() - 1);
        while (lo + 1 < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (cumulative[mid] > value) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        uint32_t symbol = lo;

        uint64_t symLow = cumulative[symbol];
        uint64_t symHigh = cumulative[symbol + 1];

        high = low + (range * symHigh) / total - 1;
        low = low + (range * symLow) / total;

        for (;;) {
            if (high < HALF_RANGE) {
            } else if (low >= HALF_RANGE) {
                low -= HALF_RANGE;
                high -= HALF_RANGE;
                code -= HALF_RANGE;
            } else if (low >= FIRST_QUARTER && high < THIRD_QUARTER) {
                low -= FIRST_QUARTER;
                high -= FIRST_QUARTER;
                code -= FIRST_QUARTER;
            } else {
                break;
            }
            low <<= 1;
            high = (high << 1) | 1;
            code = (code << 1) | static_cast<uint64_t>(reader.read_bit());
        }

        return symbol;
    }

private:
    static constexpr uint64_t STATE_BITS = 32;
    static constexpr uint64_t FULL_RANGE = (static_cast<uint64_t>(1) << STATE_BITS);
    static constexpr uint64_t HALF_RANGE = FULL_RANGE >> 1;
    static constexpr uint64_t FIRST_QUARTER = HALF_RANGE >> 1;
    static constexpr uint64_t THIRD_QUARTER = FIRST_QUARTER * 3;

    BitReader& reader;
    uint64_t low;
    uint64_t high;
    uint64_t code;
};

static const uint32_t SYMBOL_LIMIT = 257;
static const uint32_t EOF_SYMBOL = SYMBOL_LIMIT - 1;
static const uint32_t MAX_TOTAL = 1u << 24;

static void scale_frequencies(std::vector<uint32_t>& freq) {
    uint64_t total = 0;
    for (uint32_t f : freq) {
        total += f;
    }
    if (total == 0) {
        for (size_t i = 0; i < freq.size(); i++) {
            freq[i] = 1;
        }
        return;
    }
    if (total <= MAX_TOTAL) {
        return;
    }
    uint64_t newTotal = 0;
    for (size_t i = 0; i < freq.size(); i++) {
        if (freq[i] == 0) {
            continue;
        }
        uint64_t scaled = static_cast<uint64_t>(freq[i]) * MAX_TOTAL / total;
        if (scaled == 0) {
            scaled = 1;
        }
        freq[i] = static_cast<uint32_t>(scaled);
        newTotal += scaled;
    }
    if (newTotal == 0) {
        uint32_t base = MAX_TOTAL / static_cast<uint32_t>(freq.size());
        if (base == 0) {
            base = 1;
        }
        for (size_t i = 0; i < freq.size(); i++) {
            freq[i] = base;
        }
    }
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
    scale_frequencies(freq);
    return freq;
}

static std::vector<uint32_t> build_cumulative(const std::vector<uint32_t>& freq) {
    std::vector<uint32_t> cumulative(freq.size() + 1, 0);
    for (size_t i = 0; i < freq.size(); i++) {
        cumulative[i + 1] = cumulative[i] + freq[i];
    }
    if (cumulative.back() == 0) {
        for (size_t i = 0; i < freq.size(); i++) {
            cumulative[i + 1] = static_cast<uint32_t>(i + 1);
        }
    }
    return cumulative;
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
    std::vector<uint32_t> cumulative = build_cumulative(freq);

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file for reading\n";
        return false;
    }
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file for writing\n";
        return false;
    }

    const char magic[4] = {'A', 'E', 'N', 'C'};
    out.write(magic, sizeof(magic));
    write_frequencies(out, freq);

    BitWriter bitWriter(out);
    ArithmeticEncoder encoder(bitWriter);

    char c;
    while (in.get(c)) {
        uint32_t sym = static_cast<uint8_t>(c);
        encoder.encode_symbol(sym, cumulative);
    }
    encoder.encode_symbol(EOF_SYMBOL, cumulative);
    encoder.finish();

    if (in.bad()) {
        std::cerr << "Failed to read input file\n";
        return false;
    }
    if (!out) {
        std::cerr << "Failed to write output file\n";
        return false;
    }

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
    if (!in || magic[0] != 'A' || magic[1] != 'E' || magic[2] != 'N' || magic[3] != 'C') {
        std::cerr << "Invalid input file format\n";
        return false;
    }

    std::vector<uint32_t> freq;
    if (!read_frequencies(in, freq)) {
        return false;
    }
    std::vector<uint32_t> cumulative = build_cumulative(freq);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file for writing\n";
        return false;
    }

    BitReader bitReader(in);
    ArithmeticDecoder decoder(bitReader);

    for (;;) {
        uint32_t sym = decoder.decode_symbol(cumulative);
        if (sym == EOF_SYMBOL) {
            break;
        }
        unsigned char b = static_cast<unsigned char>(sym);
        out.put(static_cast<char>(b));
        if (!out) {
            std::cerr << "Failed to write output file\n";
            return false;
        }
    }

    return true;
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
