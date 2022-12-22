![Make CI (MSYS2 for Windows)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(MSYS2%20for%20Windows)/badge.svg?event=push)
![Make CI (MinGW for Windows)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(MinGW%20for%20Windows)/badge.svg?event=push)
![Make CI (for Ubuntu Linux)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(for%20Ubuntu%20Linux)/badge.svg?event=push)
![Make CI (for Mac)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(for%20Mac)/badge.svg?event=push)
![NDK CI (for Android)](https://github.com/komori-n/KomoringHeights/workflows/NDK%20CI%20(for%20Android)/badge.svg?event=push)

# KomoringHeights

KomoringHeights is a simple and powerful mate engine for Shogi game based on [YaneuraOu](https://github.com/yaneurao/YaneuraOu/).
It implements some advanced algorithms such as df-pn+ algorithm, superior/inferior relationship, strict repetition detection, and double count problem detection.

Sources for KomoringHeights are stored in `source/engine/user-engine`.
The other files mostly came from YaneuraOu.

## How to use

Please download binaries from [Releases](https://github.com/komori-n/KomoringHeights/releases).
As KomoringHeights complies with the USI protocol (universal shogi interface protocol), GUI software such as [Shogidokoro](http://shogidokoro.starfree.jp/), [ShogiGUI](http://shogigui.siganus.com/), [Electron Shogi](https://sunfish-shogi.github.io/electron-shogi/), and [ShogiDroid](http://shogidroid.siganus.com/) is needed to run.

For available engine options, please see [EngineOptions](source/engine/suer-engine/docs/EngineOptions.txt).

## How to Build

To build a binary from the source, use the following commands:

```sh
git clone https://github.com/komori-n/KomoringHeights.git
cd KomoringHeights/source
make normal TARGET_CPU=AVX2 COMPILER=clang++
```

Specify appropriate `TARGET_CPU` according to your environment.
You can find the list of available options in `source/Makefile`.

A compiler for C++17 is required to build. We confirmed to build
by GCC 10+ and Clang 11+. Compilers older than them are not supported.

## References

See [References](source/engine/user-engine/docs/refs.md).

## Detailed Design Documents

- <https://komori-n.github.io/komoring-heights-docs/index.html> (in Japanese)

## Contributing

Please feel free to submit Issues or Pull Requests.
Before you create Pull Request, please confirm `pre-commit` passes checks.

## License

Licensed under GPLv3.

- `source/engine/user-engine/**`: komori-n
- other: [YaneuraOu](https://github.com/yaneurao/YaneuraOu/)
