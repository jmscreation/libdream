#pragma once

#include "libdream.h"
#include <string>
#include <any>
#include <cassert>
#include <variant>
#include <functional>

/*
    A user is a friendly object that directly references the connected client object within the server
    This object can be passed around freely as it's only a reference
*/

namespace dream {

// forward declarations for User objects
class Server;
class Client;
class ClientObject;
class User;

using UserGlobalHookCallback = std::function<bool(User, const std::string& hook, const std::any& data)>;

// simple shared pointer wrapper around the client objects
class ClientObjectRef {
    ClientObject* ptr;
public:
    ~ClientObjectRef();

    ClientObjectRef();
    ClientObjectRef(ClientObject* ptr);
    // copy
    ClientObjectRef(const ClientObjectRef& o) { *this = o; }
    ClientObjectRef& operator=(const ClientObjectRef&);
    // move
    ClientObjectRef(ClientObjectRef&& o) { *this = std::move(o); }
    ClientObjectRef& operator=(ClientObjectRef&&);
    // dereference
    ClientObject* operator->() const { assert(ptr != nullptr); return ptr; }
    ClientObject& operator*() const { assert(ptr != nullptr); return *ptr; }

    bool valid() const { return ptr != nullptr; }
};

class User {
public:
    uint64_t uuid;
    std::string name;

    User(Server* s);
    User(Client* c);

    bool is_connected();
    std::string get_name();

    void send_string(const std::string& str);
    uint64_t register_global_hook(UserGlobalHookCallback cb);

private:
    std::variant<Server*, Client*> controller;
    
    ClientObjectRef get_client_object();
};


}