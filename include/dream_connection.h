#pragma once

#include "libdream.h"
#include <string>
#include <any>
#include <cassert>
#include <variant>
#include <functional>
#include <shared_mutex>

/*
    A Connection is a friendly object that directly references the connected socket object within the server
    This object can be passed around freely as it is only a reference - the reference is thread safe
*/

namespace dream {

// forward declarations for User objects
class Server;
class Client;
class Socket;
class Connection;

using UserGlobalHookCallback = std::function<bool(Connection, const std::string& hook, const std::any& data)>;

// simple shared pointer wrapper around the client objects
class SocketRef {
    Socket* ptr;
public:
    ~SocketRef();

    SocketRef();
    SocketRef(Socket* ptr);
    // copy
    SocketRef(const SocketRef& o) { *this = o; }
    SocketRef& operator=(const SocketRef&);
    // move
    SocketRef(SocketRef&& o) { *this = std::move(o); }
    SocketRef& operator=(SocketRef&&);
    // dereference
    Socket* operator->() const { assert(ptr != nullptr); return ptr; }
    Socket& operator*() const { assert(ptr != nullptr); return *ptr; }

    bool valid() const { return ptr != nullptr; }
};

class Connection {
public:
    uint64_t uuid;
    std::string name;

    Connection(Server* s);
    Connection(Client* c);

    bool is_connected();
    std::string get_name();

    void send_string(const std::string& str);
    uint64_t register_global_hook(UserGlobalHookCallback cb);

private:
    std::variant<Server*, Client*> controller;
    
    SocketRef get_socket();
};


}