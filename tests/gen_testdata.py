#!/usr/bin/env python3
import os
from pathlib import Path
import random

# 统一的测试数据生成脚本
# 会在 tests/data/ 下生成多种分布的二进制测试文件：
# - random_1MiB.bin        随机数据（1 MiB）
# - random_10MiB.bin       随机数据（10 MiB）
# - repetitive_10MiB.bin   大量重复字节，适合 RLE 测试
# - textlike_10MiB.bin     类文本分布，偏向 ASCII 可见字符

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "tests" / "data"


def ensure_dir():
    DATA_DIR.mkdir(parents=True, exist_ok=True)


def generate_random_file(path: Path, size_bytes: int):
    if path.exists() and path.stat().st_size == size_bytes:
        return
    print(f"[gen_testdata] generating random file: {path} ({size_bytes} bytes)")
    with path.open("wb") as f:
        remaining = size_bytes
        chunk_size = 1024 * 1024
        while remaining > 0:
            n = min(remaining, chunk_size)
            f.write(os.urandom(n))
            remaining -= n


def generate_repetitive_file(path: Path, size_bytes: int):
    if path.exists() and path.stat().st_size == size_bytes:
        return
    print(f"[gen_testdata] generating repetitive file: {path} ({size_bytes} bytes)")
    rng = random.Random(1)
    with path.open("wb") as f:
        written = 0
        while written < size_bytes:
            value = rng.randrange(0, 256)
            # 每个 run 的长度在 [4, 4096] 之间，保证有明显重复
            run_len = rng.randint(4, 4096)
            if written + run_len > size_bytes:
                run_len = size_bytes - written
            f.write(bytes([value]) * run_len)
            written += run_len


def generate_textlike_file(path: Path, size_bytes: int):
    if path.exists() and path.stat().st_size == size_bytes:
        return
    print(f"[gen_testdata] generating text-like file: {path} ({size_bytes} bytes)")
    rng = random.Random(2)
    alphabet = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" "  \n.,;:!?-_")
    alphabet_bytes = alphabet.encode("ascii")
    with path.open("wb") as f:
        remaining = size_bytes
        chunk_size = 1024 * 1024
        while remaining > 0:
            n = min(remaining, chunk_size)
            buf = bytearray(n)
            for i in range(n):
                buf[i] = alphabet_bytes[rng.randrange(0, len(alphabet_bytes))]
            f.write(buf)
            remaining -= n


def main():
    ensure_dir()
    generate_random_file(DATA_DIR / "random_1MiB.bin", 1 * 1024 * 1024)
    generate_random_file(DATA_DIR / "random_10MiB.bin", 10 * 1024 * 1024)
    generate_repetitive_file(DATA_DIR / "repetitive_10MiB.bin", 10 * 1024 * 1024)
    generate_textlike_file(DATA_DIR / "textlike_10MiB.bin", 10 * 1024 * 1024)
    print("[gen_testdata] done")


if __name__ == "__main__":
    main()
