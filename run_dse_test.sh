#!/usr/bin/env zsh

# rebuild plugin
clang++ -std=c++17 -fPIC -shared MemorySSADemo.cpp \
  -o libMemorySSADemo.dylib \
  $(llvm-config --cxxflags --ldflags --system-libs --libs core analysis transformutils passes)

# helper to run one test
run_one() {
  local src="$1"
  local base="${src%.c}"
  echo "== $src =="

  clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm "$src" -o "${base}.ll"
  opt -passes=mem2reg "${base}.ll" -S -o "${base}_simplified.ll"

  opt -load-pass-plugin=./libMemorySSADemo.dylib \
      -passes="dead-store-elim" \
      -S "${base}_simplified.ll" -o "${base}_dse.ll"

  diff -u "${base}_simplified.ll" "${base}_dse.ll" || true
  echo
}

# run all tests in test/ with names starting dse_*.c
for src in test/dse_*.c; do
  if [[ -f "$src" ]]; then
    run_one "$src"
  fi
done

