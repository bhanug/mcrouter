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
#include <unordered_map>
#include <vector>

#include <mcrouter/McrouterClient.h>
#include <mcrouter/lib/CacheClientStats.h>
#include <mcrouter/lib/carbon/connection/CarbonConnectionUtil.h>
#include <mcrouter/options.h>

namespace carbon {

template <class If>
class InternalCarbonConnectionImpl {
 public:
  using RecreateFunc = std::function<std::unique_ptr<If>()>;

  struct Options {
    Options() {}
    size_t maxOutstanding{1024};
    size_t maxOutstandingError{false};
  };

  explicit InternalCarbonConnectionImpl(
      const std::string& persistenceId,
      RecreateFunc recreateFunc,
      const Options& options = Options())
      : recreateFunc_(std::move(recreateFunc)) {
    init(
        facebook::memcache::mcrouter::CarbonRouterInstance<
            typename If::RouterInfo>::get(persistenceId),
        options);
  }

  explicit InternalCarbonConnectionImpl(
      const std::string& persistenceId,
      const facebook::memcache::McrouterOptions& mcrouterOptions,
      RecreateFunc recreateFunc,
      const Options& options = Options())
      : recreateFunc_(std::move(recreateFunc)) {
    init(
        facebook::memcache::mcrouter::CarbonRouterInstance<
            typename If::RouterInfo>::init(persistenceId, mcrouterOptions),
        options);
  }

  explicit InternalCarbonConnectionImpl(
      const facebook::memcache::McrouterOptions& mcrouterOptions,
      RecreateFunc recreateFunc,
      const Options& options = Options())
      : recreateFunc_(std::move(recreateFunc)) {
    transientRouter_ = facebook::memcache::mcrouter::CarbonRouterInstance<
        typename If::RouterInfo>::create(mcrouterOptions.clone());
    init(transientRouter_.get(), options);
  }

  facebook::memcache::CacheClientCounters getStatCounters() const noexcept {
    return client_->getStatCounters();
  }

  bool healthCheck() const {
    return true;
  }

  std::unordered_map<std::string, std::string> getConfigOptions() const {
    return router_->opts().toDict();
  }

  template <class Impl>
  std::unique_ptr<If> recreate() {
    return recreateFunc_();
  }

  template <class Request>
  void sendRequestOne(const Request& req, RequestCb<Request> cb) {
    if (!client_->send(req, std::move(cb))) {
      throw CarbonConnectionRecreateException(
          "mcrouter instance was destroyed");
    }
  }

  template <class Request>
  void sendRequestMulti(
      std::vector<std::reference_wrapper<const Request>>&& reqs,
      RequestCb<Request> cb) {
    if (!client_->send(reqs.begin(), reqs.end(), std::move(cb))) {
      throw CarbonConnectionRecreateException(
          "mcrouter instance was destroyed");
    }
  }

 private:
  facebook::memcache::mcrouter::McrouterClient::Pointer client_;
  facebook::memcache::mcrouter::CarbonRouterInstance<typename If::RouterInfo>*
      router_;
  std::shared_ptr<facebook::memcache::mcrouter::CarbonRouterInstance<
      typename If::RouterInfo>>
      transientRouter_;
  RecreateFunc recreateFunc_;

  void init(
      facebook::memcache::mcrouter::CarbonRouterInstance<
          typename If::RouterInfo>* router,
      const Options& options) {
    router_ = router;
    if (!router_) {
      throw std::runtime_error("Failed to initialize router");
    }
    try {
      client_ = router_->createClient(
          options.maxOutstanding, options.maxOutstandingError);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error(
          std::string("Failed to initialize router client: ") + e.what());
    }
  }
};
} // carbon