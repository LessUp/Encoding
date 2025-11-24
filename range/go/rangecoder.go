package rangecoder

import "errors"

const (
	symbolLimit     = 257
	eofSymbol       = symbolLimit - 1
	maxTotal  uint32 = 1 << 24
	renormThreshold  = uint32(1) << 24
)

func scaleFrequencies(freq []uint32) {
	var total uint64
	for _, f := range freq {
		total += uint64(f)
	}
	if total == 0 {
		for i := range freq {
			freq[i] = 1
		}
		return
	}
	if total <= uint64(maxTotal) {
		return
	}
	var newTotal uint64
	for i, f := range freq {
		if f == 0 {
			continue
		}
		scaled := uint64(f) * uint64(maxTotal) / total
		if scaled == 0 {
			scaled = 1
		}
		freq[i] = uint32(scaled)
		newTotal += scaled
	}
	if newTotal == 0 {
		base := maxTotal / uint32(len(freq))
		if base == 0 {
			base = 1
		}
		for i := range freq {
			freq[i] = base
		}
	}
}

func buildFrequencies(data []byte) []uint32 {
	freq := make([]uint32, symbolLimit)
	for _, b := range data {
		freq[int(b)]++
	}
	freq[eofSymbol] = 1
	scaleFrequencies(freq)
	return freq
}

func buildCumulative(freq []uint32) []uint32 {
	cum := make([]uint32, len(freq)+1)
	for i, f := range freq {
		cum[i+1] = cum[i] + f
	}
	if cum[len(cum)-1] == 0 {
		for i := range freq {
			cum[i+1] = uint32(i + 1)
		}
	}
	return cum
}

func writeU32LE(out *[]byte, v uint32) {
	*out = append(*out,
		byte(v&0xFF),
		byte((v>>8)&0xFF),
		byte((v>>16)&0xFF),
		byte((v>>24)&0xFF),
	)
}

func readU32LE(in []byte, pos *int) (uint32, bool) {
	if *pos+4 > len(in) {
		return 0, false
	}
	v := uint32(in[*pos]) |
		uint32(in[*pos+1])<<8 |
		uint32(in[*pos+2])<<16 |
		uint32(in[*pos+3])<<24
	*pos += 4
	return v, true
}

func writeHeader(out *[]byte, freq []uint32) {
	*out = append(*out, 'R', 'C', 'N', 'C')
	writeU32LE(out, uint32(len(freq)))
	for _, v := range freq {
		writeU32LE(out, v)
	}
}

func readHeader(in []byte, pos *int) ([]uint32, error) {
	if len(in) < 8 {
		return nil, errors.New("range: input too short")
	}
	if in[0] != 'R' || in[1] != 'C' || in[2] != 'N' || in[3] != 'C' {
		return nil, errors.New("range: bad magic")
	}
	*pos = 4
	count, ok := readU32LE(in, pos)
	if !ok || count == 0 || count > 1024 {
		return nil, errors.New("range: bad header")
	}
	freq := make([]uint32, count)
	for i := uint32(0); i < count; i++ {
		v, ok := readU32LE(in, pos)
		if !ok {
			return nil, errors.New("range: truncated header")
		}
		freq[i] = v
	}
	return freq, nil
}

type encoder struct {
	low  uint32
	high uint32
	out  *[]byte
}

func newEncoder(out *[]byte) *encoder {
	return &encoder{low: 0, high: 0xFFFFFFFF, out: out}
}

func (e *encoder) encodeSymbol(symbol uint32, cumulative []uint32) {
	rangeVal := uint64(e.high) - uint64(e.low) + 1
	total := uint64(cumulative[len(cumulative)-1])
	symLow := uint64(cumulative[symbol])
	symHigh := uint64(cumulative[symbol+1])

	e.high = e.low + uint32((rangeVal*symHigh)/total-1)
	e.low = e.low + uint32((rangeVal*symLow)/total)

	for (e.low ^ e.high) < renormThreshold {
		b := byte(e.low >> 24)
		*e.out = append(*e.out, b)
		e.low <<= 8
		e.high = (e.high << 8) | 0xFF
	}
}

func (e *encoder) finish() {
	for i := 0; i < 4; i++ {
		b := byte(e.low >> 24)
		*e.out = append(*e.out, b)
		e.low <<= 8
	}
}

type decoder struct {
	low  uint32
	high uint32
	code uint32
	in   []byte
	pos  int
}

func newDecoder(in []byte) *decoder {
	d := &decoder{low: 0, high: 0xFFFFFFFF, in: in}
	for i := 0; i < 4; i++ {
		b := d.readByte()
		d.code = (d.code << 8) | uint32(b)
	}
	return d
}

func (d *decoder) readByte() byte {
	if d.pos < len(d.in) {
		b := d.in[d.pos]
		d.pos++
		return b
	}
	return 0
}

func (d *decoder) decodeSymbol(cumulative []uint32) uint32 {
	rangeVal := uint64(d.high) - uint64(d.low) + 1
	total := uint64(cumulative[len(cumulative)-1])
	offset := uint64(d.code - d.low)
	value := ((offset+1)*total - 1) / rangeVal

	lo := uint32(0)
	hi := uint32(len(cumulative) - 1)
	for lo+1 < hi {
		mid := lo + (hi-lo)/2
		if uint64(cumulative[mid]) > value {
			hi = mid
		} else {
			lo = mid
		}
	}
	symbol := lo

	symLow := uint64(cumulative[symbol])
	symHigh := uint64(cumulative[symbol+1])

	d.high = d.low + uint32((rangeVal*symHigh)/total-1)
	d.low = d.low + uint32((rangeVal*symLow)/total)

	for (d.low ^ d.high) < renormThreshold {
		d.low <<= 8
		d.high = (d.high << 8) | 0xFF
		d.code = (d.code << 8) | uint32(d.readByte())
	}

	return symbol
}

func Encode(input []byte) ([]byte, error) {
	freq := buildFrequencies(input)
	cum := buildCumulative(freq)

	out := make([]byte, 0, len(input))
	writeHeader(&out, freq)

	enc := newEncoder(&out)
	for _, b := range input {
		enc.encodeSymbol(uint32(b), cum)
	}
	enc.encodeSymbol(eofSymbol, cum)
	enc.finish()

	return out, nil
}

func Decode(encoded []byte) ([]byte, error) {
	pos := 0
	freq, err := readHeader(encoded, &pos)
	if err != nil {
		return nil, err
	}
	if len(freq) != symbolLimit {
		return nil, errors.New("range: unexpected symbol count")
	}
	cum := buildCumulative(freq)
	if pos >= len(encoded) {
		return []byte{}, nil
	}

	dec := newDecoder(encoded[pos:])
	out := make([]byte, 0, len(encoded))
	for {
		sym := dec.decodeSymbol(cum)
		if sym == uint32(eofSymbol) {
			break
		}
		out = append(out, byte(sym))
	}
	return out, nil
}
