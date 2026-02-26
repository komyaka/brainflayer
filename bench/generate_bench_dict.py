#!/usr/bin/env python3
from pathlib import Path


def main() -> None:
  path = Path("bench/bench_dict.txt")
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text("".join(f"bench-{i:04d}\n" for i in range(2048)))


if __name__ == "__main__":
  main()
