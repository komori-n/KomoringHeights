#ifndef PATH_KEYS_HPP_
#define PATH_KEYS_HPP_

#include "typedefs.hpp"

namespace komori {
/// ハッシュを初期化する
void PathKeyInit();

/// 現在の path_key と手 move から1手後の path_key を計算する。値は深さ depth 依存。
Key PathKeyAfter(Key path_key, Move move, Depth depth);

inline Key PathKeyBefore(Key path_key, Move move, Depth depth) {
  // xor でハッシュを計算しているので、1手進めるのと同じ関数で戻せる
  return PathKeyAfter(path_key, move, depth);
}

}  // namespace komori

#endif  // PATH_KEYS_HPP_