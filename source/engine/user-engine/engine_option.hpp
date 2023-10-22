/**
 * @file engine_option.hpp
 */
#ifndef KOMORI_ENGINE_OPTION_HPP_
#define KOMORI_ENGINE_OPTION_HPP_

#include <limits>
#include <map>
#include <optional>
#include <string>

#include "../../usi.h"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 評価値の計算方法。詰将棋エンジンでは評価値を計算する決まった方法がないので選べるようにしておく。
 */
enum class ScoreCalculationMethod {
  kNone,     ///< 詰み／不詰が確定するまで評価値を表示しない
  kDn,       ///< dnをそのまま評価値として出す
  kMinusPn,  ///< -pnをそのまま評価値として出す
  kPonanza,  ///< ポナンザ定数を用いた勝率 <-> 評価値変換
};

/**
 * @brief 余詰探索の度合い。
 */
enum class PostSearchLevel {
  kNone,        ///< 余詰探索なし
  kUpperBound,  ///< Upper bound に一致する長さの手順を見つけるまで
  kMinLength,   ///< 最短手順を探す
};

namespace detail {
/**
 * @brief look up 時にキーが存在しない時はデフォルト値を返す ordered_map。
 *
 * @tparam K キー
 * @tparam V 値
 *
 * @note GUI側に渡すオプションに順序をつけたいので、ただの map ではなく ordered map として実装する。
 */
template <typename K, typename V>
class DefaultOrderedMap : private std::vector<std::pair<K, V>> {
  using Base = std::vector<std::pair<K, V>>;  ///< Base class

 public:
  /**
   * @brief コンストラクタ
   */
  explicit constexpr DefaultOrderedMap(K default_key, V default_value, std::initializer_list<std::pair<K, V>> list)
      : Base{std::move(list)}, default_key_{std::move(default_key)}, default_val_{std::move(default_value)} {}

  /**
   * @brief `key` に対応する値を取得する。`key` が存在しなければデフォルト値が返る。
   *
   * @param key キー
   * @return `key` に対応する値。`key` が存在しなければデフォルト値。
   */
  V Get(const K& key) const {
    for (const auto& [k, v] : *this) {
      if (k == key) {
        return v;
      }
    }

    return default_val_;
  }

  /// `map` の `key` 一覧。USIオプションの初期化時に必要
  std::vector<K> Keys() const {
    std::vector<K> keys;
    keys.reserve(this->size());
    for (const auto& [key, value] : *this) {
      keys.emplace_back(key);
    }

    return keys;
  }

  /// デフォルトキー。USIオプションの初期化時に必要
  const K& DefaultKey() const { return default_key_; }

 private:
  const K default_key_;  ///< デフォルトのキー
  const V default_val_;  ///< デフォルトの値
};

/// 評価値計算方法 `ScoreCalculationMethod` 用の Combo 定義。
inline const DefaultOrderedMap<std::string, ScoreCalculationMethod> score_caluclation_option{
    "Ponanza",
    ScoreCalculationMethod::kPonanza,
    {
        {"None", ScoreCalculationMethod::kNone},
        {"Dn", ScoreCalculationMethod::kDn},
        {"MinusPn", ScoreCalculationMethod::kMinusPn},
        {"Ponanza", ScoreCalculationMethod::kPonanza},
    },
};

/// 余詰探索方法 `PostSearchLevel` 用の Combo 定義。
inline const DefaultOrderedMap<std::string, PostSearchLevel> post_search_level{
    "MinLength",
    PostSearchLevel::kNone,
    {
        {"None", PostSearchLevel::kNone},
        {"UpperBound", PostSearchLevel::kUpperBound},
        {"MinLength", PostSearchLevel::kMinLength},
    },
};

/**
 * @brief オプション `o` から `name` の値を読み込む
 * @tparam OutType 出力値の型。`s64` や `std::string` など。デフォルト値は `s64`。
 * @param o    エンジンオプション
 * @param name 読み込みキー
 * @return `name` に対応する値
 *
 * もし `o[name]` が存在しないなら `OutType{}` を返す。
 */
template <typename OutType = s64>
inline OutType ReadOption(const USI::OptionsMap& o, const std::string& name) {
  if (auto itr = o.find(name); itr != o.end()) {
    return itr->second;
  }

  return OutType{};
}

/**
 * @brief `val` が0以下なら 2^64-1 を返す。
 * @param val 値
 * @return `val` が正ならその値、それ以外なら 2^64-1 を返す。
 */
inline constexpr std::uint64_t MakeInfIfNotPositive(std::int64_t val) {
  if (val > 0) {
    return static_cast<std::uint64_t>(val);
  }
  return std::numeric_limits<std::uint64_t>::max();
}
}  // namespace detail

/**
 * @brief エンジンオプションの事前読み込みおよび提供を行うクラス。
 *
 * エンジン起動時に `Init()` によりエンジン独自定義のオプションを設定する。ここでは、 `NodesLimit`などの
 * 詰めエンジン独自のオプションを使えるようにする。設定したオプションは `Reload()` により読み込むことができる。
 */
struct EngineOption {
  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  std::uint64_t hash_mb;  ///< ハッシュサイズ[MB]
  int threads;            ///< スレッド数

  std::uint64_t nodes_limit;         ///< 探索局面数制限。探索量に制限がないとき、2^64-1。
  std::uint64_t pv_interval;         ///< 探索進捗を表示する間隔[ms]。0 ならば全く出力しない。
  bool root_is_and_node_if_checked;  ///< 開始局面が王手されているとき、玉方手番として扱うフラグ。
  bool show_pv_after_mate;           ///< 詰みを見つけた後に PV を表示するかどうか。

  ScoreCalculationMethod score_method;  ///< スコアの計算法
  PostSearchLevel post_search_level;    ///< 余詰探索の度合い

  std::string tt_read_path;   ///< TTを読み込むファイル名
  std::string tt_write_path;  ///< TTを書き込むファイル名

  /// 探索結果を info string で出さない。ベンチマーク用のため `USI::OptionsMap` には登録しない
  bool disable_info_print{false};

#if defined(USE_DEEP_DFPN)
  Depth deep_dfpn_d;   ///< deep df-pn の D 値
  double deep_dfpn_e;  ///< deep df-pn の E 値
#endif
  // NOLINTEND(misc-non-private-member-variables-in-classes)

  /**
   * @brief 詰めエンジン独自のエンジンオプションを設定する
   * @param[out] o エンジンオプション
   */
  static void Init(USI::OptionsMap& o) {
    o["NodesLimit"] << USI::Option(0, 0, INT64_MAX);
    o["PvInterval"] << USI::Option(1000, 0, 1000000);

    o["RootIsAndNodeIfChecked"] << USI::Option(true);

#if defined(USE_DEEP_DFPN)
    o["DeepDfpnPerMile"] << USI::Option(5, 0, 10000);
    o["DeepDfpnMaxVal"] << USI::Option(1000000, 1, INT64_MAX);
#endif  // defined(USE_DEEP_DFPN)

    o["ScoreCalculation"] << USI::Option(detail::score_caluclation_option.Keys(),
                                         detail::score_caluclation_option.DefaultKey());
    o["PostSearchLevel"] << USI::Option(detail::post_search_level.Keys(), detail::post_search_level.DefaultKey());

#if defined(USE_TT_SAVE_AND_LOAD)
    o["TTReadPath"] << USI::Option("");
    o["TTWritePath"] << USI::Option("");
#endif  // defined(USE_TT_SAVE_AND_LOAD)

    o["ShowPVAfterMate"] << USI::Option(false);
  }

  /**
   * @brief エンジンオプションをメンバ変数に読み込む
   * @param[in] o エンジンオプション
   */
  void Reload(const USI::OptionsMap& o) {
    hash_mb = detail::ReadOption(o, "USI_Hash");
    threads = static_cast<int>(detail::ReadOption(o, "Threads"));

    nodes_limit = detail::MakeInfIfNotPositive(detail::ReadOption(o, "NodesLimit"));
    pv_interval = detail::MakeInfIfNotPositive(detail::ReadOption(o, "PvInterval"));
    root_is_and_node_if_checked = (detail::ReadOption(o, "RootIsAndNodeIfChecked") != 0);

#if defined(USE_DEEP_DFPN)
    if (auto val = detail::ReadOption(o, "DeepDfpnPerMile"); val > 0) {
      deep_dfpn_e = 0.001 * val + 1.0;
      const auto max = detail::ReadOption(o, "DeepDfpnMaxVal");
      deep_dfpn_d = static_cast<Depth>(std::log(static_cast<double>(max)) / std::log(e));
    } else {
      deep_dfpn_d = 0;
      deep_dfpn_e = 1.0;
    }
#endif  // defined(USE_DEEP_DFPN)

    score_method = detail::score_caluclation_option.Get(detail::ReadOption<std::string>(o, "ScoreCalculation"));
    post_search_level = detail::post_search_level.Get(detail::ReadOption<std::string>(o, "PostSearchLevel"));

#if defined(USE_TT_SAVE_AND_LOAD)
    tt_read_path = detail::ReadOption<std::string>(o, "TTReadPath");
    tt_write_path = detail::ReadOption<std::string>(o, "TTWritePath");
#endif  // defined(USE_TT_SAVE_AND_LOAD)

    show_pv_after_mate = (detail::ReadOption(o, "ShowPVAfterMate") != 0);
  }
};
}  // namespace komori

#endif  // KOMORI_ENGINE_OPTION_HPP_
