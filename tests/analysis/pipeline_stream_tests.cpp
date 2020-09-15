////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2020 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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

#include <vector>
#include "gtest/gtest.h"
#include "tests_config.hpp"

#include "analysis/pipeline_token_stream.hpp"
#include "analysis/text_token_stream.hpp"
#include "analysis/ngram_token_stream.hpp"
#include "analysis/delimited_token_stream.hpp"
#include "analysis/text_token_stream.hpp"

#include "analysis/token_attributes.hpp"
#include "analysis/token_stream.hpp"
#include "utils/locale_utils.hpp"
#include "utils/runtime_utils.hpp"
#include "utils/utf8_path.hpp"

#include <rapidjson/document.h> // for rapidjson::Document, rapidjson::Value

#ifndef IRESEARCH_DLL
NS_LOCAL
struct analyzer_token {
	irs::string_ref value;
	size_t start;
	size_t end;
	uint32_t pos;
};

using analyzer_tokens = std::vector<analyzer_token>;


void assert_pipeline(irs::analysis::pipeline_token_stream& pipe, const std::string& data, const analyzer_tokens& expected_tokens) {
	SCOPED_TRACE(data);
	auto* offset = irs::get<irs::offset>(pipe);
	ASSERT_TRUE(offset);
	auto* term = irs::get<irs::term_attribute>(pipe);
	ASSERT_TRUE(term);
	auto* inc = irs::get<irs::increment>(pipe);
	ASSERT_TRUE(inc);
	ASSERT_TRUE(pipe.reset(data));
	uint32_t pos { irs::integer_traits<uint32_t>::const_max };
	auto expected_token = expected_tokens.begin();
	while (pipe.next()) {
		auto term_value = std::string(irs::ref_cast<char>(term->value).c_str(), term->value.size());
		auto old_pos = pos;
		pos += inc->value;
#ifdef IRESEARCH_DEBUG //TODO: remove me
		std::cerr << term_value << "(" << pos << ")" <<"|";
#endif
		ASSERT_NE(expected_token, expected_tokens.end());
		ASSERT_EQ(irs::ref_cast<irs::byte_type>(expected_token->value), term->value);
		ASSERT_EQ(expected_token->start, offset->start);
		ASSERT_EQ(expected_token->end, offset->end);
		ASSERT_EQ(expected_token->pos, pos);
		++expected_token;
	}
	ASSERT_EQ(expected_token, expected_tokens.end());
#ifdef IRESEARCH_DEBUG //TODO: remove me
	std::cerr << std::endl;
#endif
}

NS_END

TEST(pipeline_token_stream_test, many_tokenizers) {
	auto delimiter = irs::analysis::analyzers::get("delimiter",
		irs::type<irs::text_format::json>::get(),
		"{\"delimiter\":\",\"}");

	auto delimiter2 = irs::analysis::analyzers::get("delimiter",
		irs::type<irs::text_format::json>::get(),
		"{\"delimiter\":\" \"}");

	auto text = irs::analysis::analyzers::get("text",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en_US.UTF-8\", \"stopwords\":[], \"case\":\"none\", \"stemming\":false }");

	auto ngram = irs::analysis::analyzers::get("ngram",
		irs::type<irs::text_format::json>::get(),
		"{\"min\":2, \"max\":2, \"preserveOriginal\":true }");

	irs::analysis::pipeline_token_stream::options_t pipeline_options;
	pipeline_options.pipeline.push_back(delimiter);
	pipeline_options.pipeline.push_back(delimiter2);
	pipeline_options.pipeline.push_back(text);
	pipeline_options.pipeline.push_back(ngram);

	irs::analysis::pipeline_token_stream pipe(pipeline_options);

	std::string data = "quick broWn,, FOX  jumps,  over lazy dog";
	const analyzer_tokens expected {
		{ "qu", 0, 2, 0},
		{ "quick", 0, 5, 0},
		{ "ui", 1, 3, 1},
		{ "ic", 2, 4, 2},
		{ "ck", 3, 5, 3},
		{ "br", 6, 8, 4},
		{ "broWn", 6, 11, 4},
		{ "ro", 7, 9, 5},
		{ "oW", 8, 10, 6},
		{ "Wn", 9, 11, 7},
		{ "FO", 14, 16, 8},
		{ "FOX", 14, 17, 8},
		{ "OX", 15, 17, 9},
		{ "ju", 19, 21, 10},
		{ "jumps", 19, 24, 10},
		{ "um", 20, 22, 11},
		{ "mp", 21, 23, 12},
		{ "ps", 22, 24, 13},
		{ "ov", 27, 29, 14},
		{ "over", 27, 31, 14},
		{ "ve", 28, 30, 15},
		{ "er", 29, 31, 16},
		{ "la", 32, 34, 17},
		{ "lazy", 32, 36, 17},
		{ "az", 33, 35, 18},
		{ "zy", 34, 36, 19},
		{ "do", 37, 39, 20},
		{ "dog", 37, 40, 20},
		{ "og", 38, 40, 21},
	};
	assert_pipeline(pipe, data, expected);
}

TEST(pipeline_token_stream_test, overlapping_ngrams) {

	auto ngram = irs::analysis::analyzers::get("ngram",
		irs::type<irs::text_format::json>::get(),
		"{\"min\":6, \"max\":7, \"preserveOriginal\":false }");
	auto ngram2 = irs::analysis::analyzers::get("ngram",
		irs::type<irs::text_format::json>::get(),
		"{\"min\":2, \"max\":3, \"preserveOriginal\":false }");

	irs::analysis::pipeline_token_stream::options_t pipeline_options;
	pipeline_options.pipeline.push_back(ngram);
	pipeline_options.pipeline.push_back(ngram2);
	irs::analysis::pipeline_token_stream pipe(pipeline_options);

	std::string data = "ABCDEFJH";
	const analyzer_tokens expected{
		{"AB", 0, 2, 0}, {"ABC", 0, 3, 0}, {"BC", 1, 3, 1}, {"BCD", 1, 4, 1},	{"CD", 2, 4, 2}, {"CDE", 2, 5, 2},	{"DE", 3, 5, 3},
		{"DEF", 3, 6, 3}, {"EF", 4, 6, 4},
		{"AB", 0, 2, 5}, {"ABC", 0, 3, 5},	{"BC", 1, 3, 6}, {"BCD", 1, 4, 6},	{"CD", 2, 4, 7}, {"CDE", 2, 5, 7},	{"DE", 3, 5, 8},
		{"DEF", 3, 6, 8}, {"EF", 4, 6, 9},	{"EFJ", 4, 7, 9}, {"FJ", 5, 7, 10},	
		{"BC", 1, 3, 11}, {"BCD", 1, 4, 11}, {"CD", 2, 4, 12},	{"CDE", 2, 5, 12}, {"DE", 3, 5, 13},
		{"DEF", 3, 6, 13}, {"EF", 4, 6, 14}, {"EFJ", 4, 7, 14}, {"FJ", 5, 7, 15},
		{"BC", 1, 3, 16}, {"BCD", 1, 4, 16}, {"CD", 2, 4, 17},	{"CDE", 2, 5, 17}, {"DE", 3, 5, 18},
		{"DEF", 3, 6, 18}, {"EF", 4, 6, 19}, {"EFJ", 4, 7, 19}, {"FJ", 5, 7, 20},
		{"FJH", 5, 8, 20}, {"JH", 6, 8, 21},
		{"CD", 2, 4, 22},	{"CDE", 2, 5, 22}, {"DE", 3, 5, 23},
		{"DEF", 3, 6, 23}, {"EF", 4, 6, 24}, {"EFJ", 4, 7, 24}, {"FJ", 5, 7, 25},
		{"FJH", 5, 8, 25}, {"JH", 6, 8, 26},
	};
	assert_pipeline(pipe, data, expected);
}


TEST(pipeline_token_stream_test, case_ngrams) {
	auto ngram = irs::analysis::analyzers::get("ngram",
		irs::type<irs::text_format::json>::get(),
		"{\"min\":3, \"max\":3, \"preserveOriginal\":false }");
	auto norm = irs::analysis::analyzers::get("norm",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en\", \"case\":\"upper\"}");
	std::string data = "QuIck BroWN FoX";
	const analyzer_tokens expected{
		{"QUI", 0, 3, 0},	{"UIC", 1, 4, 1}, {"ICK", 2, 5, 2},
		{"CK ", 3, 6, 3},	{"K B", 4, 7, 4},	{" BR", 5, 8, 5},
		{"BRO", 6, 9, 6},	{"ROW", 7, 10, 7}, {"OWN", 8, 11, 8},
		{"WN ", 9, 12, 9}, {"N F", 10, 13, 10},	{" FO", 11, 14, 11},
		{"FOX", 12, 15, 12},
	};
	{
		irs::analysis::pipeline_token_stream::options_t pipeline_options;
		pipeline_options.pipeline.push_back(ngram);
		pipeline_options.pipeline.push_back(norm);
		irs::analysis::pipeline_token_stream pipe(pipeline_options);
		assert_pipeline(pipe, data, expected);
	}
	{
		irs::analysis::pipeline_token_stream::options_t pipeline_options;
		pipeline_options.pipeline.push_back(norm);
		pipeline_options.pipeline.push_back(ngram);
		irs::analysis::pipeline_token_stream pipe(pipeline_options);
		assert_pipeline(pipe, data, expected);
	}
}

TEST(pipeline_token_stream_test, no_tokenizers) {
	auto norm1 = irs::analysis::analyzers::get("norm",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en\", \"case\":\"upper\"}");
	auto norm2 = irs::analysis::analyzers::get("norm",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en\", \"case\":\"lower\"}");
	std::string data = "QuIck";
	const analyzer_tokens expected{
		{"quick", 0, 5, 0},
	};
	irs::analysis::pipeline_token_stream::options_t pipeline_options;
	pipeline_options.pipeline.push_back(norm1);
	pipeline_options.pipeline.push_back(norm2);
	irs::analysis::pipeline_token_stream pipe(pipeline_options);
	assert_pipeline(pipe, data, expected);
}

TEST(pipeline_token_stream_test, source_modification_tokenizer) {
	auto text = irs::analysis::analyzers::get("text",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en_US.UTF-8\", \"stopwords\":[], \"case\":\"none\", \"stemming\":true }");
	auto norm = irs::analysis::analyzers::get("norm",
		irs::type<irs::text_format::json>::get(),
		"{\"locale\":\"en\", \"case\":\"lower\"}");
	std::string data = "QuIck broWn fox jumps";
	const analyzer_tokens expected{
		{"quick", 0, 5, 0},
		{"brown", 6, 11, 1},
		{"fox", 12, 15, 2},
		{"jump", 16, 21, 3}
	};
	{
		irs::analysis::pipeline_token_stream::options_t pipeline_options;
		pipeline_options.pipeline.push_back(text);
		pipeline_options.pipeline.push_back(norm);
		irs::analysis::pipeline_token_stream pipe(pipeline_options);
		assert_pipeline(pipe, data, expected);
	}
	{
		irs::analysis::pipeline_token_stream::options_t pipeline_options;
		pipeline_options.pipeline.push_back(norm);
		pipeline_options.pipeline.push_back(text);
		irs::analysis::pipeline_token_stream pipe(pipeline_options);
		assert_pipeline(pipe, data, expected);
	}
}

#endif