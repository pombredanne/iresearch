////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 by EMC Corporation, All Rights Reserved
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
/// Copyright holder is EMC Corporation
///
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#ifndef IRESEARCH_NONCOPYABLE_H
#define IRESEARCH_NONCOPYABLE_H

#include "shared.hpp"

NS_ROOT
NS_BEGIN(util)

struct noncopyable {
  noncopyable() = default;

  noncopyable(noncopyable&&) = default;
  noncopyable& operator=(noncopyable&&) = default;

  noncopyable(const noncopyable&) = delete;
  noncopyable& operator= (const noncopyable&) = delete;
};

NS_END
NS_END

#endif
