use std::env;
use std::fs::File;
use std::io::{self, BufReader, BufWriter, Read, Write};
use std::process;

// 简单的 Run-Length 编码实现。
// 编码格式：反复写入 4 字节小端无符号整数 count + 1 字节 value，直到输入结束。
// 三种语言实现使用完全相同的格式，便于交叉验证与基准测试。

fn write_u32_le<W: Write>(w: &mut W, v: u32) -> io::Result<()> {
    let bytes = v.to_le_bytes();
    w.write_all(&bytes)
}

// 从流中读取一个 32 位小端无符号整数。
// 返回 Ok(Some(v)) 表示成功读取；
// 返回 Ok(None)  表示正常 EOF（一个字节都没读到）；
// 返回 Err(...)  表示读取过程中发生 I/O 或截断错误。
fn read_u32_le<R: Read>(r: &mut R) -> io::Result<Option<u32>> {
    let mut buf = [0u8; 4];
    let mut read = 0usize;
    while read < 4 {
        match r.read(&mut buf[read..]) {
            Ok(0) => {
                if read == 0 {
                    // 正常 EOF
                    return Ok(None);
                } else {
                    return Err(io::Error::new(
                        io::ErrorKind::UnexpectedEof,
                        "RLE 数据截断：无法读取完整的 count 字段",
                    ));
                }
            }
            Ok(n) => {
                read += n;
            }
            Err(e) if e.kind() == io::ErrorKind::Interrupted => {
                continue;
            }
            Err(e) => return Err(e),
        }
    }
    Ok(Some(u32::from_le_bytes(buf)))
}

// 对整个文件进行 Run-Length 编码。
pub fn rle_encode_file(input_path: &str, output_path: &str) -> io::Result<()> {
    let input = File::open(input_path)
        .map_err(|e| io::Error::new(e.kind(), format!("无法打开输入文件用于读取: {input_path}: {e}")))?;
    let mut reader = BufReader::new(input);

    let output = File::create(output_path)
        .map_err(|e| io::Error::new(e.kind(), format!("无法打开输出文件用于写入: {output_path}: {e}")))?;
    let mut writer = BufWriter::new(output);

    let mut first = [0u8; 1];
    let n = reader.read(&mut first)?;
    if n == 0 {
        // 空文件
        writer.flush()?;
        return Ok(());
    }
    let mut current = first[0];
    let mut count: u32 = 1;

    let mut buf = [0u8; 4096];

    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        for &b in &buf[..n] {
            if b == current && count < u32::MAX {
                count += 1;
            } else {
                write_u32_le(&mut writer, count)?;
                writer.write_all(&[current])?;
                current = b;
                count = 1;
            }
        }
    }

    // 写出最后一段
    write_u32_le(&mut writer, count)?;
    writer.write_all(&[current])?;
    writer.flush()?;
    Ok(())
}

// 将 RLE 编码文件解码回原始字节流。
pub fn rle_decode_file(input_path: &str, output_path: &str) -> io::Result<()> {
    let input = File::open(input_path)
        .map_err(|e| io::Error::new(e.kind(), format!("无法打开输入文件用于读取: {input_path}: {e}")))?;
    let mut reader = BufReader::new(input);

    let output = File::create(output_path)
        .map_err(|e| io::Error::new(e.kind(), format!("无法打开输出文件用于写入: {output_path}: {e}")))?;
    let mut writer = BufWriter::new(output);

    const BUF_SIZE: usize = 4096;
    let mut buf = [0u8; BUF_SIZE];

    loop {
        let count_opt = read_u32_le(&mut reader)?;
        let count = match count_opt {
            Some(c) => c,
            None => break, // 正常 EOF
        };
        if count == 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "RLE 数据非法：count 不应为 0",
            ));
        }

        let mut value_buf = [0u8; 1];
        reader.read_exact(&mut value_buf).map_err(|e| {
            io::Error::new(
                e.kind(),
                "RLE 数据截断：缺少 value 字节",
            )
        })?;
        let value = value_buf[0];

        let mut remaining = count;
        while remaining > 0 {
            let chunk = remaining.min(BUF_SIZE as u32) as usize;
            for i in 0..chunk {
                buf[i] = value;
            }
            writer.write_all(&buf[..chunk])?;
            remaining -= chunk as u32;
        }
    }

    writer.flush()?;
    Ok(())
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        eprintln!("用法: {} encode|decode input output", args[0]);
        process::exit(1);
    }

    let mode = &args[1];
    let input_path = &args[2];
    let output_path = &args[3];

    let result = match mode.as_str() {
        "encode" => rle_encode_file(input_path, output_path),
        "decode" => rle_decode_file(input_path, output_path),
        _ => {
            eprintln!("未知模式，应为 encode 或 decode");
            process::exit(1);
        }
    };

    if let Err(e) = result {
        eprintln!("运行失败: {e}");
        process::exit(1);
    }
}
