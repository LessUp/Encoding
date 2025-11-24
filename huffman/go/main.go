package main

import (
	"bufio"
	"container/heap"
	"encoding/binary"
	"fmt"
	"io"
	"os"
)

const (
	SymbolLimit = 257
	EOFSymbol   = SymbolLimit - 1
)

type Node struct {
	symbol uint32
	freq   uint64
	left   *Node
	right  *Node
}

func isLeaf(n *Node) bool {
	return n.left == nil && n.right == nil
}

type nodeHeap []*Node

func (h nodeHeap) Len() int { return len(h) }

func (h nodeHeap) Less(i, j int) bool {
	if h[i].freq != h[j].freq {
		return h[i].freq < h[j].freq
	}
	return h[i].symbol < h[j].symbol
}

func (h nodeHeap) Swap(i, j int) { h[i], h[j] = h[j], h[i] }

func (h *nodeHeap) Push(x interface{}) {
	*h = append(*h, x.(*Node))
}

func (h *nodeHeap) Pop() interface{} {
	old := *h
	n := len(old)
	x := old[n-1]
	*h = old[:n-1]
	return x
}

func buildTree(freq []uint32) *Node {
	h := &nodeHeap{}
	heap.Init(h)
	for s := 0; s < SymbolLimit; s++ {
		if freq[s] == 0 {
			continue
		}
		n := &Node{
			symbol: uint32(s),
			freq:   uint64(freq[s]),
			left:   nil,
			right:  nil,
		}
		heap.Push(h, n)
	}
	if h.Len() == 0 {
		return &Node{symbol: uint32(EOFSymbol), freq: 1}
	}
	if h.Len() == 1 {
		only := heap.Pop(h).(*Node)
		parent := &Node{symbol: 0, freq: only.freq, left: only, right: nil}
		heap.Push(h, parent)
	}
	for h.Len() > 1 {
		a := heap.Pop(h).(*Node)
		b := heap.Pop(h).(*Node)
		parent := &Node{
			symbol: 0,
			freq:   a.freq + b.freq,
			left:   a,
			right:  b,
		}
		heap.Push(h, parent)
	}
	return heap.Pop(h).(*Node)
}

type BitWriter struct {
	w            *bufio.Writer
	buffer       byte
	bitsInBuffer uint8
}

func NewBitWriter(w io.Writer) *BitWriter {
	return &BitWriter{w: bufio.NewWriter(w)}
}

func (b *BitWriter) WriteBit(bit int) error {
	b.buffer = (b.buffer << 1) | byte(bit&1)
	b.bitsInBuffer++
	if b.bitsInBuffer == 8 {
		if err := b.w.WriteByte(b.buffer); err != nil {
			return err
		}
		b.bitsInBuffer = 0
		b.buffer = 0
	}
	return nil
}

func (b *BitWriter) Flush() error {
	if b.bitsInBuffer > 0 {
		b.buffer <<= (8 - b.bitsInBuffer)
		if err := b.w.WriteByte(b.buffer); err != nil {
			return err
		}
		b.bitsInBuffer = 0
		b.buffer = 0
	}
	return b.w.Flush()
}

type BitReader struct {
	r             *bufio.Reader
	currentByte   byte
	bitsRemaining uint8
	reachedEOF    bool
}

func NewBitReader(r *bufio.Reader) *BitReader {
	return &BitReader{r: r}
}

func (b *BitReader) ReadBit() int {
	if b.bitsRemaining == 0 {
		c, err := b.r.ReadByte()
		if err != nil {
			b.reachedEOF = true
			return 0
		}
		b.currentByte = c
		b.bitsRemaining = 8
	}
	b.bitsRemaining--
	return int((b.currentByte >> b.bitsRemaining) & 1)
}

func (b *BitReader) EOF() bool {
	return b.reachedEOF
}

func buildFrequenciesFromFile(path string) []uint32 {
	freq := make([]uint32, SymbolLimit)
	f, err := os.Open(path)
	if err != nil {
		freq[EOFSymbol] = 1
		return freq
	}
	defer f.Close()
	r := bufio.NewReader(f)
	for {
		b, err := r.ReadByte()
		if err != nil {
			break
		}
		freq[int(b)]++
	}
	freq[EOFSymbol] = 1
	return freq
}

func writeFrequencies(w io.Writer, freq []uint32) error {
	count := uint32(len(freq))
	if err := binary.Write(w, binary.LittleEndian, count); err != nil {
		return err
	}
	for _, v := range freq {
		if err := binary.Write(w, binary.LittleEndian, v); err != nil {
			return err
		}
	}
	return nil
}

func readFrequencies(r io.Reader) []uint32 {
	var count uint32
	if err := binary.Read(r, binary.LittleEndian, &count); err != nil || count == 0 || count > 1024 {
		freq := make([]uint32, SymbolLimit)
		for i := range freq {
			freq[i] = 1
		}
		return freq
	}
	freq := make([]uint32, count)
	if err := binary.Read(r, binary.LittleEndian, freq); err != nil {
		f := make([]uint32, SymbolLimit)
		for i := range f {
			f[i] = 1
		}
		return f
	}
	if len(freq) != SymbolLimit {
		f := make([]uint32, SymbolLimit)
		for i := range f {
			f[i] = 1
		}
		return f
	}
	return freq
}

func buildCodes(node *Node, codes []string, prefix []byte) {
	if node == nil {
		return
	}
	if isLeaf(node) {
		if len(prefix) == 0 {
			codes[int(node.symbol)] = "0"
		} else {
			codes[int(node.symbol)] = string(append([]byte(nil), prefix...))
		}
		return
	}
	prefix = append(prefix, '0')
	buildCodes(node.left, codes, prefix)
	prefix[len(prefix)-1] = '1'
	buildCodes(node.right, codes, prefix)
}

func compressFile(inputPath, outputPath string) {
	freq := buildFrequenciesFromFile(inputPath)
	root := buildTree(freq)
	codes := make([]string, SymbolLimit)
	buildCodes(root, codes, nil)

	inFile, err := os.Open(inputPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Cannot open input file for reading")
		return
	}
	defer inFile.Close()
	outFile, err := os.Create(outputPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Cannot open output file for writing")
		return
	}
	defer outFile.Close()

	if _, err := outFile.Write([]byte{'H', 'F', 'M', 'N'}); err != nil {
		return
	}
	if err := writeFrequencies(outFile, freq); err != nil {
		return
	}
	bw := NewBitWriter(outFile)
	r := bufio.NewReader(inFile)
	for {
		b, err := r.ReadByte()
		if err != nil {
			break
		}
		code := codes[int(b)]
		for i := 0; i < len(code); i++ {
			bit := 0
			if code[i] == '1' {
				bit = 1
			}
			if err := bw.WriteBit(bit); err != nil {
				return
			}
		}
	}
	eofCode := codes[EOFSymbol]
	for i := 0; i < len(eofCode); i++ {
		bit := 0
		if eofCode[i] == '1' {
			bit = 1
		}
		if err := bw.WriteBit(bit); err != nil {
			return
		}
	}
	_ = bw.Flush()
}

func decompressFile(inputPath, outputPath string) {
	inFile, err := os.Open(inputPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Cannot open input file for reading")
		return
	}
	defer inFile.Close()
	r := bufio.NewReader(inFile)

	magic := make([]byte, 4)
	if _, err := io.ReadFull(r, magic); err != nil || magic[0] != 'H' || magic[1] != 'F' || magic[2] != 'M' || magic[3] != 'N' {
		fmt.Fprintln(os.Stderr, "Invalid input file format")
		return
	}

	freq := readFrequencies(r)
	root := buildTree(freq)
	if root == nil {
		return
	}

	outFile, err := os.Create(outputPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Cannot open output file for writing")
		return
	}
	defer outFile.Close()
	w := bufio.NewWriter(outFile)
	defer w.Flush()

	br := NewBitReader(r)
	node := root
	for {
		bit := br.ReadBit()
		if bit == 0 {
			if node.left != nil {
				node = node.left
			} else {
				break
			}
		} else {
			if node.right != nil {
				node = node.right
			} else {
				break
			}
		}
		if isLeaf(node) {
			if node.symbol == uint32(EOFSymbol) {
				break
			}
			if err := w.WriteByte(byte(node.symbol)); err != nil {
				break
			}
			node = root
		}
		if br.EOF() && node == root {
			break
		}
	}
}

func HuffmanEncodeFile(inputPath, outputPath string) {
	compressFile(inputPath, outputPath)
}

func HuffmanDecodeFile(inputPath, outputPath string) {
	decompressFile(inputPath, outputPath)
}

func main() {
	if len(os.Args) != 4 {
		fmt.Fprintf(os.Stderr, "Usage: %s encode|decode input output\n", os.Args[0])
		os.Exit(1)
	}
	mode := os.Args[1]
	inputPath := os.Args[2]
	outputPath := os.Args[3]

	if mode == "encode" {
		HuffmanEncodeFile(inputPath, outputPath)
	} else if mode == "decode" {
		HuffmanDecodeFile(inputPath, outputPath)
	} else {
		fmt.Fprintln(os.Stderr, "Unknown mode")
		os.Exit(1)
	}
}
