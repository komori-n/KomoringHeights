#ifndef KOMORI_ENGINE_OPTION_HPP_
#define KOMORI_ENGINE_OPTION_HPP_

#include <limits>
#include <string>

#include "../../usi.h"
#include "typedefs.hpp"

namespace komori {
namespace detail {
/**
 * @brief オプション `o` から `name` の値を読み込む
 * @param o    エンジンオプション
 * @param name 読み込みキー
 * @return `name` に対応する値
 *
 * もし `o[name]` が存在しないなら 0 を返す。
 */
inline s64 ReadOption(const USI::OptionsMap& o, const std::string& name) {
  if (auto itr = o.find(name); itr != o.end()) {
    return itr->second;
  }

  return 0;
}

/**
 * @brief `val` が0以下なら 2^64-1 を返す。
 * @param val 値
 * @return `val` が正ならその値、それ以外なら 2^64-1 を返す。
 */
inline constexpr std::uint64_t MakeInfIfNotPositive(std::uint64_t val) {
  if (val > 0) {
    return val;
  }
  return std::numeric_limits<std::uint64_t>::max();
}
}  // namespace detail

/**
 * @brief エンジンオプションの事前読み込みおよび提供を行う
 *
 * エンジン起動時に `Init()` によりエンジン独自定義のオプションを設定する。ここでは、 `NodesLimit`などの
 * 詰めエンジン独自のオプションを使えるようにする。
 *
 * USIプロトコルのオプションは若干クセが強いため、値読み出し時に若干計算が必要になる。
 * このクラスでは、探索開始時に `Reload()` をコールすることでエンジンオプションをメンバ変数に読み込む。
 */
struct EngineOption {
  std::uint64_t hash_mb;  ///< ハッシュサイズ[MB]
  int threads;            ///< スレッド数

  std::uint64_t nodes_limit;         ///< 探索局面数制限。探索量に制限がないとき、2^64-1。
  std::uint64_t pv_interval;         ///< 探索進捗を表示する間隔[ms]。0 ならば全く出力しない。
  bool root_is_and_node_if_checked;  ///< 開始局面が王手されているとき、玉方手番として扱うフラグ。

#if defined(USE_DEEP_DFPN)
  Depth deep_dfpn_d;   ///< deep df-pn の D 値
  double deep_dfpn_e;  ///< deep df-pn の E 値
#endif

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
    root_is_and_node_if_checked = detail::ReadOption(o, "RootIsAndNodeIfChecked");

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
  }
};
}  // namespace komori

#endif  // KOMORI_ENGINE_OPTION_HPP_
