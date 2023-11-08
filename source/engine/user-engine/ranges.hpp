/**
 * @file ranges.hpp
 */
#ifndef KOMORI_RANGES_HPP_
#define KOMORI_RANGES_HPP_

#include <cstddef>
#include <iterator>
#include <type_traits>

#include "type_traits.hpp"

namespace komori {
namespace detail {
/**
 * @brief Range が begin と end メンバ関数を持っているかどうか判定するダミー関数（never defined）
 * @tparam Range 範囲
 */
template <typename Range>
std::false_type HasBeginAndEndMembersCheck(long);  // NOLINT(runtime/int)

/**
 * @brief Range が begin と end メンバ関数を持っているかどうか判定するダミー関数（never defined）
 * @tparam Range 範囲
 */
template <typename Range,
          Constraints<decltype(std::declval<Range>().begin()), decltype(std::declval<Range>().end())> = nullptr>
std::true_type HasBeginAndEndMembersCheck(int);

/**
 * @brief Range が begin と end メンバ関数を持っているかどうか判定するメタ関数
 * @tparam Range 範囲
 */
template <typename Range>
struct HasBeginAndEndMembers : decltype(HasBeginAndEndMembersCheck<Range>(0)) {};

/**
 * @brief range-based for の要件に従って begin をコールする関数（配列版）
 * @param range 配列
 */
template <typename Range, Constraints<std::enable_if_t<std::is_array_v<std::remove_reference_t<Range>>>> = nullptr>
constexpr inline auto call_begin(Range&& range) noexcept(noexcept(std::begin(std::forward<Range>(range)))) {
  return std::begin(std::forward<Range>(range));
}

/**
 * @brief range-based for の要件に従って begin をコールする関数（メンバ関数版）
 * @param range begin と end をメンバ関数に持つクラス
 */
template <typename Range,
          Constraints<std::enable_if_t<!std::is_array_v<std::remove_reference_t<Range>> &&
                                       HasBeginAndEndMembers<Range>::value>> = nullptr>
constexpr inline auto call_begin(Range&& range) noexcept(noexcept(std::forward<Range>(range).begin())) {
  return std::forward<Range>(range).begin();
}

/**
 * @brief range-based for の要件に従って begin をコールする関数（フリー関数版）
 * @param range 配列でなく、かつ begin と end をメンバ関数に持たないクラス
 */
template <typename Range,
          Constraints<std::enable_if_t<!std::is_array_v<std::remove_reference_t<Range>> &&
                                       !HasBeginAndEndMembers<Range>::value>> = nullptr>
constexpr inline auto call_begin(Range&& range) noexcept(noexcept(begin(std::forward<Range>(range)))) {
  return begin(std::forward<Range>(range));
}

/**
 * @brief range-based for の要件に従って end をコールする関数（配列版）
 * @param range 配列
 */
template <typename Range, Constraints<std::enable_if_t<std::is_array_v<std::remove_reference_t<Range>>>> = nullptr>
constexpr inline auto call_end(Range&& range) noexcept(noexcept(std::end(std::forward<Range>(range)))) {
  return std::end(std::forward<Range>(range));
}

/**
 * @brief range-based for の要件に従って end をコールする関数（メンバ関数版）
 * @param range begin と end をメンバ関数に持つクラス
 */
template <typename Range,
          Constraints<std::enable_if_t<!std::is_array_v<std::remove_reference_t<Range>> &&
                                       HasBeginAndEndMembers<Range>::value>> = nullptr>
constexpr inline auto call_end(Range&& range) noexcept(noexcept(std::forward<Range>(range).end())) {
  return std::forward<Range>(range).end();
}

/**
 * @brief range-based for の要件に従って end をコールする関数（フリー関数版）
 * @param range 配列でなく、かつ begin と end をメンバ関数に持たないクラス
 */
template <typename Range,
          Constraints<std::enable_if_t<!std::is_array_v<std::remove_reference_t<Range>> &&
                                       !HasBeginAndEndMembers<Range>::value>> = nullptr>
constexpr inline auto call_end(Range&& range) noexcept(noexcept(end(std::forward<Range>(range)))) {
  return end(std::forward<Range>(range));
}

/**
 * @brief `WithIndex` の実装本体
 * @tparam IndexType 添字の型。デフォルトは `std::size_t`。
 * @tparam Range range-based for でイテレートするオブジェクトの型
 */
template <typename IndexType, typename Range>
class WithIndexImpl {
  static_assert(std::is_integral_v<IndexType>, "IndexType must be an integer type");

  /// `WithIndexIterator` を構築する時に用いるタグ。これを private に置くことでこのクラス外からの構築を防ぐことが目的。
  struct PrivateConstructionTag {};

 public:
  /**
   * @brief 添字つきのイテレータ
   * @tparam Iterator もとのイテレータ
   */
  template <typename Iterator>
  class WithIndexIterator {
   public:
    /**
     * @brief 添字とイテレータを受け取るコンストラクタ
     * @param index    開始添字
     * @param iterator もとのイテレータ
     */
    constexpr WithIndexIterator(PrivateConstructionTag,
                                IndexType index,
                                Iterator&& iterator) noexcept(std::is_nothrow_move_constructible_v<Iterator>)
        : index_{index}, iterator_{std::move(iterator)} {}

    /// イテレータから添え字と値を読み取る
    constexpr std::pair<IndexType, decltype(*std::declval<Iterator&>())> operator*() noexcept(
        noexcept(*std::declval<Iterator>())) {
      return {index_, std::forward<decltype(*iterator_)>(*iterator_)};
    }

    /// イテレータを進める
    constexpr WithIndexIterator& operator++() noexcept(noexcept(++std::declval<Iterator&>())) {
      ++index_;
      ++iterator_;
      return *this;
    }

    /**
     * @brief WithIndexIterator と end() との比較器
     * @tparam EndIterator decltype(end)
     * @param lhs 添字つきイテレータ
     * @param rhs end()
     * @return lhs.iterator != rhs を返す
     */
    template <typename EndIterator,
              Constraints<decltype(std::declval<const Iterator>() != std::declval<const EndIterator&>())> = nullptr>
    friend constexpr bool operator!=(const WithIndexIterator& lhs,
                                     const EndIterator& rhs) noexcept(noexcept(lhs.iterator_ != rhs)) {
      return lhs.iterator_ != rhs;
    }

   private:
    IndexType index_;    ///< 現在の添字
    Iterator iterator_;  ///< イテレータ
  };

  /// 範囲 `range` を受け取るコンストラクタ
  constexpr explicit WithIndexImpl(Range range) noexcept(
      std::is_nothrow_constructible_v<Range, decltype(std::forward<Range>(range))>)
      : range_{std::forward<Range>(range)} {}
  /// Default constructor(delete)
  WithIndexImpl() = delete;

  /// 添字付き範囲の先頭
  constexpr auto begin() const noexcept(noexcept(call_begin(std::declval<Range&>()))) {
    using ConstRangeBeginIterator = decltype(call_begin(range_));
    return WithIndexIterator<ConstRangeBeginIterator>{PrivateConstructionTag{}, IndexType{0}, call_begin(range_)};
  }

  /// 添字付き範囲の末尾（const）
  constexpr auto end() const noexcept(noexcept(call_end(std::declval<Range&>()))) { return call_end(range_); }

 private:
  Range range_;  ///< もとの range
};
}  // namespace detail

/**
 * @brief 添字つきのrange-based for 文用オブジェクトを生成する。
 * @tparam IndexType 添字の型。デフォルトは `std::size_t`。
 * @tparam Range lvalue reference OR nothrow move constructible
 * @param range range-based for でイテレートするオブジェクト
 * @return 添字つきのrange-based for 文用オブジェクト
 *
 * Pythonにおける enumerate と同様の役割。iterable なオブジェクトを受け取り、添字と要素のペアを受け取るような
 * オブジェクトを返す。テンプレート引数の順番が `IndexType` の方が手前なのは、インデックス型だけを手軽に
 * 変更できるようにするため。
 *
 * ```cpp
 * std::vector<int> vec{3, 3, 4};
 * for (auto&& [i, x] = WithIndex<std::uint32_t>(vec)) {
 *   ...
 * }
 * ```
 */
template <typename IndexType = std::size_t, typename Range = std::nullptr_t>
constexpr inline detail::WithIndexImpl<IndexType, Range> WithIndex(Range&& range) noexcept(
    noexcept(detail::WithIndexImpl<IndexType, Range>{std::forward<Range>(range)})) {
  static_assert(std::is_lvalue_reference_v<Range> || std::is_nothrow_move_constructible_v<Range>);
  return detail::WithIndexImpl<IndexType, Range>{std::forward<Range>(range)};
}

/**
 * @brief イテレータの範囲を表す型。
 * @tparam Iterator イテレータ
 *
 * `Range(begin, end)` の形式で [begin, end) の範囲を表現する。range-based for でイテレートすることもできる。
 */
template <typename Iterator>
class Range {
 public:
  /// Default constructor(delete)
  Range() = delete;
  /// コンストラクタ
  constexpr explicit Range(Iterator begin_itr, Iterator end_itr) noexcept
      : begin_itr_{std::move(begin_itr)}, end_itr_{std::move(end_itr)} {}
  /// 範囲の先頭
  constexpr Iterator begin() const noexcept { return begin_itr_; }
  /// 範囲の末尾
  constexpr Iterator end() const noexcept { return end_itr_; }

 private:
  Iterator begin_itr_;  ///< 範囲の先頭
  Iterator end_itr_;    ///< 範囲の末尾
};

/**
 * @brief イテレータのペアで range-based for をするためのアダプタ
 * @tparam Iterator イテレータ
 *
 * `std::multimap::equal_range()` のように、(begin, end) の形式のイテレータを所持しているとき、range-based for で
 * 要素を取り出すためのアダプタ。
 *
 * ```cpp
 * std::unordered_multimap<std::int32_t, std::int32_t> map{ ... };
 * for (const auto& [key, value] : AsRange(map.equal_range(10))) {
 *   ...
 * }
 * ```
 */
template <typename Iterator>
auto AsRange(const std::pair<Iterator, Iterator>& p) noexcept {
  return Range(p.first, p.second);
}

namespace detail {
/**
 * @brief `Skep` の実装本体
 * @tparam Range Iterable
 */
template <typename Range>
class SkipImpl {
 public:
  /**
   * @brief `SkipImpl` インスタンスを生成する
   */
  constexpr SkipImpl(Range range, std::size_t skip) noexcept(
      std::is_nothrow_constructible_v<Range, decltype(std::forward<Range>(range))>)
      : range_{std::forward<Range>(range)}, skip_{skip} {}

  /// 範囲の先頭
  constexpr auto begin() const
      noexcept(noexcept(call_begin(std::declval<Range&>()) != call_end(std::declval<Range&>()))) {
    auto itr = call_begin(range_);
    for (std::size_t i = 0; i < skip_ && itr != end(); ++i, ++itr) {
    }

    return itr;
  }

  /// 範囲の末尾
  constexpr auto end() const noexcept(noexcept(call_end(std::declval<Range&>()))) { return call_end(range_); }

 private:
  Range range_;       ///< もとの range
  std::size_t skip_;  ///< スキップする要素数
};
}  // namespace detail

/**
 * @brief Iterable の先頭 `kSkip` 要素をスキップする
 * @tparam Range iterableの型
 * @param range iterable
 * @param skip スキップする要素数
 * @return range-based for で先頭 `kSize` 要素を飛ばした要素が取れるような iterable
 */
template <typename Range>
constexpr inline auto Skip(Range&& range, std::size_t skip) noexcept(noexcept(detail::SkipImpl<Range>{
    std::forward<Range>(range), skip})) {
  return detail::SkipImpl<Range>{std::forward<Range>(range), skip};
}

namespace detail {
/**
 * @brief `Take` の実装本体
 * @tparam Range Iterable
 */
template <typename Range>
class TakeImpl {
 public:
  /**
   * @brief `TakeImpl` インスタンスを生成する
   */
  constexpr TakeImpl(Range range, std::size_t take) noexcept(
      std::is_nothrow_constructible_v<Range, decltype(std::forward<Range>(range))>)
      : range_{std::forward<Range>(range)}, take_{take} {}

  /// 範囲の先頭
  constexpr auto begin() const noexcept(noexcept(call_begin(std::declval<Range&>()))) { return call_begin(range_); }

  /// 範囲の末尾
  constexpr auto end() const noexcept(noexcept(call_begin(std::declval<Range>()), call_end(std::declval<Range&>()))) {
    if (static_cast<std::size_t>(std::distance(call_begin(range_), call_end(range_))) > take_) {
      return std::next(call_begin(range_), take_);
    } else {
      return call_end(range_);
    }
  }

 private:
  Range range_;       ///< もとの range
  std::size_t take_;  ///< 取ってくる要素数
};
}  // namespace detail

/**
 * @brief Iterable の先頭 `kTake` 要素だけを取ってくる
 * @tparam Range iterableの型
 * @param range iterable
 * @param take 取ってくる要素数
 * @return range-based for で先頭 `kTake` 要素を飛ばした要素が取れるような iterable
 */
template <typename Range>
constexpr inline auto Take(Range&& range, std::size_t take) noexcept(noexcept(detail::TakeImpl<Range>{
    std::forward<Range>(range), take})) {
  return detail::TakeImpl<Range>{std::forward<Range>(range), take};
}

namespace detail {
/**
 * @brief `Zip` の実装本体
 * @tparam Range1 Iterable
 * @tparam Range2 Iterable
 */
template <typename Range1, typename Range2>
class ZipImpl {
 public:
  /**
   * @brief zipイテレータ
   * @tparam Iterator1 Range1 に対するイテレータ
   * @tparam Iterator2 Range2 に対するイテレータ
   */
  template <typename Iterator1, typename Iterator2>
  class Iterator {
   public:
    /**
     * @brief コンストラクタ
     */
    constexpr Iterator(Iterator1 itr1, Iterator2 itr2) noexcept(std::is_nothrow_copy_assignable_v<Iterator1> &&
                                                                std::is_nothrow_copy_assignable_v<Iterator2>)
        : itr1_{itr1}, itr2_{itr2} {}

    /// イテレータをペアにして値を読み取る
    constexpr auto operator*() noexcept(noexcept(*std::declval<Iterator1>(), *std::declval<Iterator2>())) {
      return std::make_pair(*itr1_, *itr2_);
    }

    /// イテレータを進める
    constexpr Iterator& operator++() noexcept(noexcept(std::declval<Iterator1>()++, std::declval<Iterator2>()++)) {
      itr1_++;
      itr2_++;

      return *this;
    }

    /// イテレータを比較する
    template <typename LI1, typename LI2, typename RI1, typename RI2>
    friend constexpr bool operator!=(const Iterator<LI1, LI2>& lhs,
                                     const Iterator<RI1, RI2>& rhs) noexcept(noexcept(lhs.itr1_ == rhs.itr1_ ||
                                                                                      lhs.itr2_ == rhs.itr2_)) {
      return lhs.itr1_ != rhs.itr1_ && lhs.itr2_ != rhs.itr2_;
    }

   private:
    Iterator1 itr1_;  ///< Range1 に対するイテレータ
    Iterator2 itr2_;  ///< Range2 に対するイテレータ
  };

  /// コンストラクタ
  constexpr ZipImpl(Range1 range1, Range2 range2) noexcept(
      std::is_nothrow_constructible_v<Range1, decltype(std::forward<Range1>(range1))> &&
      std::is_nothrow_constructible_v<Range2, decltype(std::forward<Range2>(range2))>)
      : range1_{std::forward<Range1>(range1)}, range2_{std::forward<Range2>(range2)} {}

  /// 範囲の先頭
  constexpr auto begin() const
      noexcept(noexcept(call_begin(std::declval<Range1>()), call_begin(std::declval<Range2>()))) {
    return Iterator<decltype(call_begin(range1_)), decltype(call_begin(range2_))>(call_begin(range1_),
                                                                                  call_begin(range2_));
  }

  /// 範囲の末尾
  constexpr auto end() const noexcept(noexcept(call_end(std::declval<Range1>()), call_end(std::declval<Range2>()))) {
    return Iterator<decltype(call_end(range1_)), decltype(call_end(range2_))>(call_end(range1_), call_end(range2_));
  }

 private:
  Range1 range1_;  ///< Range1
  Range2 range2_;  ///< Range2
};
}  // namespace detail

/**
 * @brief 2つのrangeをpairでまとめたようなrangeを返す。
 * @param range1 range1
 * @param range2 range2
 * @return 2つのrangeをpairでまとめたようにiterateできるrangeを返す。
 */
template <typename Range1, typename Range2>
constexpr inline auto Zip(Range1&& range1, Range2&& range2) noexcept(
    noexcept(detail::ZipImpl<Range1, Range2>(std::forward<Range1>(range1), std::forward<Range2>(range2)))) {
  return detail::ZipImpl<Range1, Range2>(std::forward<Range1>(range1), std::forward<Range2>(range2));
}
}  // namespace komori

#endif  // KOMORI_RANGES_HPP_
