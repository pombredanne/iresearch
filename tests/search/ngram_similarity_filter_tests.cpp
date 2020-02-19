////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2020 ArangoDB GmbH, Cologne, Germany
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
/// @author Andrei Lobov
////////////////////////////////////////////////////////////////////////////////

#include "tests_shared.hpp"
#include "filter_test_case_base.hpp"
#include "search/ngram_similarity_filter.hpp"
#include "search/tfidf.hpp"
#include "search/bm25.hpp"

#include <functional>

NS_BEGIN(tests)

class ngram_similarity_filter_test_case : public tests::filter_test_case_base {
protected:
};


TEST_P(ngram_similarity_filter_test_case, check_matcher_1) {
  // sequence 1 3 4 ______ 2 -> longest is 134 not 12
  // add segment
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"3\", \"4\", \"5\", \"6\", \"7\", \"2\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("1")
    .push_back("2").push_back("3").push_back("4");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(0.75, boost->value);
    ASSERT_EQ(1, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, check_matcher_2) {
  // sequence 1 1 2 2 3 3 4 4 -> longest is 1234  and freq should be 1 not 2 as 
  // this sequence could not be built twice one after another but only
  // intereaved
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"1\", \"2\", \"2\", \"3\", \"3\", \"4\", \"4\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("1")
    .push_back("2").push_back("3").push_back("4");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    // ensure all iterators contain  attributes 
    ASSERT_TRUE(bool(doc)); 
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(bool(frequency));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(1, boost->value); 
    ASSERT_EQ(1, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, check_matcher_3) {
  // sequence 1 2 1 1 3 4 -> longest is 1234  not 134!
  // add segment
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"2\", \"1\", \"1\", \"3\", \"4\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("1")
    .push_back("2").push_back("3").push_back("4");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    // ensure all iterators contain  attributes 
    ASSERT_TRUE(bool(doc)); 
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(bool(frequency));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(1, boost->value); 
    ASSERT_EQ(1, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, check_matcher_4) {
  // sequence 1 2 1 1 1 1 pattern 1 1 -> longest is 1 1 and frequency is 2
  // add segment
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"2\", \"1\", \"1\", \"1\", \"1\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("1")
    .push_back("1");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    // ensure all iterators contain  attributes 
    ASSERT_TRUE(bool(doc)); 
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(bool(frequency));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(1, boost->value); 
    ASSERT_EQ(2, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, check_matcher_5) {
  // sequence 1 2 1 2 1 2 1 2 1 2 1 2 1 2 1 pattern 1 2 1 -> longest is 1 2 1 and frequency is 4
  // add segment
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"2\", \"1\", \"2\", \"1\", "
      " \"2\", \"1\", \"2\", \"1\", \"2\", \"1\", \"2\", \"1\", \"2\", \"1\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("1")
    .push_back("2").push_back("1");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    // ensure all iterators contain  attributes 
    ASSERT_TRUE(bool(doc)); 
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(bool(frequency));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(1, boost->value); 
    ASSERT_EQ(4, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, check_matcher_6) {
  // sequence 1 1 pattern 1 1 -> longest is 1 1 and frequency is 1
  // checks seek for second term does not  skips it at all
  // add segment
  {
    tests::json_doc_generator gen(
      "[{ \"seq\" : 1, \"field\": [ \"1\", \"1\"] }]",
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();
  irs::order order;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  irs::by_ngram_similarity filter;
  filter.threshold(1.).field("field").push_back("1").push_back("1");

  auto prepared_order = order.prepare();
  auto prepared = filter.prepare(rdr, prepared_order);
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub, prepared_order);
    auto& doc = docs->attributes().get<irs::document>();
    auto& boost = docs->attributes().get<irs::filter_boost>();
    auto& frequency = docs->attributes().get<irs::frequency>();
    // ensure all iterators contain  attributes 
    ASSERT_TRUE(bool(doc)); 
    ASSERT_TRUE(bool(boost));
    ASSERT_TRUE(bool(frequency));
    ASSERT_TRUE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_FALSE(irs::doc_limits::eof(doc->value));
    ASSERT_DOUBLE_EQ(1, boost->value); 
    ASSERT_EQ(1, frequency->value);
    ASSERT_FALSE(docs->next());
  }
}

TEST_P(ngram_similarity_filter_test_case, no_match_case) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.1).field("field").push_back("ee")
    .push_back("we").push_back("qq").push_back("rr")
    .push_back("ff").push_back("never_match");

  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    ASSERT_FALSE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_TRUE(irs::doc_limits::eof(doc->value));
  }
}

TEST_P(ngram_similarity_filter_test_case, no_serial_match_case) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("ee")
    .push_back("ss").push_back("pa").push_back("rr");

  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);
    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    ASSERT_FALSE(docs->next());
    ASSERT_EQ(docs->value(), doc->value);
    ASSERT_TRUE(irs::doc_limits::eof(doc->value));
  }
}

TEST_P(ngram_similarity_filter_test_case, one_match_case) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.1).field("field").push_back("ee")
    .push_back("ss").push_back("qq").push_back("rr")
    .push_back("ff").push_back("never_match");

  docs_t expected{ 1, 3, 5, 6, 7, 8, 9, 10, 12};
  const size_t expected_size = expected.size();
  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

TEST_P(ngram_similarity_filter_test_case, missed_last_test) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll").push_back("never_match");

  docs_t expected{ 1, 2, 5, 8, 11, 12, 13};
  const size_t expected_size = expected.size();
  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

TEST_P(ngram_similarity_filter_test_case, missed_first_test) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");

  docs_t expected{ 1, 2, 5, 8, 11, 12, 13};
  const size_t expected_size = expected.size();
  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

TEST_P(ngram_similarity_filter_test_case, not_miss_match_for_tail) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.33).field("field").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll").push_back("never_match");

  docs_t expected{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  const size_t expected_size = expected.size();
  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}


TEST_P(ngram_similarity_filter_test_case, missed_middle_test) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.333).field("field").push_back("at")
    .push_back("never_match").push_back("la").push_back("as")
    .push_back("ll");

  docs_t expected{ 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14};
  const size_t expected_size = expected.size();

  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

TEST_P(ngram_similarity_filter_test_case, missed_middle2_test) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("at")
    .push_back("never_match").push_back("never_match2").push_back("la").push_back("as")
    .push_back("ll");

  docs_t expected{ 1, 2, 5, 8, 11, 12, 13};
  const size_t expected_size = expected.size();

  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

TEST_P(ngram_similarity_filter_test_case, missed_middle3_test) {
  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.28).field("field").push_back("at")
    .push_back("never_match").push_back("tl").push_back("never_match2").push_back("la").push_back("as")
    .push_back("ll");

  docs_t expected{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  const size_t expected_size = expected.size();

  auto prepared = filter.prepare(rdr, irs::order::prepared::unordered());
  size_t count = 0;
  for (const auto& sub : rdr) {
    auto docs = prepared->execute(sub);

    auto& doc = docs->attributes().get<irs::document>();
    ASSERT_TRUE(bool(doc)); // ensure all iterators contain "document" attribute
    while (docs->next()) {
      ASSERT_EQ(docs->value(), doc->value);
      expected.erase(std::remove(expected.begin(), expected.end(), docs->value()), expected.end());
      ++count;
    }
  }
  ASSERT_EQ(expected_size, count);
  ASSERT_EQ(0, expected.size());
}

struct test_score_ctx : public irs::score_ctx {
  test_score_ctx(std::vector<size_t>* f, const irs::frequency* p, std::vector<irs::boost_t>* b, const irs::filter_boost* fb)
    : freq(f), freq_from_filter(p), filter_boost(b), boost_from_filter(fb)  {}

  std::vector<size_t>* freq;
  std::vector<irs::boost_t>* filter_boost;
  const irs::frequency* freq_from_filter;
  const irs::filter_boost* boost_from_filter;
};

TEST_P(ngram_similarity_filter_test_case, missed_last_scored_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll").push_back("never_match");

  docs_t expected{ 1, 2, 5, 8, 11, 12, 13};
  irs::order order;
  size_t collect_field_count = 0;
  size_t collect_term_count = 0;
  size_t finish_count = 0;
  std::vector<size_t> frequency;
  std::vector<irs::boost_t> filter_boost;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  scorer.collector_collect_field = [&collect_field_count](const irs::sub_reader&, const irs::term_reader&)->void{
    ++collect_field_count;
  };
  scorer.collector_collect_term = [&collect_term_count](const irs::sub_reader&, const irs::term_reader&, const irs::attribute_view&)->void{
    ++collect_term_count;
  };
  scorer.collectors_collect_ = [&finish_count](irs::byte_type*, const irs::index_reader&, const irs::sort::field_collector*, const irs::sort::term_collector*)->void {
    ++finish_count;
  };
  scorer.prepare_field_collector_ = [&scorer]()->irs::sort::field_collector::ptr {
    return irs::memory::make_unique<tests::sort::custom_sort::prepared::collector>(scorer);
  };
  scorer.prepare_term_collector_ = [&scorer]()->irs::sort::term_collector::ptr {
    return irs::memory::make_unique<tests::sort::custom_sort::prepared::collector>(scorer);
  };


  scorer.prepare_scorer = [&frequency, &filter_boost](const irs::sub_reader& segment,
    const irs::term_reader& term,
    const irs::byte_type* data,
    const irs::attribute_view& attr)->std::pair<irs::score_ctx_ptr, irs::score_f> {
      auto& freq = attr.get<irs::frequency>();
      auto& boost = attr.get<irs::filter_boost>();
      return {  
        irs::memory::make_unique<test_score_ctx>(&frequency, freq.get(), &filter_boost, boost.get()),
        [](const irs::score_ctx* ctx, irs::byte_type* RESTRICT score_buf) noexcept {
        auto& freq = *reinterpret_cast<const test_score_ctx*>(ctx);
        freq.freq->push_back(freq.freq_from_filter->value);
        freq.filter_boost->push_back(freq.boost_from_filter->value);
      }
      };
  };
  std::vector<size_t> expectedFrequency{1, 1, 2, 1, 1, 1, 1};
  std::vector<irs::boost_t> expected_filter_boost{4./6., 4./6., 4./6., 4./6., 0.5, 0.5, 0.5};
  check_query(filter, order, expected, rdr);
  ASSERT_EQ(expectedFrequency, frequency);
  ASSERT_EQ(expected_filter_boost.size(), filter_boost.size());
  for (size_t i = 0; i < expected_filter_boost.size(); ++i) {
    SCOPED_TRACE(testing::Message("i=") << i);
    ASSERT_DOUBLE_EQ(expected_filter_boost[i], filter_boost[i]);
  }
  ASSERT_EQ(1, collect_field_count); 
  ASSERT_EQ(5, collect_term_count); 
  ASSERT_EQ(collect_field_count + collect_term_count, finish_count); 
}

TEST_P(ngram_similarity_filter_test_case, missed_frequency_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::generic_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");

  docs_t expected{ 1, 2, 5, 8, 11, 12, 13};
  irs::order order;
  size_t collect_field_count = 0;
  size_t collect_term_count = 0;
  size_t finish_count = 0;
  std::vector<size_t> frequency;
  std::vector<irs::boost_t> filter_boost;
  auto& scorer = order.add<tests::sort::custom_sort>(false);
  scorer.collector_collect_field = [&collect_field_count](const irs::sub_reader&, const irs::term_reader&)->void{
    ++collect_field_count;
  };
  scorer.collector_collect_term = [&collect_term_count](const irs::sub_reader&, const irs::term_reader&, const irs::attribute_view&)->void{
    ++collect_term_count;
  };
  scorer.collectors_collect_ = [&finish_count](irs::byte_type*, const irs::index_reader&, const irs::sort::field_collector*, const irs::sort::term_collector*)->void {
    ++finish_count;
  };
  scorer.prepare_field_collector_ = [&scorer]()->irs::sort::field_collector::ptr {
    return irs::memory::make_unique<tests::sort::custom_sort::prepared::collector>(scorer);
  };
  scorer.prepare_term_collector_ = [&scorer]()->irs::sort::term_collector::ptr {
    return irs::memory::make_unique<tests::sort::custom_sort::prepared::collector>(scorer);
  };


  scorer.prepare_scorer = [&frequency, &filter_boost](const irs::sub_reader& segment,
    const irs::term_reader& term,
    const irs::byte_type* data,
    const irs::attribute_view& attr)->std::pair<irs::score_ctx_ptr, irs::score_f> {
      auto& freq = attr.get<irs::frequency>();
      auto& boost = attr.get<irs::filter_boost>();
      return {  
        irs::memory::make_unique<test_score_ctx>(&frequency, freq.get(), &filter_boost, boost.get()),
        [](const irs::score_ctx* ctx, irs::byte_type* RESTRICT score_buf) noexcept {
        auto& freq = *reinterpret_cast<const test_score_ctx*>(ctx);
        freq.freq->push_back(freq.freq_from_filter->value);
        freq.filter_boost->push_back(freq.boost_from_filter->value);
      }
      };
  };
  std::vector<size_t> expected_frequency{1, 1, 2, 1, 1, 1, 1};
  std::vector<irs::boost_t> expected_filter_boost{4./6., 4./6., 4./6., 4./6., 0.5, 0.5, 0.5};
  check_query(filter, order, expected, rdr);
  ASSERT_EQ(expected_frequency, frequency);
  ASSERT_EQ(expected_filter_boost.size(), filter_boost.size());
  for (size_t i = 0; i < expected_filter_boost.size(); ++i) {
    SCOPED_TRACE(testing::Message("i=") << i);
    ASSERT_DOUBLE_EQ(expected_filter_boost[i], filter_boost[i]);
  }
  ASSERT_EQ(1, collect_field_count); 
  ASSERT_EQ(5, collect_term_count); 
  ASSERT_EQ(collect_field_count + collect_term_count, finish_count); 
}

TEST_P(ngram_similarity_filter_test_case, missed_first_tfidf_norm_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::normalized_string_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");
  docs_t expected{ 11, 12, 8, 13, 5, 1, 2};
  irs::order order;
  order.add<irs::tfidf_sort>(false).normalize(true);

  check_query(filter, order, expected, rdr);
}

TEST_P(ngram_similarity_filter_test_case, missed_first_tfidf_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::normalized_string_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");
  docs_t expected{ 11, 12, 13, 1, 2, 8, 5};
  irs::order order;
  order.add<irs::tfidf_sort>(false).normalize(false);

  check_query(filter, order, expected, rdr);
}

TEST_P(ngram_similarity_filter_test_case, missed_first_bm25_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::normalized_string_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");
  docs_t expected{13, 11, 12, 2, 1, 8, 5};
  irs::order order;
  order.add<irs::bm25_sort>(false);

  check_query(filter, order, expected, rdr);
}

TEST_P(ngram_similarity_filter_test_case, missed_first_bm15_test) {

  // add segment
  {
    tests::json_doc_generator gen(
      resource("ngram_similarity.json"),
      &tests::normalized_string_json_field_factory);
    add_segment( gen );
  }

  auto rdr = open_reader();

  irs::by_ngram_similarity filter;
  filter.threshold(0.5).field("field").push_back("never_match").push_back("at")
    .push_back("tl").push_back("la").push_back("as")
    .push_back("ll");
  docs_t expected{ 13, 11, 12, 2, 1, 8, 5};
  irs::order order;
  order.add<irs::bm25_sort>(false).b(1); // set to BM15 mode

  check_query(filter, order, expected, rdr);
}



INSTANTIATE_TEST_CASE_P(
  ngram_similarity_test,
  ngram_similarity_filter_test_case,
  ::testing::Combine(
    ::testing::Values(
      &tests::memory_directory/*!!!!!,
      &tests::fs_directory,
      &tests::mmap_directory*/
    ),
    ::testing::Values(/*!!!!!!!"1_0",*/ "1_3")
  ),
  tests::to_string
);

NS_END // tests