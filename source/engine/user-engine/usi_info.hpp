/**
 * @file usi_info.hpp
 */
#ifndef KOMORI_USI_HPP_
#define KOMORI_USI_HPP_

#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "ranges.hpp"
#include "typedefs.hpp"

namespace komori {
/// `UsiInfo::Set()` で設定可能なUSIオプション一覧
enum class UsiInfoKey {
  kSelDepth,  ///< 選択的探索深さ
  kTime,      ///< 探索時間（ms）
  kNodes,     ///< 探索局面数
  kNps,       ///< 探索速度
  kHashfull,  ///< 置換表の使用率（千分率）
  kCurrMove,  ///< 現在の最善手
  kScore,     ///< 現在の評価値。PVがセットされているときは効果がないので注意。

  // depth, multipv, pv は `UsiInfo::Set()` ではなく  `UsiInfo::PushPVFront()` を用いて設定する。
};

/**
 * @brief USIプロトコルに従い探索情報（info）を整形するクラス
 *
 * USIプロトコルでは、エンジンの探索情報を出力するコマンドとしてinfoを用いる。infoには出力フォーマットに関して
 * 細かいルールが多くある。このクラスでは、そのような出力フォーマットの整形および関数間での受け渡しをするための
 * 機能を提供する。
 */
class UsiInfo {
 public:
  /**
   * @brief `UsiInfo` を `os` に出力する。
   * @param os        出力ストリーム
   * @param usi_info  `UsiInfo`
   * @return 出力ストリーム
   *
   * ### `usi_info.multi_pv_` が空のとき
   *
   * ```
   * info ...（中略） string
   * ```
   *
   * 戻り値の `os` に続けて文字列を渡すことで、文字列を追加することができる。
   *
   * ### `usi_info.multi_pv_` のサイズが 1 のとき
   *
   * ```
   * info ...（中略） score <score> depth <depth> (seldepth <seldepth>) pv <pv設定値>
   * ```
   *
   * 戻り値の `os` に続けて文字列を渡すことはできない。
   *
   * ### `usi_info.multi_pv_` のサイズが 2 以上のとき
   *
   * ```
   * info ...（中略） score <score> depth <depth> (seldepth <seldepth>) multipv 1 pv <pv設定値>
   * info ...（中略） score <score> depth <depth> (seldepth <seldepth>) multipv 2 pv <pv設定値>
   * ...
   * ```
   *
   * 戻り値の `os` に続けて文字列を渡すことはできない。
   */
  friend std::ostream& operator<<(std::ostream& os, const UsiInfo& usi_info) {
    constexpr const char* kKeyNames[] = {"seldepth", "time", "nodes", "nps", "hashfull", "currmove", "score"};

    std::ostringstream oss;
    oss << "info";
    const auto& options = usi_info.options_;
    for (const auto& [key, value] : options) {
      // - seldepth は depth の直後に渡さなければならない
      //   MultiPV のとき、PV ごとに depth の値が変わるのでここで seldepth を設定することはできない
      // - MultiPV のときは `multi_pv_` に設定された score を出力したい。よってここでは score の出力を後回しにする。
      if (key == UsiInfoKey::kSelDepth || key == UsiInfoKey::kScore) {
        continue;
      }

      oss << " " << kKeyNames[static_cast<int>(key)] << " " << value;
    }

    if (usi_info.multi_pv_.empty()) {
      if (const auto it = options.find(UsiInfoKey::kSelDepth); it != options.end()) {
        oss << " depth 0 seldepth " << it->second;
      }
      if (const auto it = options.find(UsiInfoKey::kScore); it != options.end()) {
        oss << " score " << it->second;
      }
      oss << " string ";
      os << oss.str();
    } else {
      const auto str = oss.str();
      for (const auto [index, pv_info] : WithIndex(usi_info.multi_pv_)) {
        const auto& [depth, score, pv] = pv_info;

        os << str << " score " << score << " depth " << depth;
        if (const auto it = options.find(UsiInfoKey::kSelDepth); it != options.end()) {
          os << " seldepth " << it->second;
        }

        if (usi_info.multi_pv_.size() > 1) {
          os << " multipv " << index + 1;
        }
        os << " pv " << pv;

        if (index != usi_info.multi_pv_.size() - 1) {
          os << std::endl;
        }
      }
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
  void Set(UsiInfoKey key, std::string_view val) { options_[key] = val; }

  /// `key` に `std::to_string(val)` を設定する。
  template <typename T, Constraints<std::enable_if_t<std::is_arithmetic_v<T>>> = nullptr>
  void Set(UsiInfoKey key, const T& val) {
    Set(key, std::to_string(val));
  }

  /**
   * @brief PV列の先頭（一番いい手）へ手を追加する。
   * @param depth 探索深さ
   * @param score 探索評価値
   * @param pv    PV
   */
  void PushPVFront(Depth depth, std::string_view score, std::string_view pv) {
    multi_pv_.emplace_front(PVInfo{depth, std::string{score}, std::string{pv}});
  }

 private:
  /// MultiPV（PV）で出力する情報たち
  struct PVInfo {
    Depth depth;        ///< 現在の探索深さ
    std::string score;  ///< 探索評価値
    std::string pv;     ///< Principal Variation
  };

  /// 設定済のオプションの中に `key` が含まれているかどうか判定する。
  bool Has(UsiInfoKey key) const { return options_.find(key) != options_.end(); }

  /// オプションと設定値のペア。
  std::unordered_map<UsiInfoKey, std::string> options_;
  /// 現在のPVたち（良い順）
  std::deque<PVInfo> multi_pv_;
};
}  // namespace komori

#endif  // KOMORI_USI_HPP_
