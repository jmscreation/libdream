#include "libdream.h"

namespace dream {


SocketRef::SocketRef(): ptr(nullptr) {}

SocketRef::SocketRef(Socket* ptr): ptr(ptr) {
    ++ptr->external_lock;
}

SocketRef& SocketRef::operator=(const SocketRef& o) {
    ptr = o.ptr;
    if(ptr) ++ptr->external_lock;
    return *this;
}

SocketRef& SocketRef::operator=(SocketRef&& o) {
    ptr = o.ptr;
    o.ptr = nullptr;
    return *this;
}

SocketRef::~SocketRef() {
    if(ptr) --ptr->external_lock;
}

Connection::Connection(Server* s): uuid {}, name {}, controller(s) {}
Connection::Connection(Client* c): uuid {}, name {}, controller(c) {}


bool Connection::is_connected() {
    SocketRef client;
    if( !(client = get_socket()).valid() ) return false;

    return client->is_authorized();
}

std::string Connection::get_name() {
    SocketRef client;
    if( !(client = get_socket()).valid() ) return name; // couldn't find object so return cache

    return (name = client->get_name()); // update local name in cache
}

void Connection::send_string(const std::string& data) {
    SocketRef client;
    if( !(client = get_socket()).valid() ) return;

    client->send_command(Command(Command::STRING, data));
}

uint64_t Connection::register_global_hook(UserGlobalHookCallback cb) {
    SocketRef client;
    if( !(client = get_socket()).valid() ) return 0;

    Connection copy(*this); // get a copy of myself and pass all the way through the lambda chain
    return client->register_global_hook(
        [this, cb, copy](Socket& c, const std::string& hook, const std::any& data) -> bool {
            return cb(copy, hook, data);
        }
    );
}


SocketRef Connection::get_socket() {
    SocketRef cobj;

    Server** _server = std::get_if<Server*>(&controller);
    if(_server){
        Server* server = *_server;

        std::shared_lock<std::shared_mutex> lock(server->runtime_lock);
        if(server->clients.count(uuid))
            cobj = SocketRef(server->clients.at(uuid).get());
    } else {
        Client** _client = std::get_if<Client*>(&controller);
        if(_client){
            Client* client = *_client;

            cobj = SocketRef(client->server.get());
        }
    }

    if(!cobj.valid() || !cobj->is_valid())
        return SocketRef {};

    return cobj;
}



}