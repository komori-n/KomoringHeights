#ifndef KOMORI_USI_HPP_
#define KOMORI_USI_HPP_

#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "typedefs.hpp"

namespace komori {
/// infoコマンドで使えるUSIオプション一覧
enum class UsiInfoKey {
  kDepth,     ///< 探索深さ
  kSelDepth,  ///< 選択的探索深さ
  kTime,      ///< 探索時間（ms）
  kNodes,     ///< 探索局面数
  kNps,       ///< 探索速度
  kHashfull,  ///< 置換表の使用率（千分率）
  kCurrMove,  ///< 現在の最善手
  kPv,        ///< PV（最善応手列）
  kString,    ///< 文字列
};

/**
 * @brief USIプロトコルに従い探索情報（info）を整形するクラス
 *
 * USIプロトコルでは、エンジンの探索情報を出力するコマンドとしてinfoを用いる。infoには出力フォーマットに関して
 * 細かいルールが多くある。このクラスでは、そのような出力フォーマットの整形および関数間での受け渡しをするための
 * 機能を提供する。
 *
 * USIプロトコルの制約により、`UsiInfoKey::kPv` と `UsiInfoKey::kString` を同時に設定することはできない。
 * もし両方設定された場合、`UsiInfoKey::kString` の設定値を優先する。
 */
class UsiInfo {
 public:
  /**
   * @brief `UsiInfo` を `os` に出力する。
   * @param os        出力ストリーム
   * @param usi_info  `UsiInfo`
   * @return 出力ストリーム
   *
   * ### `UsiInfoKey::kString` が設定されているとき
   *
   * ```
   * info ...（中略） string <string設定値>
   * ```
   *
   * 戻り値の `os` に続けて文字列を渡すことで、文字列を追加することができる。
   *
   * ### `UsiInfoKey::kPv` が設定されているとき
   *
   * ```
   * info ...（中略） pv <pv設定値>
   * ```
   *
   * `UsiInfoKey::kString` が設定されている場合と異なり、戻り値の `os` に続けて文字列を渡すことはできない。
   *
   * ### Otherwise
   *
   * ```
   * info ...（中略） string
   * ```
   *
   * 上記の文字列が `os` に出力されるので、任意の文字列を続けて渡すことができる（空でも可）。
   */
  friend std::ostream& operator<<(std::ostream& os, const UsiInfo& usi_info) {
    constexpr const char* kKeyNames[] = {
        "depth", "seldepth", "time", "nodes", "nps", "hashfull", "currmove", "pv", "string",
    };

    os << "info";
    for (const auto& [key, value] : usi_info.options_) {
      if (key == UsiInfoKey::kPv || key == UsiInfoKey::kString || key == UsiInfoKey::kDepth) {
        continue;
      }

      if (key == UsiInfoKey::kSelDepth) {
        if (auto itr = usi_info.options_.find(UsiInfoKey::kDepth); itr != usi_info.options_.end()) {
          os << " depth " << itr->second;
        }
      }
      os << " " << kKeyNames[static_cast<int>(key)] << " " << value;
    }

    if (auto itr = usi_info.options_.find(UsiInfoKey::kString); itr != usi_info.options_.end()) {
      os << " string " << itr->second << " ";
    } else if (auto itr = usi_info.options_.find(UsiInfoKey::kPv); itr != usi_info.options_.end()) {
      os << " pv " << itr->second;
    } else {
      os << " string ";
    }

    return os;
  }

  /// Default constructor(default)
  UsiInfo() = default;
  /// Copy constructor(default)
  UsiInfo(const UsiInfo&) = default;
  /// Move constructor(default)
  UsiInfo(UsiInfo&&) noexcept = default;
  /// Copy assign operator(default)
  UsiInfo& operator=(const UsiInfo&) = default;
  /// Move assign operator(default)
  UsiInfo& operator=(UsiInfo&&) noexcept = default;
  /// Destructor(default)
  ~UsiInfo() = default;

  /// `key` に `val` を設定する。
  void Set(UsiInfoKey key, const std::string& val) {
    options_[key] = val;
    if (key == UsiInfoKey::kSelDepth && !Has(UsiInfoKey::kDepth)) {
      options_[UsiInfoKey::kDepth] = "0";
    }
  }

  /// `key` に `std::to_string(val)` を設定する。
  template <typename T, Constraints<std::enable_if_t<std::is_arithmetic_v<T>>> = nullptr>
  void Set(UsiInfoKey key, const T& val) {
    Set(key, std::to_string(val));
  }

 private:
  /// 設定済のオプションの中に `key` が含まれているかどうか判定する。
  bool Has(UsiInfoKey key) const { return options_.find(key) != options_.end(); }

  /// オプションと設定値のペア。
  std::unordered_map<UsiInfoKey, std::string> options_;
};
}  // namespace komori
#endif  // KOMORI_USI_HPP_
