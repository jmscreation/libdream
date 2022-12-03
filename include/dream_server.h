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
    asio::ip::tcp::acceptor listener;

    ServerHeader header;
    uint64_t cur_uuid;
    std::map<uint64_t, ClientObject> clients;

    std::thread ctx_handle;

    void reset_listener();
    void start_context_handle();

    // asynchronous callbacks
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