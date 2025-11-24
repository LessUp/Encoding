#include <cstdint>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

namespace range_coder {

static const uint32_t SYMBOL_LIMIT = 257;
static const uint32_t EOF_SYMBOL = SYMBOL_LIMIT - 1;
static const uint32_t MAX_TOTAL = 1u << 24;
static const uint32_t RENORM_THRESHOLD = 1u << 24;

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

static std::vector<uint32_t> build_frequencies_from_data(const std::vector<uint8_t>& data) {
    std::vector<uint32_t> freq(SYMBOL_LIMIT, 0);
    for (uint8_t b : data) {
        freq[static_cast<uint32_t>(b)]++;
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

static void write_u32_le(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static bool read_u32_le(const std::vector<uint8_t>& in, size_t& pos, uint32_t& v) {
    if (pos + 4 > in.size()) {
        return false;
    }
    v = static_cast<uint32_t>(in[pos]) |
        (static_cast<uint32_t>(in[pos + 1]) << 8) |
        (static_cast<uint32_t>(in[pos + 2]) << 16) |
        (static_cast<uint32_t>(in[pos + 3]) << 24);
    pos += 4;
    return true;
}

static void write_header(std::vector<uint8_t>& out, const std::vector<uint32_t>& freq) {
    const char magic[4] = {'R', 'C', 'N', 'C'};
    out.insert(out.end(), magic, magic + 4);
    write_u32_le(out, static_cast<uint32_t>(freq.size()));
    for (uint32_t v : freq) {
        write_u32_le(out, v);
    }
}

static bool read_header(const std::vector<uint8_t>& in, size_t& pos, std::vector<uint32_t>& freq) {
    if (in.size() < 8) {
        return false;
    }
    if (in[0] != 'R' || in[1] != 'C' || in[2] != 'N' || in[3] != 'C') {
        return false;
    }
    pos = 4;
    uint32_t count = 0;
    if (!read_u32_le(in, pos, count)) {
        return false;
    }
    if (count == 0 || count > 1024) {
        return false;
    }
    freq.assign(count, 0);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = 0;
        if (!read_u32_le(in, pos, v)) {
            return false;
        }
        freq[i] = v;
    }
    return true;
}

class RangeEncoder {
public:
    explicit RangeEncoder(std::vector<uint8_t>& out)
        : out_(out), low_(0), high_(0xFFFFFFFFu) {}

    void encode_symbol(uint32_t symbol, const std::vector<uint32_t>& cumulative) {
        uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
        uint64_t total = cumulative.back();
        uint64_t symLow = cumulative[symbol];
        uint64_t symHigh = cumulative[symbol + 1];

        high_ = low_ + static_cast<uint32_t>((range * symHigh) / total - 1);
        low_ = low_ + static_cast<uint32_t>((range * symLow) / total);

        while ((low_ ^ high_) < RENORM_THRESHOLD) {
            uint8_t byte = static_cast<uint8_t>(low_ >> 24);
            out_.push_back(byte);
            low_ <<= 8;
            high_ = (high_ << 8) | 0xFFu;
        }
    }

    void finish() {
        for (int i = 0; i < 4; ++i) {
            uint8_t byte = static_cast<uint8_t>(low_ >> 24);
            out_.push_back(byte);
            low_ <<= 8;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint32_t low_;
    uint32_t high_;
};

class RangeDecoder {
public:
    RangeDecoder(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0), low_(0), high_(0xFFFFFFFFu), code_(0) {
        for (int i = 0; i < 4; ++i) {
            code_ = (code_ << 8) | read_byte();
        }
    }

    uint32_t decode_symbol(const std::vector<uint32_t>& cumulative) {
        uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
        uint64_t total = cumulative.back();
        uint64_t offset = code_ - low_;
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

        high_ = low_ + static_cast<uint32_t>((range * symHigh) / total - 1);
        low_ = low_ + static_cast<uint32_t>((range * symLow) / total);

        while ((low_ ^ high_) < RENORM_THRESHOLD) {
            low_ <<= 8;
            high_ = (high_ << 8) | 0xFFu;
            code_ = (code_ << 8) | read_byte();
        }

        return symbol;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    uint32_t low_;
    uint32_t high_;
    uint32_t code_;

    uint32_t read_byte() {
        if (pos_ < size_) {
            return static_cast<uint32_t>(data_[pos_++]);
        }
        return 0;
    }
};

std::vector<uint8_t> encode(const std::vector<uint8_t>& data) {
    std::vector<uint32_t> freq = build_frequencies_from_data(data);
    std::vector<uint32_t> cumulative = build_cumulative(freq);

    std::vector<uint8_t> out;
    write_header(out, freq);

    RangeEncoder encoder(out);
    for (uint8_t b : data) {
        encoder.encode_symbol(static_cast<uint32_t>(b), cumulative);
    }
    encoder.encode_symbol(EOF_SYMBOL, cumulative);
    encoder.finish();

    return out;
}

std::vector<uint8_t> decode(const std::vector<uint8_t>& encoded) {
    size_t pos = 0;
    std::vector<uint32_t> freq;
    if (!read_header(encoded, pos, freq)) {
        throw std::runtime_error("Invalid range-coded stream");
    }
    if (freq.size() != SYMBOL_LIMIT) {
        throw std::runtime_error("Unexpected symbol count in header");
    }
    std::vector<uint32_t> cumulative = build_cumulative(freq);

    std::vector<uint8_t> out;
    if (pos >= encoded.size()) {
        return out;
    }

    RangeDecoder decoder(encoded.data() + pos, encoded.size() - pos);
    for (;;) {
        uint32_t sym = decoder.decode_symbol(cumulative);
        if (sym == EOF_SYMBOL) {
            break;
        }
        out.push_back(static_cast<uint8_t>(sym));
    }

    return out;
}

} // namespace range_coder

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open input file");
    }
    in.seekg(0, std::ios::end);
    std::streampos size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), size);
    }
    return data;
}

static void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open output file");
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

static void run_benchmark(std::size_t size_bytes, int iterations) {
    std::vector<uint8_t> data(size_bytes);
    for (std::size_t i = 0; i < size_bytes; ++i) {
        data[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
    }

    using clock = std::chrono::high_resolution_clock;

    std::vector<uint8_t> encoded;
    auto start_enc = clock::now();
    for (int i = 0; i < iterations; ++i) {
        encoded = range_coder::encode(data);
    }
    auto end_enc = clock::now();
    std::chrono::duration<double> enc_dur = end_enc - start_enc;

    std::vector<uint8_t> decoded;
    auto start_dec = clock::now();
    for (int i = 0; i < iterations; ++i) {
        decoded = range_coder::decode(encoded);
    }
    auto end_dec = clock::now();
    std::chrono::duration<double> dec_dur = end_dec - start_dec;

    if (decoded != data) {
        std::cerr << "Benchmark decode result mismatch!" << std::endl;
    }

    double total_mb = static_cast<double>(size_bytes) * iterations / (1024.0 * 1024.0);
    std::cout << "C++ range coder benchmark" << std::endl;
    std::cout << "Input size: " << size_bytes << " bytes" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Encoded size (last run): " << encoded.size() << " bytes" << std::endl;
    std::cout << "Encode time: " << enc_dur.count() << " s, throughput: "
              << (total_mb / enc_dur.count()) << " MiB/s" << std::endl;
    std::cout << "Decode time: " << dec_dur.count() << " s, throughput: "
              << (total_mb / dec_dur.count()) << " MiB/s" << std::endl;
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " encode input output\n";
            std::cerr << "       " << argv[0] << " decode input output\n";
            std::cerr << "       " << argv[0] << " bench [size_bytes] [iterations]\n";
            return 1;
        }
        std::string mode = argv[1];
        if (mode == "encode") {
            if (argc != 4) {
                std::cerr << "Usage: " << argv[0] << " encode input output\n";
                return 1;
            }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            std::vector<uint8_t> data = read_file(inputPath);
            std::vector<uint8_t> encoded = range_coder::encode(data);
            write_file(outputPath, encoded);
        } else if (mode == "decode") {
            if (argc != 4) {
                std::cerr << "Usage: " << argv[0] << " decode input output\n";
                return 1;
            }
            std::string inputPath = argv[2];
            std::string outputPath = argv[3];
            std::vector<uint8_t> encoded = read_file(inputPath);
            std::vector<uint8_t> decoded = range_coder::decode(encoded);
            write_file(outputPath, decoded);
        } else if (mode == "bench") {
            std::size_t size_bytes = 1u << 20; // 1 MiB
            int iterations = 20;
            if (argc >= 3) {
                size_bytes = static_cast<std::size_t>(std::stoul(argv[2]));
            }
            if (argc >= 4) {
                iterations = std::stoi(argv[3]);
            }
            run_benchmark(size_bytes, iterations);
        } else {
            std::cerr << "Unknown mode\n";
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
