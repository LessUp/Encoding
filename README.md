# encoding 编码算法集合

这是一个用多种语言实现经典压缩编码算法的学习与对比项目。目前包含：

- Huffman 编码
- 算术编码 (Arithmetic coding)
- 区间编码 (Range coder)
- Run-Length 编码 (RLE)

所有实现均以 **字节流** 为输入/输出，重点关注：

- 实现的可读性与教学性
- 不同语言之间的性能对比
- 简单一致的命令行接口与基准测试脚本

---

## 目录结构

- **huffman/**
  - **cpp/**  C++ 实现，`main.cpp` 提供 `huffman_encode_file` / `huffman_decode_file` 以及 CLI
  - **go/**   Go 实现，`main.go` 提供 `HuffmanEncodeFile` / `HuffmanDecodeFile` 以及 CLI
  - **rust/** Rust 实现，`main.rs` 提供 `huffman_encode_file` / `huffman_decode_file` 以及 CLI
  - **benchmark/**  跨语言 benchmark 脚本 `bench.py`
- **arithmetic/**
  - **cpp/** C++ 算术编码实现，`main.cpp` 提供文件级 encode/decode 与 CLI
- **range/**
  - **cpp/**  预留 C++ 实现目录
  - **go/**   Go 区间编码库 `rangecoder.go`，以及 `rangecoder_test.go` 中的基准测试
  - **rust/** Rust crate `rangecoder`，`src/lib.rs` 为库，`src/bin/bench.rs` 为基准程序
- **Run-Length/**
  - **cpp/**  C++ RLE 实现，`main.cpp` 提供 `rle_encode_file` / `rle_decode_file` 与 CLI
  - **go/**   Go RLE 实现，`main.go` 提供 `RLEEncodeFile` / `RLEDecodeFile` 与 CLI
  - **rust/** Rust RLE 实现，`main.rs` 提供 `rle_encode_file` / `rle_decode_file` 与 CLI
  - **benchmark/**  跨语言 RLE benchmark 脚本 `bench.py`

---

## 各算法简介

### Huffman 编码

- 基于前缀码的无损压缩算法。
- 实现中先扫描输入统计频率，构建 Huffman 树，再按位写入编码结果。
- 三种语言实现共享相同的文件头与频率表格式，支持交叉验证和对比。

### 算术编码 (Arithmetic coding)

- 使用区间逐步细分表示整段消息的概率，压缩效率更接近信息熵上界。
- 当前提供 C++ 版本，使用固定精度区间与频率缩放策略，支持文件级 encode/decode。

### 区间编码 (Range coder)

- 一种等价于算术编码的实现方式，但常在实践中更高效。
- Go 与 Rust 版本以 **字节切片** 为输入/输出，提供 `Encode` / `Decode` 两个核心 API：
  - Go: `rangecoder.Encode(data []byte) ([]byte, error)` / `rangecoder.Decode(encoded []byte) ([]byte, error)`
  - Rust: `rangecoder::encode(input: &[u8]) -> Result<Vec<u8>, RangeError>` / `rangecoder::decode(encoded: &[u8]) -> Result<Vec<u8>, RangeError>`
- Rust 通过 `src/bin/bench.rs` 提供基准程序；Go 在 `rangecoder_test.go` 中提供 `go test -bench` 基准。

### Run-Length 编码 (RLE)

- 适用于包含大量 **相同字节连续重复** 的数据。
- 本项目中三种语言使用完全一致且极其简单的二进制格式：
  - 反复写入 `(count, value)` 对，直到文件结束；
  - `count` 为 4 字节无符号整数，小端序 (little-endian)，表示 `value` 的重复次数，`count > 0`；
  - `value` 为 1 字节，表示要重复输出的字节值。
- C++ / Go / Rust 版本都提供文件级接口：
  - C++: `void rle_encode_file(const std::string& input, const std::string& output);`
  - Go:  `func RLEEncodeFile(inputPath, outputPath string)` / `RLEDecodeFile(...)`
  - Rust: `pub fn rle_encode_file(input: &str, output: &str) -> io::Result<()>` / `rle_decode_file(...)`
- 三种实现都按相同格式编码，因此任意语言编码的结果都可以被其他语言正确解码。

---

## 构建与运行示例

下面示例均假设当前工作目录为仓库根目录 `encoding/`。

### Huffman 跨语言 benchmark

- **运行 benchmark：**

```bash
cd huffman/benchmark
python3 bench.py            # 自动生成随机输入数据
# 或
python3 bench.py /path/to/input.bin
```

脚本会：

- **编译** C++ / Go / Rust 三个实现；
- 分别执行 `encode` 和 `decode`，校验解码结果是否与原始输入一致；
- 打印每种语言的 **编译时间、编码/解码耗时、压缩比**。

### Range coder 基准测试

- **Rust：**

```bash
cd range/rust
cargo run --bin bench --release
```

- **Go：**

```bash
cd range/go
go test -bench .
```

### Run-Length (RLE) CLI 使用

以 Linux 为例：

- **C++：**

```bash
cd Run-Length/cpp
g++ -std=c++17 -O2 main.cpp -o rle_cpp

# 编码
./rle_cpp encode ../../huffman/benchmark/tmp/bench_input.bin out.rle
# 解码
./rle_cpp decode out.rle restored.bin
```

- **Go：**

```bash
cd Run-Length/go
go build -o rle_go .

./rle_go encode ../../huffman/benchmark/tmp/bench_input.bin out.rle
./rle_go decode out.rle restored.bin
```

- **Rust：**

```bash
cd Run-Length/rust
rustc -O main.rs -o rle_rust

./rle_rust encode ../../huffman/benchmark/tmp/bench_input.bin out.rle
./rle_rust decode out.rle restored.bin
```

在三种语言中，命令行接口保持一致：

```text
<程序名> encode input_file output_file
<程序名> decode input_file output_file
```

### Run-Length 跨语言 benchmark

- **运行 benchmark：**

```bash
cd Run-Length/benchmark
python3 bench.py            # 默认生成 10 MiB 随机输入
# 或
python3 bench.py /path/to/input.bin
```

输出与 Huffman 的 benchmark 类似，会显示：

- 每种语言的构建时间；
- 编码/解码耗时以及总耗时；
- RLE 压缩后的文件大小与原始大小的比值（压缩比）。

---

## 后续扩展建议

- **更多数据集**：引入实际文本、图像或日志数据进行更真实的压缩效果对比。
- **错误处理**：在需要时对截断/损坏数据提供更细粒度的错误类型。
- **API 扩展**：为 RLE 和 Huffman 等实现增加基于内存缓冲区的 `encode(Vec<u8>) -> Vec<u8>` 接口，便于嵌入其他项目。
