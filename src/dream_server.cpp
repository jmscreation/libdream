#include "libdream.h"

namespace dream {

Server::Server(): listener(ctx), header({}), cur_uuid(1) {}

Server::~Server() {}

void Server::start_context_handle() {
    ctx_handle = std::thread([this](){
        ctx.run();
    });
}

void Server::reset_listener() {
    if(listener.is_open()){
        listener.cancel();
        listener.close();
        listener.release();
    }
}

bool Server::start_server(short port, const std::string& ip) {
    reset_listener();
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

    return true;
}


// Callbacks

void Server::new_client_socket(asio::ip::tcp::socket&& soc) {    
    while(clients.count(cur_uuid)) ++cur_uuid; // find a free uuid

    clients.insert_or_assign(cur_uuid, ClientObject(cur_uuid, std::move(soc), std::string("Client ") + std::to_string(cur_uuid)));

    std::cout << "new client socket id=" << cur_uuid << "\n";
}


// Async Loopbacks

void Server::do_accept() {
    listener.async_accept([this](std::error_code er, asio::ip::tcp::socket soc){
        std::scoped_lock lock(accept_lock);
        if(soc.is_open()){
            const auto& ep = soc.remote_endpoint();

            std::cout << "accepted connection from " << ep.address().to_string() << " : " << ep.port() << "\n";

            new_client_socket(std::move(soc));
        } else {
            std::cout << "error accepting connection\n";
        }

        do_accept();
    });
}




}