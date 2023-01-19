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
        auto& b = blobdata.insert_blob<std::string>("test_blob", "");

        runtime_handle = std::thread([this](){
            runtime_running = true;
            while(runtime_running){
                Clock::sleepMilliseconds(2.0); // client runtime has 2ms delay
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
            soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 8000 });
            soc.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>{ 8000 });
            soc.connect(endpoint);

            server = std::make_unique<ClientObject>(ctx, std::move(soc), 0, name);

            break;
        } catch(...) {
            if(!--retry)
                return false;
        }
    } while(1);

    start_context_handle();
    start_runtime();

    std::cout << "connected to server\n"; // temporary debug

    server->register_hook("pre_command", [&](ClientObject& server, const std::any& data){
        const Command& cmd = std::any_cast<const Command&>(data);

        if(cmd.type == Command::TEST){
            auto& b = blobdata.get_blob<std::string>("test_blob");
            {
                std::stringstream raw(cmd.data);
                cereal::BinaryInputArchive convert(raw);
                convert(*b);
            }

            std::cout << "Blob data:" << b.get() << "\n";
        }
    });

    return true;
}

void Client::stop_client() {
    ctx.stop();

    stop_runtime();
}


void Client::client_runtime() { // check for and remove invalid clients

    if(server){
        if(!server->is_valid()){
            std::cout << "disconnected from server\n";
            server.reset();
        } else if(!server->is_authorized()) {
            server->client_authorize();
        } else {
            server->runtime_update();
        }
    }

}


}