#include "../include/exchange_handler.h"

using std::cout;                   using std::endl;
using std::cin;                    using std::remove;
using std::function;               using std::shared_ptr;
using std::string;                 using std::ofstream;
using std::this_thread::sleep_for; using std::floor;
using std::mutex;                  using std::vector;
using std::lock_guard;             using std::thread;
using std::vector;                 using std::map;
using std::chrono::minutes;        using std::bind;
using std::to_string;              using std::ostringstream;
using std::make_shared;            using std::make_unique;
using namespace std::placeholders; using std::to_string;
using namespace std::chrono_literals;
using std::accumulate;

// REQUIRED TO EXPLICITLY ADD EACH EXCHANGE HERE
vector<shared_ptr<ExchangeMeta>> BitcoinTrader::exchange_metas() {
  return { okcoin_futs.meta, okcoin_spot.meta };
}

BitcoinTrader::BitcoinTrader(shared_ptr<Config> config) :
  okcoin_futs("OKCoinFuts",
      make_shared<Log>((*config)["okcoin_futs_log"], config)),
  okcoin_spot("OKCoinSpot",
      make_shared<Log>((*config)["okcoin_spot_log"], config)),
  config(config),
  trading_log(new Log((*config)["trading_log"], config)),
  done(false)
{
  // each exchange must have its set_up_and_start set
  // which creates the exchange, sets up its callbacks
  // and starts the exchange
  // this will also be used to restart the exchange
  okcoin_futs.meta->set_up_and_start = [&]() {
    okcoin_futs.reset();
    // create the OKCoinFuts exchange
    okcoin_futs.exchange = make_shared<OKCoinFuts>("OKCoinFuts", OKCoinFuts::Weekly, okcoin_futs.meta->log, config);
    // store the generic pointer
    okcoin_futs.meta->exchange = okcoin_futs.exchange;

    // set the callbacks the exchange will use
    auto open_callback = [&]() {
      // start receiving ticks and block until one is received
      okcoin_futs.exchange->subscribe_to_ticker();
      check_until([&]() { return okcoin_futs.meta->tick.has_been_set(); });

      // backfill and subscribe to each market data
      for (auto &m : okcoin_futs.meta->mktdata) {
        okcoin_futs.exchange->backfill_OHLC(m.second->period, m.second->bars->capacity());
        okcoin_futs.exchange->subscribe_to_OHLC(m.second->period);
      }
    };
    okcoin_futs.exchange->set_open_callback(open_callback);

    auto OHLC_callback = [&](minutes period, OHLC bar) {
      okcoin_futs.meta->mktdata[period]->add(bar);
    };
    okcoin_futs.exchange->set_OHLC_callback(OHLC_callback);

    auto ticker_callback = [&](const Ticker new_tick) {
      for (auto &m : okcoin_futs.meta->mktdata) {
        m.second->add(new_tick);
      }
      okcoin_futs.meta->tick.set(new_tick);
    };
    okcoin_futs.exchange->set_ticker_callback(ticker_callback);

    auto userinfo_callback = [&](OKCoinFuts::UserInfo info) {
      okcoin_futs.user_info.set(info);
    };
    okcoin_futs.exchange->set_userinfo_callback(userinfo_callback);

    // start the exchange
    okcoin_futs.exchange->start();
  };
  okcoin_futs.meta->print_userinfo = [&]() {
    return okcoin_futs.user_info.get().to_string();
  };

  okcoin_spot.meta->set_up_and_start = [&]() {
    okcoin_spot.reset();
    okcoin_spot.exchange = make_shared<OKCoinSpot>("OKCoinSpot", okcoin_spot.meta->log, config);
    okcoin_spot.meta->exchange = okcoin_spot.exchange;

    auto open_callback = [&]() {
      okcoin_spot.exchange->subscribe_to_ticker();
    };
    okcoin_spot.exchange->set_open_callback(open_callback);

    auto userinfo_callback = [&](OKCoinSpot::UserInfo info) {
      okcoin_spot.user_info.set(info);
    };
    okcoin_spot.exchange->set_userinfo_callback(userinfo_callback);

    auto ticker_callback = [&](const Ticker new_tick) {
      okcoin_spot.meta->tick.set(new_tick);
    };
    okcoin_spot.exchange->set_ticker_callback(ticker_callback);

    okcoin_spot.exchange->start();
  };
  okcoin_spot.meta->print_userinfo = [&]() {
    return okcoin_spot.user_info.get().to_string();
  };

  // create the strategies
  strategies.push_back(make_shared<SMACrossover>("SMACrossover"));

  // we're using OKCoin Futs for our basket of strategies
  for (auto strategy : strategies) {
      // if we do not have a mktdata object for this period
      if (okcoin_futs.meta->mktdata.count(strategy->period) == 0) {
        // create a mktdata object with the period the strategy uses
        okcoin_futs.meta->mktdata[strategy->period] = make_shared<MktData>(strategy->period);
      }
      // tell the mktdata object about the strategy
      okcoin_futs.meta->mktdata[strategy->period]->add_strategy(strategy);
  }
}

BitcoinTrader::~BitcoinTrader() {
  done = true;
  for (auto t : running_threads)
    if (t && t->joinable())
      t->join();
}

string BitcoinTrader::status() {
  ostringstream os;
  for (auto exchange_meta : exchange_metas()) {
    os << exchange_meta->exchange->status();
    Ticker tick = exchange_meta->tick.get();
    os << "bid: " << tick.bid << ", ask: " << tick.ask << std::endl;
    os << exchange_meta->print_userinfo();
    for (auto m : exchange_meta->mktdata) {
      os << "MktData with period: " <<  m.second->period.count();
      os << ", size: " << m.second->bars->size() << endl;
      os << "last: " << m.second->bars->back().to_string() << endl;
    }
  }
  return os.str();
}

void BitcoinTrader::start() {
  for (auto exchange : exchange_metas())
    exchange->set_up_and_start();

  // check connection and reconnect if down on another thread
  check_connection();

  // start fetching userinfo on another thread
  fetch_userinfo();

  // wait until OKCoinFuts userinfo and ticks are fetched,
  // and subscribed to market data
  auto can_open_positions = [&]() {
    // we can open positions if
    bool can = true;
    // we're subscribed to every OHLC period
    for (auto &m : okcoin_futs.meta->mktdata) {
      can = can && okcoin_futs.exchange->subscribed_to_OHLC(m.first);
    }
    // and every strategy has a signal that's been set
    can = can && accumulate(strategies.begin(), strategies.end(), true,
                            [](bool a, shared_ptr<Strategy> b) { return a && b->signal.has_been_set(); });
    // and we have userinfo and a tick set
    can = can &&
        okcoin_futs.user_info.has_been_set() &&
        okcoin_futs.meta->tick.has_been_set();
    return can;
  };
  check_until(can_open_positions);

  // manage positions on another thread
  position_management();
}

void BitcoinTrader::position_management() {
  auto position_thread = [&]() {
    while (!done && okcoin_futs.exchange->connected()) {
      double blended_signal = blend_signals();
      manage_positions(blended_signal);
      sleep_for(5s);
    }
  };
  running_threads.push_back(make_shared<thread>(position_thread));
}

void BitcoinTrader::check_connection() {
  auto connection_thread = [&]() {
    bool warm_up = true;
    while (!done) {
      // give some time for everything to start up after we reconnect
      if (warm_up) {
        sleep_for(10s);
        warm_up = false;
      }
      for (auto exchange : exchange_metas()) {
        if (exchange->exchange &&
            // if the time since the last message received is > 1min
            (((timestamp_now() - exchange->exchange->ts_since_last) > 1min) ||
             // if the websocket has closed
             !exchange->exchange->connected())) {
          exchange->log->output("RECONNECTING TO " + exchange->name);
          exchange->set_up_and_start();
          warm_up = true;
        }
      }
      // check to reconnect every second
      sleep_for(1s);
    }
  };
  running_threads.push_back(make_shared<thread>(connection_thread));
}

void BitcoinTrader::fetch_userinfo() {
  auto userinfo_thread = [&]() {
    while (!done) {
      for (auto exchange : exchange_metas()) {
        if (exchange->exchange->connected())
          exchange->exchange->userinfo();
      }
      sleep_for(1s);
    }
  };
  running_threads.push_back(make_shared<thread>(userinfo_thread));
}
