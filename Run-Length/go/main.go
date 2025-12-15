package main

import (
    "bufio"
    "encoding/binary"
    "fmt"
    "io"
    "os"
)

// Run-Length 编码实现。
// 格式：重复写入 4 字节小端无符号整数 count + 1 字节 value，直到输入结束。
// 三种语言（C++/Go/Rust）都使用相同的格式，方便交叉解码与基准测试。

// RLEEncodeFile 对整个文件执行 Run-Length 编码。
func rleEncodeFile(inputPath, outputPath string) error {
    in, err := os.Open(inputPath)
    if err != nil {
        return fmt.Errorf("无法打开输入文件用于读取: %s: %w", inputPath, err)
    }
    defer in.Close()

    out, err := os.Create(outputPath)
    if err != nil {
        return fmt.Errorf("无法打开输出文件用于写入: %s: %w", outputPath, err)
    }
    defer out.Close()

    r := bufio.NewReader(in)
    w := bufio.NewWriter(out)

    first, err := r.ReadByte()
    if err == io.EOF {
        // 空文件，编码结果也是空文件。
        if err := w.Flush(); err != nil {
            return err
        }
        return nil
    }
    if err != nil {
        return fmt.Errorf("读取输入文件失败: %w", err)
    }

    current := first
    var count uint32 = 1

    for {
        b, err := r.ReadByte()
        if err == io.EOF {
            // 写出最后一段
            if err := writeRun(w, count, current); err != nil {
                return fmt.Errorf("写入 RLE 数据失败: %w", err)
            }
            break
        }
        if err != nil {
            return fmt.Errorf("读取输入文件失败: %w", err)
        }

        if b == current && count < ^uint32(0) {
            count++
        } else {
            if err := writeRun(w, count, current); err != nil {
                return fmt.Errorf("写入 RLE 数据失败: %w", err)
            }
            current = b
            count = 1
        }
    }

    if err := w.Flush(); err != nil {
        return err
    }
    return nil
}

// RLEEncodeFile 对整个文件执行 Run-Length 编码。
func RLEEncodeFile(inputPath, outputPath string) {
    if err := rleEncodeFile(inputPath, outputPath); err != nil {
        fmt.Fprintln(os.Stderr, err)
    }
}

// writeRun 将单个 (count, value) 段写入输出流。
func writeRun(w *bufio.Writer, count uint32, value byte) error {
    // 写入 4 字节小端 count
    if err := binary.Write(w, binary.LittleEndian, count); err != nil {
        return err
    }
    // 写入 1 字节 value
    if err := w.WriteByte(value); err != nil {
        return err
    }
    return nil
}

// RLEDecodeFile 将 RLE 编码文件解码为原始字节序列。
func rleDecodeFile(inputPath, outputPath string) error {
    in, err := os.Open(inputPath)
    if err != nil {
        return fmt.Errorf("无法打开输入文件用于读取: %s: %w", inputPath, err)
    }
    defer in.Close()

    out, err := os.Create(outputPath)
    if err != nil {
        return fmt.Errorf("无法打开输出文件用于写入: %s: %w", outputPath, err)
    }
    defer out.Close()

    r := bufio.NewReader(in)
    w := bufio.NewWriter(out)

    buf := make([]byte, 4096)

    for {
        var count uint32
        if err := binary.Read(r, binary.LittleEndian, &count); err != nil {
            if err == io.EOF {
                // 正常 EOF
                break
            }
            if err == io.ErrUnexpectedEOF {
                return fmt.Errorf("RLE 数据截断：无法读取完整的 count 字段")
            }
            return fmt.Errorf("读取 count 失败: %w", err)
        }
        if count == 0 {
            return fmt.Errorf("RLE 数据非法：count 不应为 0")
        }

        value, err := r.ReadByte()
        if err != nil {
            if err == io.EOF {
                return fmt.Errorf("RLE 数据截断：缺少 value 字节")
            }
            return fmt.Errorf("读取 value 失败: %w", err)
        }

        // 将 (count, value) 展开写回输出
        for count > 0 {
            chunk := int(count)
            if chunk > len(buf) {
                chunk = len(buf)
            }
            for i := 0; i < chunk; i++ {
                buf[i] = value
            }
            if _, err := w.Write(buf[:chunk]); err != nil {
                return fmt.Errorf("写入解码数据失败: %w", err)
            }
            count -= uint32(chunk)
        }
    }

    if err := w.Flush(); err != nil {
        return err
    }
    return nil
}

// RLEDecodeFile 将 RLE 编码文件解码为原始字节序列。
func RLEDecodeFile(inputPath, outputPath string) {
    if err := rleDecodeFile(inputPath, outputPath); err != nil {
        fmt.Fprintln(os.Stderr, err)
    }
}

func main() {
    if len(os.Args) != 4 {
        fmt.Fprintf(os.Stderr, "用法: %s encode|decode input output\n", os.Args[0])
        os.Exit(1)
    }

    mode := os.Args[1]
    inputPath := os.Args[2]
    outputPath := os.Args[3]

    var err error
    switch mode {
    case "encode":
        err = rleEncodeFile(inputPath, outputPath)
    case "decode":
        err = rleDecodeFile(inputPath, outputPath)
    default:
        fmt.Fprintln(os.Stderr, "未知模式，应为 encode 或 decode")
        os.Exit(1)
    }
    if err != nil {
        fmt.Fprintln(os.Stderr, err)
        os.Exit(1)
    }
}
