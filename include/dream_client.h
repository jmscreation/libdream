#pragma once
#include "dream_server.h"

namespace dream {


class Client {
    asio::io_context ctx;
    asio::io_context::work idle;
    
    ServerHeader header;
    uint64_t cur_uuid;
    std::unique_ptr<Socket> server;

    Block blobdata;

    std::thread ctx_handle, runtime_handle;
    std::atomic_bool runtime_running;

    Clock ping_timeout;

    void start_context_handle();

    void start_runtime();
    void stop_runtime();

    std::unique_ptr<Socket> generate_server_object(asio::ip::tcp::socket&& soc, uint64_t id, const std::string& name);

    // client runtime
    std::recursive_mutex runtime_lock; // runtime mutex
    void client_runtime();

public:
    Client();
    virtual ~Client();

    bool start_client(short port, const std::string& ip = "", const std::string& name = "NoName");
    void stop_client();

    bool is_running() { return runtime_running; }
    bool is_connected() { return server && server->is_valid() && server->is_authorized(); }

    Block& get_block() { return blobdata; }

    Connection get_socket();

    void send_string(const std::string& data);

    std::function<void(Connection&)> on_connect; // this is temporary just so we can quickly get a callback

    friend class Connection;
};



    
}