#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <functional>
#include <memory>
#include <functional>
#include <thread>
#include "../include/exchange_utils.h"
#include "../include/log.h"

using json = nlohmann::json;

class Exchange {
  public:
    Exchange(std::string name, std::shared_ptr<Log> log, std::shared_ptr<Config> config) :
      name(name),
      reconnect(false),
      config(config),
      log(log) { }

    virtual void start() = 0;
    virtual void subscribe_to_ticker() = 0;
    virtual void subscribe_to_OHLC(std::chrono::minutes) = 0;
    virtual void market_buy(double) = 0;
    virtual void market_sell(double) = 0;
    virtual void limit_buy(double, double) = 0;
    virtual void limit_sell(double, double) = 0;
    virtual void cancel_order(std::string) = 0;
    virtual void orderinfo(std::string) = 0;
    struct UserInfo { double asset_net = 0; double free_btc = 0; double free_cny = 0; double borrow_btc = 0; double borrow_cny = 0; };
    virtual void userinfo() = 0;
    struct BorrowInfo { std::string id = ""; double amount = 0; double rate = 0; };
    virtual BorrowInfo borrow(Currency, double = 1) = 0;
    virtual double close_borrow(Currency) = 0;

    virtual void ping() = 0;
    virtual void backfill_OHLC(std::chrono::minutes, int) = 0;
    virtual std::string status() = 0;

    void set_ticker_callback(std::function<void(long, double, double, double)> callback) {
      ticker_callback = callback;
    }
    void set_OHLC_callback(std::function<void(std::chrono::minutes, long, double, double, double, double, double, bool)> callback) {
      OHLC_callback = callback;
    }
    void set_open_callback(std::function<void()> callback) {
      open_callback = callback;
    }
    void set_userinfo_callback(std::function<void(UserInfo)> callback) {
      userinfo_callback = callback;
    }
    void set_trade_callback(std::function<void(std::string)> callback) {
      trade_callback = callback;
    }
    void set_orderinfo_callback(std::function<void(OrderInfo)> callback) {
      orderinfo_callback = callback;
    }
    void set_filled_callback(std::function<void(double)> callback) {
      filled_callback = callback;
    }
    void set_pong_callback(std::function<void()> callback) {
      pong_callback = callback;
    }

    std::string name;

    // timestamp representing time of last received message
    std::chrono::nanoseconds ts_since_last;

    // when true, letting handler know to make a new Exchange object
    bool reconnect;
  protected:
    std::shared_ptr<Config> config;
    std::shared_ptr<Log> log;

    std::function<void(long, double, double, double)> ticker_callback;
    std::function<void(std::chrono::minutes, long, double, double, double, double, double, bool)> OHLC_callback;
    std::function<void()> open_callback;
    std::function<void(UserInfo)> userinfo_callback;
    std::function<void(std::string)> trade_callback;
    std::function<void(OrderInfo)> orderinfo_callback;
    std::function<void(double)> filled_callback;
    std::function<void()> pong_callback;
};
