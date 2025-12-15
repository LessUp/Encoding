use std::cmp::Ordering;
use std::collections::BinaryHeap;
use std::env;
use std::fs::File;
use std::io::{self, Read, Write, BufReader, BufWriter};
use std::process;

const SYMBOL_LIMIT: usize = 257;
const EOF_SYMBOL: u32 = (SYMBOL_LIMIT - 1) as u32;

struct Node {
    symbol: u32,
    freq: u64,
    left: Option<Box<Node>>,
    right: Option<Box<Node>>,
}

fn is_leaf(node: &Node) -> bool {
    node.left.is_none() && node.right.is_none()
}

struct HeapItem {
    freq: u64,
    symbol: u32,
    node: Box<Node>,
}

impl Eq for HeapItem {}

impl PartialEq for HeapItem {
    fn eq(&self, other: &Self) -> bool {
        self.freq == other.freq && self.symbol == other.symbol
    }
}

impl Ord for HeapItem {
    fn cmp(&self, other: &Self) -> Ordering {
        other.freq.cmp(&self.freq).then_with(|| other.symbol.cmp(&self.symbol))
    }
}

impl PartialOrd for HeapItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

fn build_tree(freq: &[u32]) -> Box<Node> {
    let mut heap = BinaryHeap::<HeapItem>::new();
    for (s, &f) in freq.iter().enumerate() {
        if f == 0 {
            continue;
        }
        let node = Box::new(Node {
            symbol: s as u32,
            freq: f as u64,
            left: None,
            right: None,
        });
        heap.push(HeapItem {
            freq: node.freq,
            symbol: node.symbol,
            node,
        });
    }
    if heap.is_empty() {
        return Box::new(Node {
            symbol: EOF_SYMBOL,
            freq: 1,
            left: None,
            right: None,
        });
    }
    if heap.len() == 1 {
        let item = heap.pop().unwrap();
        let only = item.node;
        let parent = Box::new(Node {
            symbol: 0,
            freq: only.freq,
            left: Some(only),
            right: None,
        });
        heap.push(HeapItem {
            freq: parent.freq,
            symbol: parent.symbol,
            node: parent,
        });
    }
    while heap.len() > 1 {
        let a = heap.pop().unwrap().node;
        let b = heap.pop().unwrap().node;
        let freq_sum = a.freq + b.freq;
        let parent = Box::new(Node {
            symbol: 0,
            freq: freq_sum,
            left: Some(a),
            right: Some(b),
        });
        let symbol = parent.symbol;
        heap.push(HeapItem {
            freq: parent.freq,
            symbol,
            node: parent,
        });
    }
    heap.pop().unwrap().node
}

struct BitWriter<W: Write> {
    writer: W,
    buffer: u8,
    bits_in_buffer: u8,
}

impl<W: Write> BitWriter<W> {
    fn new(writer: W) -> Self {
        BitWriter {
            writer,
            buffer: 0,
            bits_in_buffer: 0,
        }
    }

    fn write_bit(&mut self, bit: u8) -> io::Result<()> {
        self.buffer = (self.buffer << 1) | (bit & 1);
        self.bits_in_buffer += 1;
        if self.bits_in_buffer == 8 {
            self.writer.write_all(&[self.buffer])?;
            self.bits_in_buffer = 0;
            self.buffer = 0;
        }
        Ok(())
    }

    fn flush(&mut self) -> io::Result<()> {
        if self.bits_in_buffer > 0 {
            self.buffer <<= 8 - self.bits_in_buffer;
            self.writer.write_all(&[self.buffer])?;
            self.bits_in_buffer = 0;
            self.buffer = 0;
        }
        self.writer.flush()
    }
}

struct BitReader<R: Read> {
    reader: R,
    current_byte: u8,
    bits_remaining: u8,
    reached_eof: bool,
}

impl<R: Read> BitReader<R> {
    fn new(reader: R) -> Self {
        BitReader {
            reader,
            current_byte: 0,
            bits_remaining: 0,
            reached_eof: false,
        }
    }

    fn read_bit(&mut self) -> u8 {
        if self.bits_remaining == 0 {
            let mut buf = [0u8; 1];
            match self.reader.read(&mut buf) {
                Ok(0) | Err(_) => {
                    self.reached_eof = true;
                    return 0;
                }
                Ok(_) => {
                    self.current_byte = buf[0];
                    self.bits_remaining = 8;
                }
            }
        }
        self.bits_remaining -= 1;
        (self.current_byte >> self.bits_remaining) & 1
    }

    fn eof(&self) -> bool {
        self.reached_eof
    }
}

fn build_frequencies_from_file(path: &str) -> Vec<u32> {
    let mut freq = vec![0u32; SYMBOL_LIMIT];
    if let Ok(file) = File::open(path) {
        let mut reader = BufReader::new(file);
        let mut buf = [0u8; 4096];
        loop {
            match reader.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => {
                    for &b in &buf[..n] {
                        freq[b as usize] += 1;
                    }
                }
                Err(_) => break,
            }
        }
    }
    freq[EOF_SYMBOL as usize] = 1;
    freq
}

fn default_frequencies() -> Vec<u32> {
    vec![1u32; SYMBOL_LIMIT]
}

fn write_frequencies<W: Write>(writer: &mut W, freq: &[u32]) -> io::Result<()> {
    let count = freq.len() as u32;
    writer.write_all(&count.to_le_bytes())?;
    for &v in freq {
        writer.write_all(&v.to_le_bytes())?;
    }
    Ok(())
}

fn read_frequencies<R: Read>(reader: &mut R) -> io::Result<Vec<u32>> {
    let mut count_bytes = [0u8; 4];
    reader
        .read_exact(&mut count_bytes)
        .map_err(|e| io::Error::new(e.kind(), format!("读取频率表失败: {e}")))?;

    let count = u32::from_le_bytes(count_bytes) as usize;
    if count != SYMBOL_LIMIT {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("频率表大小异常: {count}"),
        ));
    }

    let mut freq = vec![0u32; count];
    for f in freq.iter_mut() {
        let mut arr = [0u8; 4];
        reader
            .read_exact(&mut arr)
            .map_err(|e| io::Error::new(e.kind(), format!("读取频率表失败: {e}")))?;
        *f = u32::from_le_bytes(arr);
    }

    Ok(freq)
}

fn build_codes(node: &Node, codes: &mut [String], prefix: &mut String) {
    if is_leaf(node) {
        if prefix.is_empty() {
            codes[node.symbol as usize] = "0".to_string();
        } else {
            codes[node.symbol as usize] = prefix.clone();
        }
        return;
    }
    if let Some(ref left) = node.left {
        prefix.push('0');
        build_codes(left, codes, prefix);
        prefix.pop();
    }
    if let Some(ref right) = node.right {
        prefix.push('1');
        build_codes(right, codes, prefix);
        prefix.pop();
    }
}

fn compress_file(input_path: &str, output_path: &str) -> io::Result<()> {
    let freq = build_frequencies_from_file(input_path);
    let root = build_tree(&freq);
    let mut codes = vec![String::new(); SYMBOL_LIMIT];
    let mut prefix = String::new();
    build_codes(&root, &mut codes, &mut prefix);

    let input_file = File::open(input_path)?;
    let mut reader = BufReader::new(input_file);
    let output_file = File::create(output_path)?;
    let mut writer = BufWriter::new(output_file);

    writer.write_all(b"HFMN")?;
    write_frequencies(&mut writer, &freq)?;

    let mut bit_writer = BitWriter::new(writer);
    let mut buf = [0u8; 4096];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        for &b in &buf[..n] {
            let code = &codes[b as usize];
            for ch in code.as_bytes() {
                let bit = if *ch == b'1' { 1 } else { 0 };
                bit_writer.write_bit(bit)?;
            }
        }
    }
    let eof_code = &codes[EOF_SYMBOL as usize];
    for ch in eof_code.as_bytes() {
        let bit = if *ch == b'1' { 1 } else { 0 };
        bit_writer.write_bit(bit)?;
    }
    bit_writer.flush()?;
    Ok(())
}

fn decompress_file(input_path: &str, output_path: &str) -> io::Result<()> {
    let file = File::open(input_path)?;
    let mut reader = BufReader::new(file);
    let mut magic = [0u8; 4];
    reader.read_exact(&mut magic)?;
    if &magic != b"HFMN" {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "输入文件格式非法"));
    }
    let freq = read_frequencies(&mut reader)?;
    let root = build_tree(&freq);

    let output_file = File::create(output_path)?;
    let mut writer = BufWriter::new(output_file);

    let mut bit_reader = BitReader::new(reader);
    let mut node_ref: &Node = &root;
    let mut saw_eof = false;
    loop {
        let bit = bit_reader.read_bit();
        if bit == 0 {
            match node_ref.left {
                Some(ref left) => {
                    node_ref = left;
                }
                None => {
                    return Err(io::Error::new(io::ErrorKind::InvalidData, "输入数据损坏或截断"));
                }
            }
        } else {
            match node_ref.right {
                Some(ref right) => {
                    node_ref = right;
                }
                None => {
                    return Err(io::Error::new(io::ErrorKind::InvalidData, "输入数据损坏或截断"));
                }
            }
        }
        if is_leaf(node_ref) {
            if node_ref.symbol == EOF_SYMBOL {
                saw_eof = true;
                break;
            }
            writer.write_all(&[node_ref.symbol as u8])?;
            node_ref = &root;
        }
        if bit_reader.eof() && std::ptr::eq(node_ref, &root) {
            break;
        }
    }

    if !saw_eof {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "输入数据损坏或截断"));
    }
    writer.flush()?;
    Ok(())
}

pub fn huffman_encode_file(input_path: &str, output_path: &str) -> io::Result<()> {
    compress_file(input_path, output_path)
}

pub fn huffman_decode_file(input_path: &str, output_path: &str) -> io::Result<()> {
    decompress_file(input_path, output_path)
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

    let result = if mode == "encode" {
        huffman_encode_file(input_path, output_path)
    } else if mode == "decode" {
        huffman_decode_file(input_path, output_path)
    } else {
        eprintln!("未知模式，应为 encode 或 decode");
        process::exit(1);
    };

    if let Err(e) = result {
        eprintln!("运行失败: {e}");
        process::exit(1);
    }
}
