#include "bitcointrader.h"

using std::shared_ptr;
using std::accumulate;
using std::abs;
using std::to_string;
using std::string;
using std::chrono::seconds;
using namespace std::chrono_literals;
using std::this_thread::sleep_for;
using std::function;
using std::ostringstream;

bool BitcoinTrader::futs_market(OKCoinFuts::OrderType type, double amount, int lever_rate, seconds timeout = 30s) {
  auto tick = okcoin_futs_h->tick.get();

  string action;
  string direction;
  double price;

  switch (type) {
    case OKCoinFuts::OpenLong :
      action = "OPENING"; direction = "LONG";
      price = tick.bid * (1 + 0.01);
      break;
    case OKCoinFuts::OpenShort :
      action = "OPENING"; direction = "SHORT";
      price = tick.ask * (1 - 0.01);
      break;
    case OKCoinFuts::CloseLong :
      action = "CLOSING"; direction = "LONG";
      price = tick.ask * (1 - 0.01);
      break;
    case OKCoinFuts::CloseShort :
      action = "CLOSING"; direction = "SHORT";
      price = tick.bid * (1 + 0.01);
      break;
    default :
      okcoin_futs_h->log->output("type NOT RECOGNIZED IN CALL TO futs_market");
      return false;
  }

  trading_log->output("MARKET " + action + " " + to_string(amount) + " " + direction + " CONTRACTS WITH MAX PRICE " + to_string(price));

  bool trading_done = false;
  auto cancel_time = timestamp_now() + timeout;
  auto trade_callback = [&](const string& order_id) {
    if (order_id != "failed") {
      bool done_limit_check = false;
      // until we are done,
      // we fully filled for the entire amount
      // or some seconds have passed
      OKCoinFuts::OrderInfo final_info;
      while (!done && !done_limit_check &&
             timestamp_now() < cancel_time) {
        // fetch the orderinfo every second
        okcoin_futs_h->okcoin_futs->set_orderinfo_callback(
            [&](const OKCoinFuts::OrderInfo& orderinfo) {
              if (orderinfo.status != OKCoin::OrderStatus::Failed) {
                final_info = orderinfo;
                // early stopping if we fill for the entire amount
                if (orderinfo.status == OKCoin::OrderStatus::FullyFilled)
                  done_limit_check = true;
              }
              else // early stop if order status is failed (this shouldn't be called)
                done_limit_check = true;
            }
        );
        okcoin_futs_h->okcoin_futs->orderinfo(order_id, cancel_time);
        sleep_for(seconds(1));
      }

      // given the limit enough time, cancel it
      if (final_info.status != OKCoin::OrderStatus::FullyFilled)
        okcoin_futs_h->okcoin_futs->cancel_order(order_id, cancel_time);

      if (final_info.filled_amount != 0) {
        trading_log->output(
            "FILLED FOR " + to_string(final_info.filled_amount) + " BTC @ $" + to_string(final_info.avg_price));
      }
      else
        trading_log->output("NOT FILLED IN TIME");
    }
    trading_done = true;
  };
  okcoin_futs_h->okcoin_futs->set_trade_callback(trade_callback);

  okcoin_futs_h->okcoin_futs->order(type, amount, price, lever_rate, false, cancel_time);

  // check until the trade callback is finished, or cancel_time
  return check_until([&]() { return trading_done; }, cancel_time);
}

double BitcoinTrader::blend_signals() {
  // average the signals
  double signal_sum = accumulate(strategies.begin(), strategies.end(), 0,
                                 [](double a, shared_ptr<Strategy> b) { return a + b->signal.get(); });
  return signal_sum / strategies.size();
}

bool BitcoinTrader::futs_userinfo(OKCoinFuts::UserInfo& userinfo) {
  // Fetch the current OKCoin Futs account information (to get the equity)
  // If we do not get a response in time, restart this function
  auto cancel_time = timestamp_now() + 10s;
  Atomic<OKCoinFuts::UserInfo> userinfo_a;
  okcoin_futs_h->okcoin_futs->set_userinfo_callback([&userinfo_a](const OKCoinFuts::UserInfo& new_userinfo) {
    userinfo_a.set(new_userinfo);
  });
  okcoin_futs_h->okcoin_futs->userinfo(cancel_time);
  if (check_until([&userinfo_a]() { return userinfo_a.has_been_set(); }, cancel_time)) {
    userinfo = userinfo_a.get();
    return true;
  }
  else
    return false;
}

void BitcoinTrader::manage_positions(double signal) {
  // fetch the current position
  // if something is wrong, restart this function
  auto position = okcoin_futs_h->okcoin_futs->positions();
  if (!position.valid)
    return;

  // fetch userinfo, if we cannot just return
  OKCoinFuts::UserInfo userinfo;
  if (!futs_userinfo(userinfo))
    return;

  // fetch tick, return if it's stale
  auto tick = okcoin_futs_h->tick.get();
  if (timestamp_now() - tick.timestamp > 30s)
    return;

  // calculate the number of contracts we have open, and the number of contracts we would like to have
  // negative contracts are short contracts
  double equity = userinfo.equity * tick.last;
  double max_exposure = position.lever_rate * equity;
  double desired_exposure = signal * max_exposure;;
  int desired_contracts = static_cast<int>(floor(desired_exposure / 100));

  // TODO: remove for live
  if (desired_contracts > 0)
    desired_contracts = 1;
  else if (desired_contracts == 0)
    desired_contracts = 0;
  else
    desired_contracts = -1;

  int current_contracts = position.buy.contracts - position.sell.contracts;

  // number of contracts we have to close and/or open
  int contracts_to_close;
  int contracts_to_open;

  // if the signs are different or if we desire 0 contracts
  // we're closing all the current contracts, and opening
  // all of desired contracts (or 0 if it's 0)
  if ((current_contracts > 0 && desired_contracts <= 0) ||
      (current_contracts < 0 && desired_contracts >= 0)) {
    contracts_to_close = current_contracts;
    contracts_to_open = desired_contracts;
  }
    // if the signs are the same
    // we have to figure out if we're increasing or decreasing our exposure
  else {
    // if we are decreasing our exposure, close the difference
    if (abs(desired_contracts) < abs(current_contracts)) {
      contracts_to_close = current_contracts - desired_contracts;
      contracts_to_open = 0;
    }
      // if we're increasing our exposure, open the difference
    else {
      contracts_to_open = desired_contracts - current_contracts;
      contracts_to_close = 0;
    }
  }

  // if we have any contracts to close, convert the contracts to long or short contracts instead of negative/positive
  if (contracts_to_close != 0) {
    auto to_close = (contracts_to_close >= 0) ? OKCoinFuts::OrderType::CloseLong : OKCoinFuts::OrderType::CloseShort;
    if (!futs_market(to_close, abs(contracts_to_close), position.lever_rate)) {
      // if we've failed to close, discontinue
      return;
    }
  }

  // if we have any contracts to open, convert the contracts to long or short contracts instead of negative/positive
  if (contracts_to_open != 0) {
    auto to_close = (contracts_to_open >= 0) ? OKCoinFuts::OrderType::OpenLong : OKCoinFuts::OrderType::OpenShort;
    // no need to check for success, since it's the last thing we do
    // if it fails, manage positions loops again
    futs_market(to_close, abs(contracts_to_open), position.lever_rate);
  }
}
