#pragma once

#include <string>
#include <chrono>
#include <functional>
#include <thread>

#include <curl/curl.h>
#include <json.hpp>


enum Currency { BTC, CNY };

class Channel {
  public:
    Channel(std::string name, std::string status) :
      name(name),
      status(status) { }

    void subscribe();

    std::string name;
    std::string status;
    std::string last_message;
};

class OrderInfo {
  public:
    OrderInfo(double amount,
              double avg_price,
              std::string create_date,
              double filled_amount,
              std::string order_id,
              double price,
              std::string status,
              std::string symbol,
              std::string type) :
      amount(amount), avg_price(avg_price), create_date(create_date),
      filled_amount(filled_amount), order_id(order_id), price(price),
      status(status), symbol(symbol), type(type) { }
    OrderInfo() :
      amount(0), avg_price(0), create_date(0),
      filled_amount(0), order_id(0), price(0),
      status(""), symbol(""), type("") { }

    double amount;
    double avg_price;
    std::string create_date;
    double filled_amount;
    std::string order_id;
    double price;
    std::string status;
    std::string symbol;
    std::string type;

    std::string to_string() {
      std::ostringstream os;
      os << "amount: " << amount << ", avg_price: " << avg_price
        << ", create_date: " << create_date << ", filled_amount: "
        << filled_amount << ", order_id: " << order_id
        << ", price: " << ", status: " << status << ", symbol: "
        << symbol << ", type: " << type;
      return os.str();
    }
};

long optionally_to_long(nlohmann::json);
double optionally_to_double(nlohmann::json);
int optionally_to_int(nlohmann::json);
template <typename T> std::string opt_to_string(nlohmann::json);

double truncate_to(double, int);

std::string dtos(double, int);

std::chrono::nanoseconds timestamp_now();

size_t Curl_write_callback(void *contents, size_t size, size_t nmemb, std::string *s);

std::string curl_post(std::string, std::string = "");

bool wait_until(std::function<bool()>, std::chrono::milliseconds test_time, std::chrono::milliseconds time_between_checks = std::chrono::milliseconds(50));
