#pragma once
#include "dream_server.h"

namespace dream {


class Client {
    asio::io_context ctx;
    asio::io_context::work idle;
    
    ServerHeader header;
    uint64_t cur_uuid;
    std::unique_ptr<ClientObject> server;

    Block blobdata;

    std::thread ctx_handle, runtime_handle;
    std::atomic_bool runtime_running;

    Clock ping_timeout;

    void start_context_handle();

    void start_runtime();
    void stop_runtime();

    // client runtime
    std::mutex runtime_lock; // runtime mutex
    void client_runtime();

    // asynchronous callbacks

    // asynchronous loop backs

public:
    Client();
    virtual ~Client();

    bool start_client(short port, const std::string& ip = "", const std::string& name = "NoName");
    void stop_client();

    Block& get_block() { return blobdata; }
};



    
}