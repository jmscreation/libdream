#include "libdream.h"

namespace dream {


ClientObjectRef::ClientObjectRef(): ptr(nullptr) {}

ClientObjectRef::ClientObjectRef(ClientObject* ptr): ptr(ptr) {
    ++ptr->external_lock;
}

ClientObjectRef& ClientObjectRef::operator=(const ClientObjectRef& o) {
    ptr = o.ptr;
    if(ptr) ++ptr->external_lock;
    return *this;
}

ClientObjectRef& ClientObjectRef::operator=(ClientObjectRef&& o) {
    ptr = o.ptr;
    o.ptr = nullptr;
    return *this;
}

ClientObjectRef::~ClientObjectRef() {
    if(ptr) --ptr->external_lock;
}

User::User(Server* s): uuid {}, name {}, controller(s) {}
User::User(Client* c): uuid {}, name {}, controller(c) {}


bool User::is_connected() {
    ClientObjectRef client;
    if( !(client = get_client_object()).valid() ) return false;

    return client->is_authorized();
}

std::string User::get_name() {
    ClientObjectRef client;
    if( !(client = get_client_object()).valid() ) return name; // couldn't find object so return cache

    return (name = client->get_name()); // update local name in cache
}

void User::send_string(const std::string& data) {
    ClientObjectRef client;
    if( !(client = get_client_object()).valid() ) return;

    client->send_command(Command(Command::STRING, data));
}

uint64_t User::register_global_hook(UserGlobalHookCallback cb) {
    ClientObjectRef client;
    if( !(client = get_client_object()).valid() ) return 0;

    User copy(*this); // get a copy of myself and pass all the way through the lambda chain
    return client->register_global_hook(
        [this, cb, copy](ClientObject& c, const std::string& hook, const std::any& data) -> bool {
            return cb(copy, hook, data);
        }
    );
}


ClientObjectRef User::get_client_object() {
    ClientObjectRef cobj;

    Server** _server = std::get_if<Server*>(&controller);
    if(_server){
        Server* server = *_server;

        std::scoped_lock lock(server->accept_lock);
        if(server->clients.count(uuid))
            cobj = ClientObjectRef(server->clients.at(uuid).get());
    } else {
        Client** _client = std::get_if<Client*>(&controller);
        if(_client){
            Client* client = *_client;

            cobj = ClientObjectRef(client->server.get());
        }
    }

    if(!cobj.valid() || !cobj->is_valid())
        return ClientObjectRef {};

    return cobj;
}



}