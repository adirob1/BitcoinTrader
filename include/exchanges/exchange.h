#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <functional>
#include <memory>
#include <functional>
#include <thread>
#include <boost/optional.hpp>
#include "utilities/exchange_utils.h"
#include "utilities/log.h"

using json = nlohmann::json;

class Exchange {
public:
  Exchange(std::string name, std::shared_ptr<Log> log, std::shared_ptr<Config> config) :
      name(name),
      config(config),
      log(log) { }

  virtual void start() = 0;
  virtual void subscribe_to_ticker() = 0;
  virtual void subscribe_to_depth() = 0;
  virtual void subscribe_to_OHLC(std::chrono::minutes) = 0;
  virtual bool subscribed_to_OHLC(std::chrono::minutes) = 0;
  virtual void userinfo(timestamp_t) = 0;
  virtual void ping() = 0;
  virtual bool backfill_OHLC(std::chrono::minutes, unsigned long) = 0;
  virtual std::string status() = 0;
  virtual bool connected() = 0;

  void set_ticker_callback(std::function<void(const Ticker&)> callback) {
    ticker_callback = callback;
  }
  void set_depth_callback(std::function<void(const Depth&)> callback) {
    depth_callback = callback;
  }
  void set_OHLC_callback(std::function<void(std::chrono::minutes, const OHLC&)> callback) {
    OHLC_callback = callback;
  }
  void set_open_callback(std::function<void()> callback) {
    open_callback = callback;
  }
  void set_trade_callback(std::function<void(const std::string&)> callback) {
    trade_callback = callback;
  }
  void set_filled_callback(std::function<void(double)> callback) {
    filled_callback = callback;
  }
  void set_pong_callback(std::function<void()> callback) {
    pong_callback = callback;
  }

  std::string name;

  // timestamp representing time of last received message
  Atomic<timestamp_t> ts_since_last;
protected:
  std::shared_ptr<Config> config;
  std::shared_ptr<Log> log;

  std::function<void(const Ticker&)> ticker_callback;
  std::function<void(const Depth&)> depth_callback;
  std::function<void(std::chrono::minutes, const OHLC&)> OHLC_callback;
  std::function<void()> open_callback;
  std::function<void(const std::string&)> trade_callback;
  std::function<void(double)> filled_callback;
  std::function<void()> pong_callback;

  // stores the time that the channel message callback will be invalid and no longer called
  std::map<std::string, timestamp_t> channel_timeouts;
  std::mutex channel_timeouts_lock;
};
