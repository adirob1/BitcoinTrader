#pragma once

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

#include <iostream>
#include <string>

typedef websocketpp::client<websocketpp::config::asio_tls_client> wspp_client;
typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> wspp_context_ptr;

class websocket {
  public:
    websocket(std::string);
    ~websocket();

    void setup();
    void teardown();

    void connect();

    void send(std::string);
    void close(websocketpp::close::status::value, std::string);

    void set_open_callback(std::function<void()> callback) { open_callback = callback; };
    void set_message_callback(std::function<void(std::string)> callback) { message_callback = callback; };
    void set_error_callback(std::function<void(std::string)> callback) { error_callback = callback; };
    void set_close_callback(std::function<void()> callback) { close_callback = callback; };
    void set_fail_callback(std::function<void()> callback) { fail_callback = callback; };

    // getters
    std::string get_status() const { return status; };
    std::string get_uri() const { return uri; };
    std::string get_error_reason() const { return error_reason; };
    int get_close_code() const { return close_code; };

  private:
    void on_open(wspp_client *, websocketpp::connection_hdl);
    wspp_context_ptr on_tls_init(wspp_client *, websocketpp::connection_hdl);
    void on_message(websocketpp::connection_hdl, wspp_client::message_ptr);
    void on_close(wspp_client *, websocketpp::connection_hdl);
    void on_fail(wspp_client *, websocketpp::connection_hdl);
    
    // callbacks
    std::function<void()> open_callback;
    std::function<void(std::string)> message_callback;
    std::function<void(std::string)> error_callback;
    std::function<void()> close_callback;
    std::function<void()> fail_callback;

    wspp_client endpoint;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> thread;
    websocketpp::connection_hdl hdl;
    std::string status;
    std::string server;
    std::string uri;
    std::string error_reason;
    int close_code;
};
