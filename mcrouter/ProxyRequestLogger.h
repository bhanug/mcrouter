/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class ProxyBase;
struct RequestLoggerContext;

class ProxyRequestLogger {
 public:
  explicit ProxyRequestLogger(ProxyBase* proxy) : proxy_(proxy) {}

  template <class Request>
  void log(const RequestLoggerContext& loggerContext);

 protected:
  ProxyBase* proxy_;

  void logError(mc_res_t result, RequestClass reqClass);
};
}
}
} // facebook::memcache::mcrouter

#include "ProxyRequestLogger-inl.h"
