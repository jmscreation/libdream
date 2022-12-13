#pragma once

#include "ip_tools.h"
#include "lib_cereal.h"
#include "dream_command.h"
#include "dream_clock.h"

#include <string>
#include <atomic>
#include <list>

namespace dream {

class ClientObject {
    asio::io_context& ctx;
    asio::ip::tcp::socket socket;

    uint64_t id;
    std::string name;

    std::atomic_bool server_authorized, authorizing, valid;
    char authbuf[16] {}; // small buffer for authorization

    std::mutex outgoing_command_lock, shutdown_lock;
    std::list<std::stringstream> out_data; // buffers for outgoing data

public:
    ClientObject(asio::io_context& ctx, asio::ip::tcp::socket&& soc, uint64_t id, std::string name):
        ctx(ctx), socket(std::move(soc)), id(id), name(name),
        server_authorized(false), authorizing(false), valid(true) {}

    ~ClientObject();

    ClientObject(const ClientObject&) = delete;
    ClientObject& operator=(const ClientObject&) = delete;

    void server_authorize(); // begin authorize process for server - asynchronous
    void client_authorize(); // begin authorize process for client - asynchronous

    void runtime_update(); // misc blocking update loop
    bool send_raw_data(const char* data, size_t length, std::function<void(bool)> on_complete=[](bool){});
    void send_command(const Command& cmd); // send command to socket

    void shutdown(); // a safe way to shutdown the socket
    bool is_valid();
    bool is_authorized();
    size_t get_id() { return id; }
    std::string get_name() { return name; }

private:
    bool incoming_data_handle();
    bool outgoing_data_handle();

    void process_command(Command& cmd);

public:
    template<typename Archive>
    void serialize(Archive& ar) {
        ar(id, name);
    }
};


}
