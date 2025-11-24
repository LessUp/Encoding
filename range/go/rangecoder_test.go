package rangecoder

import (
	"bytes"
	"math/rand"
	"testing"
	"time"
)

func makeTestData(n int) []byte {
	b := make([]byte, n)
	r := rand.New(rand.NewSource(1))
	if n > 0 {
		_, _ = r.Read(b)
	}
	return b
}

func TestRoundTripEmpty(t *testing.T) {
	data := []byte{}
	enc, err := Encode(data)
	if err != nil {
		t.Fatalf("encode error: %v", err)
	}
	dec, err := Decode(enc)
	if err != nil {
		t.Fatalf("decode error: %v", err)
	}
	if !bytes.Equal(dec, data) {
		t.Fatalf("mismatch: got %v, want %v", dec, data)
	}
}

func TestRoundTripRandom(t *testing.T) {
	data := makeTestData(10000)
	enc, err := Encode(data)
	if err != nil {
		t.Fatalf("encode error: %v", err)
	}
	dec, err := Decode(enc)
	if err != nil {
		t.Fatalf("decode error: %v", err)
	}
	if !bytes.Equal(dec, data) {
		t.Fatalf("mismatch: decoded data differs from original")
	}
}

func BenchmarkEncodeDecode1MiB(b *testing.B) {
	data := makeTestData(1 << 20)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		enc, err := Encode(data)
		if err != nil {
			b.Fatalf("encode error: %v", err)
		}
		dec, err := Decode(enc)
		if err != nil {
			b.Fatalf("decode error: %v", err)
		}
		if len(dec) != len(data) {
			b.Fatalf("length mismatch: got %d, want %d", len(dec), len(data))
		}
	}
}

func BenchmarkEncodeDecode4MiB(b *testing.B) {
	data := makeTestData(4 << 20)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		enc, err := Encode(data)
		if err != nil {
			b.Fatalf("encode error: %v", err)
		}
		dec, err := Decode(enc)
		if err != nil {
			b.Fatalf("decode error: %v", err)
		}
		if len(dec) != len(data) {
			b.Fatalf("length mismatch: got %d, want %d", len(dec), len(data))
		}
	}
}

func TestDeterministic(t *testing.T) {
	data := makeTestData(1 << 16)
	enc1, err := Encode(data)
	if err != nil {
		t.Fatalf("encode1 error: %v", err)
	}
	time.Sleep(10 * time.Millisecond)
	enc2, err := Encode(data)
	if err != nil {
		t.Fatalf("encode2 error: %v", err)
	}
	if !bytes.Equal(enc1, enc2) {
		t.Fatalf("encodings not deterministic")
	}
}
