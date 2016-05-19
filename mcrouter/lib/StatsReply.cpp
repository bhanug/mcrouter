/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "StatsReply.h"

#include <folly/io/IOBuf.h>

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.tcc"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

TypedThriftReply<cpp2::McStatsReply> StatsReply::getReply() {
  /**
   * In the 'stats' IOBuf, we store the string representation returned to
   * clients, e.g.,
   * "STAT stat1 value1\r\nSTAT stat2 value2\r\n..."
   */

  TypedThriftReply<cpp2::McStatsReply> reply(mc_res_ok);
  std::vector<std::string> statsList;

  for (const auto& s : stats_) {
    statsList.emplace_back(
        folly::to<std::string>("STAT ", s.first, ' ', s.second));
  }

  reply->set_stats(std::move(statsList));

  return reply;
}

}}  // facebook::memcache
