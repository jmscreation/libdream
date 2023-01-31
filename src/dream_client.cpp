#include "dream_client.h"

namespace dream {

Client::Client(): idle(ctx), header({}), cur_uuid(0), runtime_running(false) {}

Client::~Client() {
    stop_client();
}

void Client::start_context_handle() {
    ctx_handle = std::thread([this](){
        ctx.run();
    });
}

void Client::start_runtime() {
    if(!runtime_running){
        runtime_handle = std::thread([this](){
            runtime_running = true;
            while(runtime_running){
                Clock::sleepMilliseconds(2); // client runtime has 2ms delay
                {
                    std::scoped_lock lock(runtime_lock);
                    client_runtime(); // invoke runtime update
                }
            }
        });
    }
}

void Client::stop_runtime() {
    runtime_running = false;
    blobdata.clear();
    if(runtime_handle.joinable()){
        runtime_handle.join();
    }
}


bool Client::start_client(short port, const std::string& ip, const std::string& name) {
    stop_runtime();

    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);

    size_t retry = 10;
    do {
        try {
            if(ip.size()){
                asio::ip::address addr;
                if(!stoip(ip, addr)) return false; // invalid address
                endpoint = asio::ip::tcp::endpoint(addr, port);
            }

            asio::ip::tcp::socket soc(ctx);
            soc.open(endpoint.protocol());
            soc.set_option(asio::ip::tcp::no_delay(true));
            // soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 8000 });
            // soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>{ 8000 });
            soc.connect(endpoint);

            server = generate_server_object(std::move(soc), 0, name);

            server->register_hook("on_authorized", [this](Socket& client, const std::any& data){
                if(on_connect){
                    Connection user(this);
                    user.uuid = client.get_id();
                    user.name = client.get_name();

                    std::scoped_lock lock(runtime_lock);
                    on_connect(user);
                }
            });

            break;
        } catch(...) {
            if(!--retry)
                return false;
        }
    } while(1);

    start_context_handle();
    start_runtime();

    return true;
}

void Client::stop_client() {
    ctx.stop();

    stop_runtime();

    server.reset();
    while(!ctx.stopped());
    ctx.reset();

    if(ctx_handle.joinable()){
        ctx_handle.join();
    }
}


void Client::client_runtime() { // check for and remove invalid clients

    if(server){
        if(!server->is_valid()){
            dlog << "disconnected from server\n";
            server.reset();
        } else if(!server->is_authorized()) {
            server->client_authorize();
        } else {
            server->runtime_update();
        }
    }

}

Connection Client::get_socket() {
    Connection user(this);
    user.uuid = server->get_id();
    user.name = server->get_name();
    return user;
}

void Client::send_string(const std::string& data) {
    server->send_command(Command(Command::STRING, data));
}


// Misc

std::unique_ptr<Socket> Client::generate_server_object(asio::ip::tcp::socket&& soc, uint64_t id, const std::string& name) {
    return std::unique_ptr<Socket>( new Socket(ctx, std::move(soc), cur_uuid, std::to_string(cur_uuid)) );
}


}