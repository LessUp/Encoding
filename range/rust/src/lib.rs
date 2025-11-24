use std::error::Error;
use std::fmt;

const SYMBOL_LIMIT: usize = 257;
const EOF_SYMBOL: usize = SYMBOL_LIMIT - 1;
const MAX_TOTAL: u32 = 1 << 24;
const RENORM_THRESHOLD: u32 = 1 << 24;

#[derive(Debug, Clone)]
pub struct RangeError(&'static str);

impl fmt::Display for RangeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl Error for RangeError {}

fn scale_frequencies(freq: &mut [u32]) {
    let mut total: u64 = freq.iter().map(|&f| f as u64).sum();
    if total == 0 {
        for f in freq.iter_mut() {
            *f = 1;
        }
        return;
    }
    if total <= MAX_TOTAL as u64 {
        return;
    }
    let mut new_total: u64 = 0;
    for f in freq.iter_mut() {
        if *f == 0 {
            continue;
        }
        let mut scaled = (*f as u64 * MAX_TOTAL as u64) / total;
        if scaled == 0 {
            scaled = 1;
        }
        *f = scaled as u32;
        new_total += scaled;
    }
    if new_total == 0 {
        let mut base = MAX_TOTAL / freq.len() as u32;
        if base == 0 {
            base = 1;
        }
        for f in freq.iter_mut() {
            *f = base;
        }
    }
}

fn build_frequencies(data: &[u8]) -> Vec<u32> {
    let mut freq = vec![0u32; SYMBOL_LIMIT];
    for &b in data {
        freq[b as usize] += 1;
    }
    freq[EOF_SYMBOL] = 1;
    scale_frequencies(&mut freq);
    freq
}

fn build_cumulative(freq: &[u32]) -> Vec<u32> {
    let mut cumulative = vec![0u32; freq.len() + 1];
    for (i, &f) in freq.iter().enumerate() {
        cumulative[i + 1] = cumulative[i] + f;
    }
    if let Some(&last) = cumulative.last() {
        if last == 0 {
            for i in 0..freq.len() {
                cumulative[i + 1] = (i + 1) as u32;
            }
        }
    }
    cumulative
}

fn write_u32_le(out: &mut Vec<u8>, v: u32) {
    out.push((v & 0xFF) as u8);
    out.push(((v >> 8) & 0xFF) as u8);
    out.push(((v >> 16) & 0xFF) as u8);
    out.push(((v >> 24) & 0xFF) as u8);
}

fn read_u32_le(input: &[u8], pos: &mut usize) -> Option<u32> {
    if *pos + 4 > input.len() {
        return None;
    }
    let v = (input[*pos] as u32)
        | ((input[*pos + 1] as u32) << 8)
        | ((input[*pos + 2] as u32) << 16)
        | ((input[*pos + 3] as u32) << 24);
    *pos += 4;
    Some(v)
}

fn write_header(out: &mut Vec<u8>, freq: &[u32]) {
    out.extend_from_slice(b"RCNC");
    write_u32_le(out, freq.len() as u32);
    for &v in freq {
        write_u32_le(out, v);
    }
}

fn read_header(input: &[u8], pos: &mut usize) -> Result<Vec<u32>, RangeError> {
    if input.len() < 8 {
        return Err(RangeError("range: input too short"));
    }
    if &input[0..4] != b"RCNC" {
        return Err(RangeError("range: bad magic"));
    }
    *pos = 4;
    let count = read_u32_le(input, pos).ok_or(RangeError("range: truncated header"))?;
    if count == 0 || count > 1024 {
        return Err(RangeError("range: bad symbol count"));
    }
    let mut freq = Vec::with_capacity(count as usize);
    for _ in 0..count {
        let v = read_u32_le(input, pos).ok_or(RangeError("range: truncated frequencies"))?;
        freq.push(v);
    }
    Ok(freq)
}

struct RangeEncoder<'a> {
    low: u32,
    high: u32,
    out: &'a mut Vec<u8>,
}

impl<'a> RangeEncoder<'a> {
    fn new(out: &'a mut Vec<u8>) -> Self {
        RangeEncoder {
            low: 0,
            high: 0xFFFF_FFFF,
            out,
        }
    }

    fn encode_symbol(&mut self, symbol: u32, cumulative: &[u32]) {
        let range = (self.high as u64).wrapping_sub(self.low as u64) + 1;
        let total = *cumulative.last().unwrap() as u64;
        let sym_low = cumulative[symbol as usize] as u64;
        let sym_high = cumulative[symbol as usize + 1] as u64;

        self.high = self
            .low
            .wrapping_add(((range * sym_high) / total - 1) as u32);
        self.low = self
            .low
            .wrapping_add(((range * sym_low) / total) as u32);

        while (self.low ^ self.high) < RENORM_THRESHOLD {
            let byte = (self.low >> 24) as u8;
            self.out.push(byte);
            self.low <<= 8;
            self.high = (self.high << 8) | 0xFF;
        }
    }

    fn finish(&mut self) {
        for _ in 0..4 {
            let byte = (self.low >> 24) as u8;
            self.out.push(byte);
            self.low <<= 8;
        }
    }
}

struct RangeDecoder<'a> {
    low: u32,
    high: u32,
    code: u32,
    data: &'a [u8],
    pos: usize,
}

impl<'a> RangeDecoder<'a> {
    fn new(data: &'a [u8]) -> Self {
        let mut dec = RangeDecoder {
            low: 0,
            high: 0xFFFF_FFFF,
            code: 0,
            data,
            pos: 0,
        };
        for _ in 0..4 {
            let b = dec.read_byte() as u32;
            dec.code = (dec.code << 8) | b;
        }
        dec
    }

    fn read_byte(&mut self) -> u8 {
        if self.pos < self.data.len() {
            let b = self.data[self.pos];
            self.pos += 1;
            b
        } else {
            0
        }
    }

    fn decode_symbol(&mut self, cumulative: &[u32]) -> u32 {
        let range = (self.high as u64).wrapping_sub(self.low as u64) + 1;
        let total = *cumulative.last().unwrap() as u64;
        let offset = (self.code as u64).wrapping_sub(self.low as u64);
        let value = ((offset + 1) * total - 1) / range;

        let mut lo: u32 = 0;
        let mut hi: u32 = cumulative.len() as u32 - 1;
        while lo + 1 < hi {
            let mid = lo + (hi - lo) / 2;
            if cumulative[mid as usize] as u64 > value {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        let symbol = lo;

        let sym_low = cumulative[symbol as usize] as u64;
        let sym_high = cumulative[symbol as usize + 1] as u64;

        self.high = self
            .low
            .wrapping_add(((range * sym_high) / total - 1) as u32);
        self.low = self
            .low
            .wrapping_add(((range * sym_low) / total) as u32);

        while (self.low ^ self.high) < RENORM_THRESHOLD {
            self.low <<= 8;
            self.high = (self.high << 8) | 0xFF;
            let b = self.read_byte() as u32;
            self.code = (self.code << 8) | b;
        }

        symbol
    }
}

pub fn encode(input: &[u8]) -> Result<Vec<u8>, RangeError> {
    let freq = build_frequencies(input);
    let cumulative = build_cumulative(&freq);

    let mut out = Vec::with_capacity(input.len());
    write_header(&mut out, &freq);

    {
        let mut enc = RangeEncoder::new(&mut out);
        for &b in input {
            enc.encode_symbol(b as u32, &cumulative);
        }
        enc.encode_symbol(EOF_SYMBOL as u32, &cumulative);
        enc.finish();
    }

    Ok(out)
}

pub fn decode(encoded: &[u8]) -> Result<Vec<u8>, RangeError> {
    let mut pos: usize = 0;
    let freq = read_header(encoded, &mut pos)?;
    if freq.len() != SYMBOL_LIMIT {
        return Err(RangeError("range: unexpected symbol count"));
    }
    let cumulative = build_cumulative(&freq);

    if pos >= encoded.len() {
        return Ok(Vec::new());
    }

    let mut dec = RangeDecoder::new(&encoded[pos..]);
    let mut out = Vec::with_capacity(encoded.len());
    loop {
        let sym = dec.decode_symbol(&cumulative);
        if sym as usize == EOF_SYMBOL {
            break;
        }
        out.push(sym as u8);
    }

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_empty() {
        let data: Vec<u8> = Vec::new();
        let enc = encode(&data).unwrap();
        let dec = decode(&enc).unwrap();
        assert_eq!(dec, data);
    }

    #[test]
    fn roundtrip_random() {
        use rand::RngCore;
        use rand::SeedableRng;
        let mut data = vec![0u8; 10000];
        let mut rng = rand::rngs::StdRng::seed_from_u64(1);
        rng.fill_bytes(&mut data);
        let enc = encode(&data).unwrap();
        let dec = decode(&enc).unwrap();
        assert_eq!(dec, data);
    }
}
