#ifndef KOMORI_EXPANSION_STACK_HPP_
#define KOMORI_EXPANSION_STACK_HPP_

#include <stack>
#include "local_expansion.hpp"

namespace komori {
/**
 * @brief `LocalExpansion` を一元管理するクラス。合流検出に用いる。
 *
 * 基本的には `std::stack<LocalExpansion>` のように振る舞う。すなわち、`Emplace()` により新たな `LocalExpansion` を
 * 構築し、`Pop()` により構築したインスタンスのうち最も新しいものを消す。最新のインスタンスは `Current()` で取得できる。
 *
 * `Emplace()` 時に合流検出の判定を行い、必要があれば親に遡って二重カウントの解消を試みる。
 *
 * @note 二重カウントの検出は未実装。
 */
class ExpansionStack {
 public:
  /// Default constructor(default)
  ExpansionStack() = default;
  /// Copy constructor(delete)
  ExpansionStack(const ExpansionStack&) = delete;
  /// Move constructor(delete)
  ExpansionStack(ExpansionStack&&) = delete;
  /// Copy assign operator(delete)
  ExpansionStack& operator=(const ExpansionStack&) = delete;
  /// Move assign operator(delete)
  ExpansionStack& operator=(ExpansionStack&&) = delete;
  /// Destructor(default)
  ~ExpansionStack() = default;

  /**
   * @brief スタックの先頭に `LocalExpansion` オブジェクトを構築する。
   * @tparam Args `LocalExpansion` のコンストラクタの引数。詳細は `LocalExpansion` の定義を参照。
   * @param args `LocalExpansion` のコンストラクタの引数。
   * @return 構築した `LocalExpansion` オブジェクト
   *
   * 構築した `LocalExpansion` において局面の合流を検出した場合、二重カウントの回避を試みる。
   */
  template <typename... Args>
  LocalExpansion& Emplace(Args&&... args) {
    auto& expansion = list_.emplace(std::forward<Args>(args)...);
    return expansion;
  }

  /**
   * @brief スタック先頭の `LocalExpansion` オブジェクトを開放する。
   */
  void Pop() noexcept { list_.pop(); }

  /// スタック先頭要素を返す。
  LocalExpansion& Current() { return list_.top(); }
  /// スタック先頭要素を返す。
  const LocalExpansion& Current() const { return list_.top(); }

 private:
  /**
   * @brief 格納データの本体。
   *
   * @note `std::vector` のほうが若干高速に動作すると思われるが、
   *       `LocalExpansion` のような move 不可オブジェクトには用いることができない。
   *       現時点では探索速度にそれほど影響ないと考えるので、実装は後回しにする。
   */
  std::stack<LocalExpansion> list_;
};
}  // namespace komori

#endif  // KOMORI_EXPANSION_STACK_HPP_
