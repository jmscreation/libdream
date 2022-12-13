#pragma once

#include "dream_client_object.h"
#include "ip_tools.h"

#include <map>
#include <string>
#include <atomic>
#include <mutex>

namespace dream {

struct ServerHeader {
    std::string name, description;
    float version;

    template<typename Archive>
    void serialize(Archive& ar) {
        ar(version, name, description);
    }
};

class Server {
    asio::io_context ctx;
    asio::io_context::work idle;
    asio::ip::tcp::acceptor listener;

    ServerHeader header;
    uint64_t cur_uuid;
    std::map<uint64_t, std::unique_ptr<ClientObject>> clients;

    std::thread ctx_handle, runtime_handle;
    std::atomic_bool runtime_running;

    Clock ping_timeout;

    void reset_listener();
    void start_context_handle();

    void start_runtime();
    void stop_runtime();

    // server runtime - check clients and validate the session
    std::mutex runtime_lock; // runtime mutex
    void server_runtime();

    // asynchronous callbacks
    bool validate_socket(asio::ip::tcp::socket& soc); // blocking - determines that connecting socket is valid

    void new_client_socket(asio::ip::tcp::socket&& soc);

    // asynchronous loop backs
    std::mutex accept_lock; // acceptor mutex

    void do_accept();

public:
    Server();
    ~Server();

    bool start_server(short port, const std::string& ip = "");
    void stop_server();
};


}