////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2019 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#ifndef IRESEARCH_TABLE_MATCHER_H
#define IRESEARCH_TABLE_MATCHER_H

#include <algorithm>

#include "fst/matcher.h"
#include "utils/misc.hpp"
#include "utils/integer.hpp"

NS_BEGIN(fst)

template<typename F, fst::MatchType TYPE = MATCH_INPUT>
std::vector<typename F::Arc::Label> getStartLabels(const F& fst) {
  static_assert(TYPE == MATCH_INPUT || TYPE == MATCH_OUTPUT,
                "unsupported match type");

  std::set<typename F::Arc::Label> labels;
  for (StateIterator<F> siter(fst); !siter.Done(); siter.Next()) {
    const auto state = siter.Value();
    for (ArcIterator<F> aiter(fst, state); !aiter.Done(); aiter.Next()) {
      const auto& arc = aiter.Value();
      labels.emplace(TYPE == MATCH_INPUT ? arc.ilabel : arc.olabel);
    }
  }

  return { labels.begin(), labels.end() };
}

template<typename F, typename F::Arc::Label Rho, size_t CacheSize = 256, fst::MatchType TYPE = MATCH_INPUT>
class TableMatcher : public MatcherBase<typename F::Arc> {
 public:
  using FST = F;
  using Arc = typename FST::Arc;
  using Label = typename Arc::Label;
  using StateId = typename Arc::StateId;
  using Weight = typename Arc::Weight;

  using MatcherBase<Arc>::Flags;
  using MatcherBase<Arc>::Properties;

  explicit TableMatcher(const FST& fst)
    : start_labels_(fst::getStartLabels<F, TYPE>(fst)),
      arc_(kNoLabel, kNoLabel, Weight::NoWeight(), kNoStateId),
      fst_(&fst) {
    static constexpr auto props = (TYPE == MATCH_INPUT ? kNoIEpsilons : kNoOEpsilons)
        | (TYPE == MATCH_INPUT ? kILabelSorted : kOLabelSorted)
        | (TYPE == MATCH_INPUT ? kIDeterministic : kODeterministic)
        | kAcceptor;
    assert(fst.Properties(props, true) == props);

    const size_t num_states = fst.NumStates();

    // initialize transition table
    transitions_.resize(num_states*start_labels_.size(), kNoStateId);
    for (StateIterator<FST> siter(fst); !siter.Done(); siter.Next()) {
      const auto state = siter.Value();

      ArcIterator<FST> aiter(fst, state);
      auto begin = start_labels_.begin();
      auto end = start_labels_.end();

      aiter.Seek(fst.NumArcs(state)-1);
      if (!aiter.Done()) {
        const auto& arc = aiter.Value();
        if (Rho == (TYPE == MATCH_INPUT ? arc.ilabel : arc.olabel)) {
          std::fill_n(transitions_.begin() + state*start_labels_.size(), start_labels_.size(), arc.nextstate);
        }
      }

      for (aiter.Seek(0); !aiter.Done() && begin != end;) {
        for (; !aiter.Done() && (TYPE == MATCH_INPUT ? aiter.Value().ilabel : aiter.Value().olabel) < *begin; aiter.Next()) { }

        if (aiter.Done()) {
          break;
        }

        for (; begin != end  && (TYPE == MATCH_INPUT ? aiter.Value().ilabel : aiter.Value().olabel) > *begin; ++begin) { }

        if (begin == end) {
          break;
        }

        auto& arc = aiter.Value();
        if ((TYPE == MATCH_INPUT ? arc.ilabel : arc.olabel) == *begin) {
          transitions_[state*start_labels_.size() + std::distance(start_labels_.begin(), begin)] = arc.nextstate;
          ++begin;
          aiter.Next();
        }
      }
    }

    // initialize lookup table for first CacheSize labels,
    // code below is the optimized version of:
    // for (size_t i = 0; i < CacheSize; ++i) {
    //   cached_label_offsets_[i] = find_label_offset(i);
    // }
    auto begin = start_labels_.begin();
    auto end = start_labels_.end();
    for (size_t i = 0, offset = 0;
         i < IRESEARCH_COUNTOF(cached_label_offsets_); ++i) {
      if (begin != end && *begin == i) {
        cached_label_offsets_[i] = offset;
        ++offset;
        ++begin;
      } else {
        cached_label_offsets_[i] = start_labels_.size();
      }
    }
  }

  virtual TableMatcher<FST, Rho>* Copy(bool) const override {
    return new TableMatcher<FST, Rho>(*this);
  }

  virtual MatchType Type(bool test) const override {
    if /*constexpr*/ (TYPE == MATCH_NONE) {
      return TYPE;
    }

    constexpr const auto true_prop = (TYPE == MATCH_INPUT)
      ? kILabelSorted
      : kOLabelSorted;

    constexpr const auto false_prop = (TYPE == MATCH_INPUT)
      ? kNotILabelSorted
      : kNotOLabelSorted;

    const auto props = fst_->Properties(true_prop | false_prop, test);

    if (props & true_prop) {
      return TYPE;
    } else if (props & false_prop) {
      return MATCH_NONE;
    } else {
      return MATCH_UNKNOWN;
    }
  }

  virtual void SetState(StateId s) noexcept final {
    assert(s*start_labels_.size() < transitions_.size());
    state_begin_ = transitions_.data() + s*start_labels_.size();
    state_ = state_begin_;
    state_end_ = state_begin_ + start_labels_.size();
  }

  virtual bool Find(Label label) noexcept final {
    auto label_offset = (label < IRESEARCH_COUNTOF(cached_label_offsets_)
                           ? cached_label_offsets_[size_t(label)]
                           : find_label_offset(label));

    if (label_offset == start_labels_.size()) {
      if (start_labels_.back() != Rho) {
        return false;
      }

      label_offset = start_labels_.size() - 1;
    }

    state_ = state_begin_ + label_offset;
    assert(state_ < state_end_);
    arc_.nextstate = *state_;
    return arc_.nextstate != kNoStateId;
  }

  virtual bool Done() const noexcept final {
    return state_ != state_end_;
  }

  virtual const Arc& Value() const noexcept final {
    return arc_;
  }

  virtual void Next() noexcept final {
    assert(state_ < state_end_);
    while (!Done()) {
      if (*++state_ != kNoLabel) {
        auto& label = (TYPE == MATCH_INPUT ? arc_.ilabel : arc_.olabel);
        label = start_labels_[size_t(std::distance(state_begin_, state_))];
        arc_.nextstate = *state_;
        break;
      }
    }
  }

  virtual Weight Final(StateId s) const final {
    return MatcherBase<Arc>::Final(s);
  }

  virtual ssize_t Priority(StateId s) final {
    return MatcherBase<Arc>::Priority(s);
  }

  virtual const FST &GetFst() const noexcept override {
    return *fst_;
  }

  virtual uint64 Properties(uint64 inprops) const noexcept override {
    return inprops;// | (error_ ? kError : 0);
  }

 private:
  size_t find_label_offset(Label label) const noexcept {
    const auto it = std::lower_bound(start_labels_.begin(), start_labels_.end(), label);
    if (it == start_labels_.end() || *it != label) {
      return start_labels_.size();
    }

    assert(it != start_labels_.end());
    assert(start_labels_.begin() <= it);
    return size_t(std::distance(start_labels_.begin(), it));
  }

  size_t cached_label_offsets_[CacheSize]{};
  std::vector<Label> start_labels_;
  std::vector<StateId> transitions_;
  Arc arc_;
  const FST* fst_;                   // FST for matching
  const Label* state_begin_{};       // Matcher state begin
  const Label* state_end_{};         // Matcher state end
  const Label* state_{};             // Matcher current state
}; // TableMatcher

NS_END // fst

#endif