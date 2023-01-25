#include "libdream.h"
#include <algorithm>
#include <iomanip>
namespace dream {

Server::Server(): idle(ctx), listener(ctx), header({}), cur_uuid(1), runtime_running(false) {}

Server::~Server() {
    stop_server();
}

void Server::start_context_handle() {
    ctx_handle = std::thread([this](){
        ctx.run();
    });
}

void Server::reset_listener() {
    std::scoped_lock lock(accept_lock);
    if(listener.is_open()){
        listener.cancel();
        listener.close();
    }
}

void Server::start_runtime() {
    if(!runtime_running){
        runtime_handle = std::thread([this](){
            runtime_running = true;
            while(runtime_running){
                Clock::sleepMilliseconds(2); // server runtime has 2ms delay
                {
                    std::scoped_lock lock(runtime_lock);
                    server_runtime();
                }
            }
        });
    }
}

void Server::stop_runtime() {
    runtime_running = false;
    blobdata.clear();
    if(runtime_handle.joinable()){
        runtime_handle.join();
    }
}

bool Server::start_server(short port, const std::string& ip) {
    reset_listener();
    stop_runtime();

    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);

    try {
        if(ip.size()){
            asio::ip::address addr;
            if(!stoip(ip, addr)) return false; // invalid address
            endpoint = asio::ip::tcp::endpoint(addr, port);
        }

        listener.open(endpoint.protocol());
        listener.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        listener.bind(endpoint);
        listener.listen();

        do_accept();

    } catch(...) {
        return false;
    }

    start_context_handle();
    start_runtime();

    std::cout << "server started\n";

    return true;
}

void Server::stop_server() {
    ctx.stop(); // first send stop signal to io context

    stop_runtime();
    reset_listener();
    clients.clear(); // close all clients

    while(!ctx.stopped());
    ctx.reset();
    if(ctx_handle.joinable()){
        ctx_handle.join(); // close context handle
    }
}


// Callbacks

void Server::new_client_socket(asio::ip::tcp::socket&& soc) {
    std::scoped_lock lock(accept_lock);
    while(clients.count(cur_uuid)) ++cur_uuid; // find a free uuid

    std::cout << "new client [" << cur_uuid << "]\n";
    auto& c = clients.insert_or_assign(
                                cur_uuid,
                                std::make_unique<ClientObject>(ctx, std::move(soc), cur_uuid, std::to_string(cur_uuid))
                            ).first->second;
    // TEST and DEBUG
    static Clock tc;
    static size_t msgs = 0;

    c->register_global_hook([](ClientObject& client, const std::string& hook, const std::any& data) -> bool {
        if(hook == "pre_command"){
            const Command& cmd = std::any_cast<Command>(data);
            if(cmd.type == Command::STRING){
                msgs++;
                std::cout << "                 \r" << (double(msgs) / tc.getSeconds()) << " packages per second";
            }

            if(cmd.type == Command::RESPONSE){
                msgs = 0;
                tc.restart();
            }
        }

        return true;
    });
}


void Server::server_runtime() { // check for and remove invalid clients
    std::scoped_lock lock(accept_lock);

    for(auto& [id, client] : clients){
        if(!client) continue;
        if(!client->is_valid()){
            std::cout << client->get_name() << " disconnected\n";
            client.reset();
        } else if(!client->is_authorized()) {
            client->server_authorize();
        } else {
            client->runtime_update();
        }
    }

    if(ping_timeout.getSeconds() > 10){
        for(auto it = clients.begin(); it != clients.end(); ++it){
            if(!it->second.get()){
                clients.erase(it);
                it = clients.begin(); // reset iterator - less efficient, but simple
                if(it == clients.end()) break;
            }
        }
        for(auto& [id, client] : clients){
            if(!client->is_valid() || !client->is_authorized()) continue;

            client->send_command(Command(Command::PING));
        }
        ping_timeout.restart();
    }

    std::erase_if(clients, [](const auto& p) -> bool { return !p.second; }); // remove nullptr from clients
}

// Async Loopbacks

void Server::do_accept() {
    if(!listener.is_open()) return;

    listener.async_accept([this](std::error_code er, asio::ip::tcp::socket soc){
        if(!listener.is_open()) return; // listener shutdown

        if(soc.is_open()){
            const auto& ep = soc.remote_endpoint();
            std::cout << "connection from " << ep.address().to_string() << " : " << ep.port() << "\n";
            soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>(5000)); // 5 second write timeout
            new_client_socket(std::move(soc));
        } else {
            std::cout << "error accepting connection\n";
        }

        do_accept();
    });
}




}