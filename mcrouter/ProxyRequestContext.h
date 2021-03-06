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

#include <memory>
#include <string>

#include <folly/fibers/FiberManager.h>

#include "mcrouter/ProxyRequestLogger.h"
#include "mcrouter/ProxyRequestPriority.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/RequestLoggerContext.h"
#include "mcrouter/lib/carbon/NoopAdditionalLogger.h"

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

template <class RouterInfo>
class Proxy;
template <class RouterInfo>
class ProxyRoute;

class ProxyBase;
class CarbonRouterClientBase;
class ShardSplitter;

/**
 * This object is alive for the duration of user's request,
 * including any subrequests that might have been sent out.
 *
 * It starts it's life under a unique_ptr outside of proxy threads.
 * When handed off to a proxy thread and ready to execute,
 * we save the current configuration and convert it to shared
 * ownership.
 *
 * Records collected stats on destruction.
 */
class ProxyRequestContext {
 public:
  using ClientCallback =
      std::function<void(folly::StringPiece, size_t, const AccessPoint&)>;
  using ShardSplitCallback = std::function<void(const ShardSplitter&)>;

  virtual ~ProxyRequestContext();

  ProxyBase& proxy() {
    return proxyBase_;
  }

  bool recording() const noexcept {
    return recording_;
  }

  void recordDestination(
      folly::StringPiece poolName,
      size_t index,
      const AccessPoint& ap) const {
    if (recording_ && recordingState_->clientCallback) {
      recordingState_->clientCallback(poolName, index, ap);
    }
  }

  void recordShardSplitter(const ShardSplitter& splitter) const {
    if (recording_ && recordingState_->shardSplitCallback) {
      recordingState_->shardSplitCallback(splitter);
    }
  }

  uint64_t senderId() const;

  void setSenderIdForTest(uint64_t id);

  bool failoverDisabled() const {
    return failoverDisabled_;
  }

  ProxyRequestPriority priority() const {
    return priority_;
  }

  /**
   * Continues processing current request.
   * Should be called only from the attached proxy thread.
   */
  virtual void startProcessing() {
    throw std::logic_error(
        "Calling startProcessing on an incomplete instance "
        "of ProxyRequestContext");
  }

  const std::string& userIpAddress() const noexcept {
    return userIpAddr_;
  }

  void setUserIpAddress(folly::StringPiece newAddr) noexcept {
    userIpAddr_ = newAddr.str();
  }

  bool isProcessing() const {
    return processing_;
  }
  void markAsProcessing() {
    processing_ = true;
  }

  void setRequester(std::shared_ptr<CarbonRouterClientBase> requester) {
    requester_ = std::move(requester);
  }

 protected:
  bool replied_{false};

  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(ProxyRequestContext& preq){nullptr};

  ProxyRequestContext(ProxyBase& pr, ProxyRequestPriority priority__);

  enum RecordingT { Recording };
  ProxyRequestContext(
      RecordingT,
      ProxyBase& pr,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback);

 private:
  ProxyBase& proxyBase_;
  bool failoverDisabled_{false};

  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_{false};

  bool recording_{false};

  std::shared_ptr<CarbonRouterClientBase> requester_;

  struct RecordingState {
    ClientCallback clientCallback;
    ShardSplitCallback shardSplitCallback;
  };

  union {
    void* context_{nullptr};
    std::unique_ptr<RecordingState> recordingState_;
  };

  uint64_t senderIdForTest_{0};

  ProxyRequestPriority priority_{ProxyRequestPriority::kCritical};

  std::string userIpAddr_;

  ProxyRequestContext(const ProxyRequestContext&) = delete;
  ProxyRequestContext(ProxyRequestContext&&) noexcept = delete;
  ProxyRequestContext& operator=(const ProxyRequestContext&) = delete;
  ProxyRequestContext& operator=(ProxyRequestContext&&) = delete;

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    using ReqCompleteFunc = void (*)(ProxyRequestContext&);

    static ReqCompleteFunc& reqComplete(ProxyRequestContext& preq) {
      return preq.reqComplete_;
    }

    static void*& context(ProxyRequestContext& preq) {
      assert(!preq.recording_);
      return preq.context_;
    }

    static bool& failoverDisabled(ProxyRequestContext& preq) {
      return preq.failoverDisabled_;
    }
  };

 private:
  friend class ProxyBase;
};

namespace detail {

template <class T>
struct Void {
  using type = void;
};

template <class RouterInfo, class U = void>
struct RouterAdditionalLogger {
  using type = carbon::NoopAdditionalLogger;
};
template <class RouterInfo>
struct RouterAdditionalLogger<
    RouterInfo,
    typename Void<typename RouterInfo::AdditionalLogger>::type> {
  using type = typename RouterInfo::AdditionalLogger;
};

} // detail

template <class RouterInfo>
class ProxyRequestContextWithInfo : public ProxyRequestContext {
 public:
  /**
   * A request with this context will not be sent/logged anywhere.
   *
   * @param clientCallback  If non-nullptr, called by DestinationRoute when
   *   the request would normally be sent to destination;
   *   also in traverse() of DestinationRoute.
   * @param shardSplitCallback  If non-nullptr, called by ShardSplitRoute
   *   in traverse() with itself as the argument.
   */
  static std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>>
  createRecording(
      ProxyBase& proxy,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback = nullptr) {
    return std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>>(
        new ProxyRequestContextWithInfo<RouterInfo>(
            Recording,
            proxy,
            std::move(clientCallback),
            std::move(shardSplitCallback)));
  }

  /**
   * Same as createRecording(), but also notifies the baton
   * when this context is destroyed (i.e. all requests referencing it
   * finish executing).
   */
  static std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>>
  createRecordingNotify(
      ProxyBase& proxy,
      folly::fibers::Baton& baton,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback = nullptr) {
    return std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>>(
        new ProxyRequestContextWithInfo<RouterInfo>(
            Recording,
            proxy,
            std::move(clientCallback),
            std::move(shardSplitCallback)),
        [&baton](ProxyRequestContext* ctx) {
          delete ctx;
          baton.post();
        });
  }

  ~ProxyRequestContextWithInfo() override {
    if (reqComplete_) {
      fiber_local<RouterInfo>::runWithoutLocals(
          [this]() { reqComplete_(*this); });
    }
  }

  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <class Request>
  void onReplyReceived(
      const std::string& poolName,
      const AccessPoint& ap,
      folly::StringPiece strippedRoutingPrefix,
      const Request& request,
      const ReplyT<Request>& reply,
      RequestClass requestClass,
      const int64_t startTimeUs,
      const int64_t endTimeUs,
      const ReplyStatsContext replyStatsContext) {
    if (recording()) {
      return;
    }

    RequestLoggerContext loggerContext(
        poolName,
        ap,
        strippedRoutingPrefix,
        requestClass,
        startTimeUs,
        endTimeUs,
        reply.result(),
        replyStatsContext);
    assert(logger_.hasValue());
    logger_->log<Request>(loggerContext);
    assert(additionalLogger_.hasValue());
    additionalLogger_->log(request, reply, loggerContext);
  }

 private:
  using AdditionalLogger =
      typename detail::RouterAdditionalLogger<RouterInfo>::type;

 protected:
  ProxyRequestContextWithInfo(
      Proxy<RouterInfo>& pr,
      ProxyRequestPriority priority__)
      : ProxyRequestContext(pr, priority__),
        logger_(ProxyRequestLogger(&pr)),
        additionalLogger_(AdditionalLogger(&pr)) {}

 private:
  ProxyRequestContextWithInfo(
      RecordingT,
      ProxyBase& proxyBase,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback = nullptr)
      : ProxyRequestContext(
            Recording,
            proxyBase,
            std::move(clientCallback),
            std::move(shardSplitCallback)) {}

  folly::Optional<ProxyRequestLogger> logger_;
  folly::Optional<AdditionalLogger> additionalLogger_;
};

} // mcrouter
} // memcache
} // facebook
