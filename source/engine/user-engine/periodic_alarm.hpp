/**
 * @file periodic_alarm.hpp
 */
#ifndef KOMORI_PERIODIC_ALARM_HPP_
#define KOMORI_PERIODIC_ALARM_HPP_

#include <chrono>
#include <cstdint>

namespace komori {
/**
 * @brief 一定間隔で通知するアラーム
 *
 * `Tick()` を短い周期で呼び出すと、一定間隔 `internal_ms_` ごとに `true` が帰ってくるタイマーを提供する。
 * 毎回現在時刻を確認すると処理負荷が高まるので、タイマー確認タイミンを `kCheckSkip` 回に1回に制限している。
 * `Tick()` の呼び出し頻度が不足していると意図した時刻に `true` が帰ってこない可能性があるので注意すること。
 */
class PeriodicAlarm {
 public:
  /// Default constructor(default)
  PeriodicAlarm() = default;
  /// Copy constructor(default)
  PeriodicAlarm(const PeriodicAlarm&) = default;
  /// Move constructor(default)
  PeriodicAlarm(PeriodicAlarm&&) noexcept = default;
  /// Copy assign operator(default)
  PeriodicAlarm& operator=(const PeriodicAlarm&) = default;
  /// Move assign operator(default)
  PeriodicAlarm& operator=(PeriodicAlarm&&) noexcept = default;
  /// Destructor(default)
  ~PeriodicAlarm() = default;

  /**
   * @brief 周期タイマーを開始する
   * @param interval_ms タイマーの周期[ms]
   */
  void Start(std::uint64_t interval_ms) {
    check_skip_remain_ = kCheckSkip;
    next_notify_tp_ = Now() + interval_ms;
    interval_ms_ = interval_ms;
  }

  /**
   * @brief 起動中のタイマーを停止する
   */
  void Stop() {
    check_skip_remain_ = 0;
    next_notify_tp_ = 0;
    interval_ms_ = 0;
  }

  /**
   * @brief 現在時刻を確認し、前回の `true` を返してから `interval_ms_` が経過していれば `true` を返す
   * @return 前回の `true` 返却から `interval_ms_` が経過していれば `true`
   */
  bool Tick() {
    --check_skip_remain_;
    if (check_skip_remain_ > 0) {
      return false;
    }

    check_skip_remain_ = kCheckSkip;
    const auto now = Now();
    if (now < next_notify_tp_) {
      return false;
    }

    next_notify_tp_ = now + interval_ms_;
    return true;
  }

 private:
  /// `Tick()` でタイマーを実際に確認する頻度。
  static constexpr std::uint32_t kCheckSkip = 2048;

  /// Time Point をミリ秒で取得する
  std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  std::uint32_t check_skip_remain_{};  ///< あと何回 `Tick()` が呼ばれたら現在時刻を確認するか
  std::uint64_t interval_ms_{};        ///< `Tick()` が `true` を返す周期
  std::uint64_t next_notify_tp_{};     ///< 次に `Tick()` が `true` を返す時刻
};
}  // namespace komori

#endif  // KOMORI_PERIODIC_ALARM_HPP_
