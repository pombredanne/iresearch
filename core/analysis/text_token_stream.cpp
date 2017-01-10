//
// IResearch search engine 
// 
// Copyright � 2016 by EMC Corporation, All Rights Reserved
// 
// This software contains the intellectual property of EMC Corporation or is licensed to
// EMC Corporation from third parties. Use of this software and the intellectual property
// contained therein is expressly limited to the terms and conditions of the License
// Agreement under which it is provided by or on behalf of EMC.
// 

#include <cctype>
#include <mutex>
#include <unordered_map>

#if !defined(_MSC_VER)
  #pragma GCC diagnostic ignored "-Wunused-variable"
#endif

  #include <boost/filesystem.hpp>

#if !defined(_MSC_VER)
  #pragma GCC diagnostic pop
#endif

#include <boost/locale/encoding.hpp>

#if !defined(_MSC_VER)
  #pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#else
  #pragma warning(disable: 4512)
#endif

  #include <boost/property_tree/json_parser.hpp>

#if !defined(_MSC_VER)
  #pragma GCC diagnostic pop
#else
  #pragma warning(default: 4512)
#endif

#include <unicode/brkiter.h> // for icu::BreakIterator

#if defined(_MSC_VER)
  #pragma warning(disable: 4512)
#endif

  #include <unicode/normalizer2.h> // for icu::Normalizer2

#if defined(_MSC_VER)
  #pragma warning(default: 4512)
#endif

#include <unicode/translit.h> // for icu::Transliterator

#include "libstemmer.h"

#include "token_attributes.hpp"
#include "utils/hash_utils.hpp"
#include "utils/locale_utils.hpp"
#include "utils/log.hpp"
#include "utils/runtime_utils.hpp"
#include "utils/thread_utils.hpp"
#include "text_token_stream.hpp"

NS_ROOT
NS_BEGIN(analysis)

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

struct text_token_stream::state_t {
  std::shared_ptr<BreakIterator> break_iterator;
  UnicodeString data;
  Locale locale;
  std::shared_ptr<const Normalizer2> normalizer;
  std::shared_ptr<sb_stemmer> stemmer;
  std::string tmp_buf; // used by processTerm(...)
  std::shared_ptr<Transliterator> transliterator;
};

NS_END // analysis
NS_END // ROOT

NS_LOCAL

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

class bytes_term: public iresearch::term_attribute {
 public:
  DECLARE_FACTORY_DEFAULT();

  virtual ~bytes_term() {}

  virtual void clear() override {
    buf_.clear();
    value_ = iresearch::bytes_ref::nil;
  }

  virtual const iresearch::bytes_ref& value() const {
    return value_;
  }

  void value(iresearch::bstring&& data) {
    buf_ = std::move(data);
    value(buf_);
  }

  void value(const iresearch::bytes_ref& data) {
    value_ = data;
  }

 private:
  iresearch::bstring buf_; // buffer for value if value cannot be referenced directly
  iresearch::bytes_ref value_;
};

DEFINE_FACTORY_DEFAULT(bytes_term);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

typedef std::unordered_set<std::string> ignored_words_t;
typedef std::pair<std::locale, ignored_words_t> cached_state_t;
static std::unordered_map<iresearch::hashed_string_ref, cached_state_t> cached_state_by_key;
static std::mutex mutex;

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief retrieves a set of ignored words from FS at the specified custom path
////////////////////////////////////////////////////////////////////////////////
bool get_ignored_words(
  ignored_words_t& buf,
  const std::locale& locale,
  const std::string* path = nullptr
) {
  auto language = iresearch::locale_utils::language(locale);
  boost::filesystem::path stopword_path;
  auto* custom_stopword_path =
    path
    ? path->c_str()
    : iresearch::getenv(iresearch::analysis::text_token_stream::STOPWORD_PATH_ENV_VARIABLE)
    ;

  if (custom_stopword_path) {
    stopword_path = boost::filesystem::path(custom_stopword_path);

    if (!stopword_path.is_absolute()) {
      stopword_path = boost::filesystem::current_path() /= stopword_path;
    }
  }
  else {
    // use CWD if the environment variable STOPWORD_PATH_ENV_VARIABLE is undefined
    stopword_path = boost::filesystem::current_path();
  }

  try {
    if (!boost::filesystem::is_directory(stopword_path) ||
        !boost::filesystem::is_directory(stopword_path.append(language))) {
      return false;
    }

    ignored_words_t ignored_words;

    for (boost::filesystem::directory_iterator dir_itr(stopword_path), end; dir_itr != end; ++dir_itr) {
      if (boost::filesystem::is_directory(dir_itr->status())) {
        continue;
      }

      std::ifstream in(dir_itr->path().native());

      if (!in) {
        return false;
      }

      for (std::string line; std::getline(in, line);) {
        size_t i = 0;

        // find first whitespace
        for (size_t length = line.size(); i < length && !std::isspace(line[i]); ++i);

        // skip lines starting with whitespace
        if (i > 0) {
          ignored_words.insert(line.substr(0, i));
        }
      }
    }

    buf.insert(ignored_words.begin(), ignored_words.end());

    return true;
  } catch (...) {
    // NOOP
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer with supplied ignored_words and cache its state
////////////////////////////////////////////////////////////////////////////////
iresearch::analysis::analyzer::ptr construct(
  const iresearch::string_ref& cache_key,
  const std::locale& locale,
  ignored_words_t&& ignored_words
) {
  cached_state_t* cached_state;

  {
    SCOPED_LOCK(mutex);

    cached_state = &(cached_state_by_key.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(iresearch::make_hashed_ref(cache_key, iresearch::string_ref_hash_t())),
      std::forward_as_tuple(locale, std::move(ignored_words))
    ).first->second);
  }

  return iresearch::analysis::analyzer::ptr(
    new iresearch::analysis::text_token_stream(cached_state->first, cached_state->second)
  );
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer based on the supplied cache_key
////////////////////////////////////////////////////////////////////////////////
iresearch::analysis::analyzer::ptr construct(
  const iresearch::string_ref& cache_key
) {
  {
    SCOPED_LOCK(mutex);
    auto itr = cached_state_by_key.find(
      iresearch::make_hashed_ref(cache_key, iresearch::string_ref_hash_t())
    );

    if (itr != cached_state_by_key.end()) {
      return iresearch::analysis::analyzer::ptr(
        new iresearch::analysis::text_token_stream(itr->second.first, itr->second.second)
      );
    }
  }

  // interpret the cache_key as a locale name
  std::string locale_name(cache_key.c_str(), cache_key.size());
  auto locale = iresearch::locale_utils::locale(locale_name);
  ignored_words_t buf;

  return get_ignored_words(buf, locale, nullptr)
    ? construct(cache_key, locale, std::move(buf))
    : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer for the supplied locale and default values
////////////////////////////////////////////////////////////////////////////////
iresearch::analysis::analyzer::ptr construct(
  const iresearch::string_ref& cache_key,
  const std::locale& locale
) {
  ignored_words_t buf;

  return get_ignored_words(buf, locale)
    ? construct(cache_key, locale, std::move(buf))
    : nullptr
    ;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer with supplied ignore_word_path
////////////////////////////////////////////////////////////////////////////////
iresearch::analysis::analyzer::ptr construct(
  const iresearch::string_ref& cache_key,
  const std::locale& locale,
  const std::string& ignored_word_path
) {
  ignored_words_t buf;

  return get_ignored_words(buf, locale, &ignored_word_path)
    ? construct(cache_key, locale, std::move(buf))
    : nullptr
    ;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer with supplied ignored_words and ignore_word_path
////////////////////////////////////////////////////////////////////////////////
iresearch::analysis::analyzer::ptr construct(
  const iresearch::string_ref& cache_key,
  const std::locale& locale,
  const std::string& ignored_word_path,
  ignored_words_t&& ignored_words
) {
  return get_ignored_words(ignored_words, locale, &ignored_word_path)
    ? construct(cache_key, locale, std::move(ignored_words))
    : nullptr
    ;
}

bool process_term(
  iresearch::attributes& attrs,
  const std::unordered_set<std::string>& ignored_words,
  iresearch::analysis::text_token_stream::state_t& state,
  UnicodeString const& data
) {
  // ...........................................................................
  // normalize unicode
  // ...........................................................................
  UnicodeString word;
  UErrorCode err = U_ZERO_ERROR; // a value that passes the U_SUCCESS() test

  state.normalizer->normalize(data, word, err);

  if (!U_SUCCESS(err)) {
    word = data; // use non-normalized value if normalization failure
  }

  // ...........................................................................
  // case-convert unicode
  // ...........................................................................
  word.toLower(state.locale); // inplace case-conversion

  // ...........................................................................
  // collate value, e.g. remove accents
  // ...........................................................................
  state.transliterator->transliterate(word); // inplace translitiration

  std::string& word_utf8 = state.tmp_buf;

  word_utf8.clear();
  word.toUTF8String(word_utf8);

  // ...........................................................................
  // skip ignored tokens
  // ...........................................................................
  if (ignored_words.find(word_utf8) != ignored_words.end()) {
    return false;
  }

  // ...........................................................................
  // get value holder
  // ...........................................................................
  auto& term = attrs.add<bytes_term>();

  // ...........................................................................
  // find the token stem
  // ...........................................................................
  if (state.stemmer) {
    static_assert(sizeof(sb_symbol) == sizeof(char), "sizeof(sb_symbol) != sizeof(char)");
    const sb_symbol* value = reinterpret_cast<sb_symbol const*>(word_utf8.c_str());

    value = sb_stemmer_stem(state.stemmer.get(), value, (int)word_utf8.size());

    if (value) {
      static_assert(sizeof(iresearch::byte_type) == sizeof(sb_symbol), "sizeof(iresearch::byte_type) != sizeof(sb_symbol)");
      term->value(iresearch::bytes_ref(reinterpret_cast<const iresearch::byte_type*>(value), sb_stemmer_length(state.stemmer.get())));

      return true;
    }
  }

  // ...........................................................................
  // use the value of the unstemmed token
  // ...........................................................................
  static_assert(sizeof(iresearch::byte_type) == sizeof(char), "sizeof(iresearch::byte_type) != sizeof(char)");
  term->value(iresearch::bstring(iresearch::ref_cast<iresearch::byte_type>(word_utf8).c_str(), word_utf8.size()));

  return true;
}

NS_END

NS_ROOT
NS_BEGIN(analysis)

// -----------------------------------------------------------------------------
// --SECTION--                                                  static variables
// -----------------------------------------------------------------------------

char const* text_token_stream::STOPWORD_PATH_ENV_VARIABLE = "IRESEARCH_TEXT_STOPWORD_PATH";

// -----------------------------------------------------------------------------
// --SECTION--                                                  static functions
// -----------------------------------------------------------------------------

DEFINE_ANALYZER_TYPE_NAMED(text_token_stream, "text");
REGISTER_ANALYZER(text_token_stream);
/*static*/ analyzer::ptr text_token_stream::make(const string_ref& args) {
  auto stream = construct(args);

  if (stream) {
    return stream;
  }

  // try to parse 'args' as a jSON config
  try {
    std::stringstream args_stream(std::string(args.c_str(), args.size()));
    ::boost::property_tree::ptree pt;

    ::boost::property_tree::read_json(args_stream, pt);

    auto locale = iresearch::locale_utils::locale(pt.get<std::string>("locale"));
    auto ignored_words = pt.get_child_optional("ignored_words");
    auto ignored_words_path = pt.get_optional<std::string>("ignored_words_path");

    if (!ignored_words) {
      return ignored_words_path
        ? construct(args, locale, ignored_words_path.value())
        : construct(args, locale)
        ;
    }

    ignored_words_t buf;

    for (auto& ignored_word: *ignored_words) {
     buf.emplace(ignored_word.second.get_value<std::string>());
    }

    return ignored_words_path
      ? construct(args, locale, ignored_words_path.value(), std::move(buf))
      : construct(args, locale, std::move(buf))
      ;
  }
  catch (...) {
    IR_ERROR() << "Caught error while constructing text_token_stream from jSON arguments: " << args;
  }

  return nullptr;
}

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

text_token_stream::text_token_stream(
  const std::locale& locale,
  const std::unordered_set<std::string>& ignored_words
):
  analyzer(text_token_stream::type()),
  attrs_(3), // offset + bytes_term + increment
  state_(new state_t),
  ignored_words_(ignored_words) {
  attrs_.add<offset>();
  attrs_.add<bytes_term>();
  attrs_.add<increment>();
  locale_.country = locale_utils::country(locale);
  locale_.encoding = locale_utils::encoding(locale);
  locale_.language = locale_utils::language(locale);
  locale_.utf8 = locale_utils::utf8(locale);
  state_->locale.setToBogus(); // set to uninitialized
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

const iresearch::attributes& text_token_stream::attributes() const NOEXCEPT {
  return attrs_;
}

bool text_token_stream::reset(const string_ref& data) {
  if (state_->locale.isBogus()) {
    state_->locale = Locale(locale_.language.c_str(), locale_.country.c_str());

    if (state_->locale.isBogus()) {
      return false;
    }
  }

  UErrorCode err = U_ZERO_ERROR; // a value that passes the U_SUCCESS() test

  if (!state_->normalizer) {
    // reusable object owned by ICU
    state_->normalizer.reset(Normalizer2::getNFCInstance(err), [](const Normalizer2*)->void{});

    if (!U_SUCCESS(err) || !state_->normalizer) {
      state_->normalizer.reset();

      return false;
    }
  }

  if (!state_->transliterator) {
    // transliteration rule taken verbatim from: http://userguide.icu-project.org/transforms/general
    static UnicodeString collationRule = "NFD; [:Nonspacing Mark:] Remove; NFC";

    // reusable object owned by *this
    state_->transliterator.reset(
      Transliterator::createInstance(collationRule, UTransDirection::UTRANS_FORWARD, err)
    );

    if (!U_SUCCESS(err) || !state_->transliterator) {
      state_->transliterator.reset();

      return false;
    }
  }

  if (!state_->break_iterator) {
    // reusable object owned by *this
    state_->break_iterator.reset(BreakIterator::createWordInstance(state_->locale, err));

    if (!U_SUCCESS(err) || !state_->break_iterator) {
      state_->break_iterator.reset();

      return false;
    }
  }

  // optional since not available for all locales
  if (!state_->stemmer) {
    // reusable object owned by *this
    state_->stemmer.reset(
      sb_stemmer_new(locale_.language.c_str(), nullptr), // defaults to utf-8
      [](sb_stemmer* ptr)->void{ sb_stemmer_delete(ptr); }
    );
  }

  // ...........................................................................
  // convert encoding to UTF8 for use with ICU
  // ...........................................................................
  if (locale_.utf8) {
    if (data.size() > INT32_MAX) {
      return false; // ICU UnicodeString signatures can handle at most INT32_MAX
    }

    state_->data =
      UnicodeString::fromUTF8(StringPiece(data.c_str(), (int32_t)(data.size())));
  }
  else {
    std::string data_utf8 = boost::locale::conv::to_utf<char>(
      data.c_str(), data.c_str() + data.size(), locale_.encoding
    );

    if (data_utf8.size() > INT32_MAX) {
      return false; // ICU UnicodeString signatures can handle at most INT32_MAX
    }

    state_->data =
      UnicodeString::fromUTF8(StringPiece(data_utf8.c_str(), (int32_t)(data_utf8.size())));
  }

  // ...........................................................................
  // tokenise the unicode data
  // ...........................................................................
  state_->break_iterator->setText(state_->data);

  return true;
}

bool text_token_stream::next() {
  auto& offset = attrs_.add<iresearch::offset>();

  // ...........................................................................
  // find boundaries of the next word
  // ...........................................................................
  for (auto start = state_->break_iterator->current(), end = state_->break_iterator->next();
    BreakIterator::DONE != end;
    start = end, end = state_->break_iterator->next()) {

    // ...........................................................................
    // skip whitespace and unsuccessful terms
    // ...........................................................................
    if (state_->break_iterator->getRuleStatus() == UWordBreak::UBRK_WORD_NONE ||
        !process_term(attrs_, ignored_words_, *state_, state_->data.tempSubString(start, end - start))) {
      continue;
    }

    offset->start = start;
    offset->end = end;
    return true;
  }

  return false;
}

NS_END // analysis
NS_END // ROOT

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------