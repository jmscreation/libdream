#pragma once

#include "dream_connection.h"
#include "dream_socket.h"
#include "dream_block.h"
#include "ip_tools.h"

#include <map>
#include <string>
#include <atomic>
#include <functional>
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
    std::map<uint64_t, std::unique_ptr<Socket>> clients;
    std::vector<std::unique_ptr<Socket>> expired_clients;

    Block blobdata;

    std::thread ctx_handle, runtime_handle;
    std::atomic_bool runtime_running;

    Clock ping_timeout;

    void reset_listener();
    void start_context_handle();

    void start_runtime();
    void stop_runtime();

    std::unique_ptr<Socket> generate_socket(asio::ip::tcp::socket&& soc, uint64_t id, const std::string& name);

    // server runtime - check clients and validate the session
    std::shared_mutex runtime_lock; // runtime mutex
    void server_runtime();

    // asynchronous callbacks
    void new_client_socket(asio::ip::tcp::socket&& soc);

    // asynchronous loop backs

    void do_accept();

public:
    Server();
    virtual ~Server();

    bool start_server(short port, const std::string& ip = "");
    void stop_server();

    bool is_running() { return runtime_running; }

    Block& get_block() { return blobdata; }

    size_t get_client_count() { std::shared_lock lock(runtime_lock); return clients.size(); }

    std::vector<Connection> get_client_list();

    void broadcast_string(const std::string& data);

    std::function<void(Connection&)> on_client_join; // this is temporary just so we can quickly get a callback

    friend class Connection;
};


}