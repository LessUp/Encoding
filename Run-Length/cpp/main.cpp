#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>

// 简单的 Run-Length 编码实现。
// 编码格式：重复写入 (count, value) 对，直到文件结束。
// - count: 4 字节无符号整数，小端序 (little-endian)，表示后面 value 重复的次数，必须 > 0。
// - value: 1 字节，表示要重复输出的字节值。
// 这种格式非常简单，三种语言的实现保持完全一致，便于交叉解码和基准测试。

static void write_u32_le(std::ostream& out, uint32_t v) {
    unsigned char buf[4];
    buf[0] = static_cast<unsigned char>(v & 0xFFu);
    buf[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    buf[2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    buf[3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

// 从流中读取一个 32 位小端无符号整数。
// 返回值：
//   true  - 成功读取一个完整的值，并写入到 out_value
//   false - 正常到达 EOF（没有读取到任何字节）
// 如遇到截断（只读到部分字节），会在标准错误输出错误信息。
static bool read_u32_le(std::istream& in, uint32_t& out_value) {
    unsigned char buf[4];
    in.read(reinterpret_cast<char*>(buf), 4);
    std::streamsize got = in.gcount();
    if (got == 0) {
        // 正常 EOF
        return false;
    }
    if (got != 4 || !in) {
        std::cerr << "RLE 数据截断：无法读取完整的 count 字段\n";
        in.setstate(std::ios::badbit);
        return false;
    }
    out_value =
        (static_cast<uint32_t>(buf[0])      ) |
        (static_cast<uint32_t>(buf[1]) << 8 ) |
        (static_cast<uint32_t>(buf[2]) << 16) |
        (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

// 对整个文件进行 Run-Length 编码。
// inputPath  为原始二进制文件路径。
// outputPath 为编码后文件路径。
void rle_encode_file(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "无法打开输入文件用于读取: " << inputPath << "\n";
        return;
    }
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "无法打开输出文件用于写入: " << outputPath << "\n";
        return;
    }

    char c;
    if (!in.get(c)) {
        // 空文件，编码结果也是空文件。
        return;
    }
    unsigned char current = static_cast<unsigned char>(c);
    uint32_t count = 1;

    while (in.get(c)) {
        unsigned char b = static_cast<unsigned char>(c);
        if (b == current && count < 0xFFFFFFFFu) {
            ++count;
        } else {
            write_u32_le(out, count);
            out.put(static_cast<char>(current));
            if (!out) {
                std::cerr << "写入 RLE 数据失败\n";
                return;
            }
            current = b;
            count = 1;
        }
    }

    // 写出最后一段
    write_u32_le(out, count);
    out.put(static_cast<char>(current));
    if (!out) {
        std::cerr << "写入 RLE 数据失败\n";
    }
}

// 对 RLE 编码后的文件进行解码，还原原始字节流。
// inputPath  为已编码文件路径。
// outputPath 为解码输出文件路径。
void rle_decode_file(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "无法打开输入文件用于读取: " << inputPath << "\n";
        return;
    }
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "无法打开输出文件用于写入: " << outputPath << "\n";
        return;
    }

    const std::size_t BUF_SIZE = 4096;
    char buffer[BUF_SIZE];

    while (true) {
        uint32_t count = 0;
        if (!read_u32_le(in, count)) {
            if (!in.bad()) {
                // 正常 EOF
                break;
            } else {
                // read_u32_le 已输出错误信息
                return;
            }
        }
        if (count == 0) {
            std::cerr << "RLE 数据非法：count 不应为 0\n";
            return;
        }

        char valueChar;
        if (!in.get(valueChar)) {
            std::cerr << "RLE 数据截断：缺少 value 字节\n";
            return;
        }
        unsigned char value = static_cast<unsigned char>(valueChar);

        while (count > 0) {
            std::size_t chunk = static_cast<std::size_t>(std::min<uint32_t>(count, static_cast<uint32_t>(BUF_SIZE)));
            std::memset(buffer, static_cast<int>(value), chunk);
            out.write(buffer, static_cast<std::streamsize>(chunk));
            if (!out) {
                std::cerr << "写入解码数据失败\n";
                return;
            }
            count -= static_cast<uint32_t>(chunk);
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "用法: " << argv[0] << " encode|decode input output\n";
        return 1;
    }
    std::string mode = argv[1];
    std::string inputPath = argv[2];
    std::string outputPath = argv[3];

    if (mode == "encode") {
        rle_encode_file(inputPath, outputPath);
    } else if (mode == "decode") {
        rle_decode_file(inputPath, outputPath);
    } else {
        std::cerr << "未知模式: " << mode << "，应为 encode 或 decode\n";
        return 1;
    }

    return 0;
}
