#!/usr/bin/env python3
import sys
import time
import subprocess
from pathlib import Path

# Arithmetic C++ 单语言 benchmark 脚本
# - 编译 arithmetic/cpp/main.cpp -> arithmetic_cpp
# - 在指定输入文件上执行 encode/decode
# - 校验解码结果与原始输入是否一致
#
# 用法：
#   python3 bench.py /path/to/input.bin
# 若未提供参数，则默认使用项目根目录下 tests/data/random_10MiB.bin

ROOT = Path(__file__).resolve().parent.parent
CPP_DIR = ROOT / "cpp"
PROJECT_ROOT = ROOT.parent
TEST_DATA_DIR = PROJECT_ROOT / "tests" / "data"


def run(cmd, cwd):
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    end = time.perf_counter()
    if proc.returncode != 0:
        sys.stderr.write(f"Command failed: {' '.join(map(str, cmd))}\n")
        if proc.stderr:
            sys.stderr.write(proc.stderr.decode(errors="ignore") + "\n")
        sys.exit(1)
    return end - start


def files_equal(a: Path, b: Path) -> bool:
    with a.open("rb") as fa, b.open("rb") as fb:
        while True:
            ba = fa.read(65536)
            bb = fb.read(65536)
            if ba != bb:
                return False
            if not ba:
                return True


def main():
    if len(sys.argv) >= 2:
        input_path = Path(sys.argv[1]).resolve()
        if not input_path.is_file():
            sys.stderr.write("Input file does not exist\n")
            sys.exit(1)
    else:
        input_path = TEST_DATA_DIR / "random_10MiB.bin"

    if not input_path.is_file():
        sys.stderr.write(f"Input file not found: {input_path}\n")
        sys.stderr.write("请先运行 tests/gen_testdata.py 生成测试数据\n")
        sys.exit(1)

    print(f"Arithmetic C++ benchmark, input = {input_path}, size = {input_path.stat().st_size} bytes")

    build_t = run(["g++", "-std=c++17", "-O2", "main.cpp", "-o", "arithmetic_cpp"], CPP_DIR)

    exe = CPP_DIR / "arithmetic_cpp"
    tmp_dir = ROOT / "benchmark" / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    encoded = tmp_dir / "arith.enc"
    decoded = tmp_dir / "arith.out"

    enc_t = run([str(exe), "encode", str(input_path), str(encoded)], cwd=CPP_DIR)
    dec_t = run([str(exe), "decode", str(encoded), str(decoded)], cwd=CPP_DIR)

    if not files_equal(input_path, decoded):
        sys.stderr.write("decode output mismatch\n")
        sys.exit(1)

    original_size = input_path.stat().st_size
    comp_size = encoded.stat().st_size
    ratio = comp_size / original_size if original_size > 0 else 0.0

    print(f"Build time: {build_t:.4f} s")
    print("Runtime (seconds) and compression ratio:")
    print("encode  decode  total  ratio")
    total = enc_t + dec_t
    print(f"{enc_t:6.4f}  {dec_t:6.4f}  {total:6.4f}  {ratio:6.3f}")


if __name__ == "__main__":
    main()
