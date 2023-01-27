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

    dlog << "server started\n";

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

void Server::broadcast_string(const std::string& data) {
    std::scoped_lock lock(accept_lock);

    for(auto& [id,client] : clients){
        client->send_command(Command(Command::STRING, data));
    }
}

std::vector<User> Server::get_client_list() {
    std::vector<User> list;

    std::scoped_lock lock(accept_lock);

    for(auto& [id, client] : clients){
        User& user = list.emplace_back(this);
        user.uuid = id;
        user.name = client->get_name();
    }

    return list;
}

// Callbacks

void Server::new_client_socket(asio::ip::tcp::socket&& soc) {
    std::scoped_lock lock(accept_lock);
    while(clients.count(cur_uuid)) ++cur_uuid; // find a free uuid

    dlog << "new client [" << cur_uuid << "]\n";
    auto& c = clients.insert_or_assign(
                                cur_uuid,
                                generate_client_object(std::move(soc), cur_uuid, "NoName")
                            ).first->second;

    // register the on_authorized callback
    c->register_hook("on_authorized", [this](ClientObject& client, const std::any& data){
        if(on_client_join){
            User user(this);
            user.uuid = client.get_id();
            user.name = client.get_name();

            std::scoped_lock lock(runtime_lock);
            on_client_join(user);
        }
    });

    c->register_hook("pre_command", [this](ClientObject& client, const std::any& data){
        const Command& cmd = std::any_cast<const Command&>(data);

        if(cmd.type == Command::RESPONSE){
            client.send_command(Command::RESPONSE);
        }
    });
}


void Server::server_runtime() { // check for and remove invalid clients
    std::scoped_lock lock(accept_lock);

    for(auto it = clients.begin(); it != clients.end(); ++it){
        auto& [id, client] = *it;

        if(!client->is_valid()){
            dlog << client->get_name() << " disconnected\n";
            expired_clients.emplace_back(std::move(client)); // move expired client to gc
            it = clients.erase(it); // remove and continue
            if(it == clients.end()) break;
            --it;
            continue;

        } else if(!client->is_authorized()) {
            client->server_authorize();

        } else {
            client->runtime_update();
        }
    }

    if(ping_timeout.getSeconds() > 10){
        for(auto& [id, client] : clients){
            if(!client->is_authorized()) continue;

            client->send_command(Command(Command::PING));
        }
        
        std::erase_if(expired_clients, [](const auto& c){ return !c->has_weak_references(); }); // expired client cleanup

        ping_timeout.restart();
    }
}

// Async Loopbacks

void Server::do_accept() {
    if(!listener.is_open()) return;

    listener.async_accept([this](std::error_code er, asio::ip::tcp::socket soc){
        if(!listener.is_open()) return; // listener shutdown

        if(soc.is_open()){
            const auto& ep = soc.remote_endpoint();
            dlog << "connection from " << ep.address().to_string() << " : " << ep.port() << "\n";
            soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>(5000)); // 5 second write timeout
            new_client_socket(std::move(soc));
        } else {
            dlog << "error accepting connection\n";
        }

        do_accept();
    });
}

// Misc

std::unique_ptr<ClientObject> Server::generate_client_object(asio::ip::tcp::socket&& soc, uint64_t id, const std::string& name) {
    return std::unique_ptr<ClientObject>( new ClientObject(ctx, std::move(soc), cur_uuid, std::to_string(cur_uuid)) );
}




}