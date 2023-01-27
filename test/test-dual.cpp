#include "libdream.h"

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <iomanip>
#include <map>
#include <functional>


#include <sstream>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/binary.hpp>

struct User {
    dream::User user;
    dream::Clock ping;
    double ping_ms;

    User(dream::User& user): user(user) {}
};


class TestServer {
    dream::Server server;
    std::vector<User> users;
public:
    TestServer() { users.reserve(1024); }

    int RunServer() {
        server.on_client_join = std::bind(&TestServer::OnClientConnect, this, std::placeholders::_1);

        if(!server.start_server(5050)){
            dream::dlog << "error starting server\n";
            return 1;
        }

        while(OnUpdate());

        server.stop_server();

        return 0;
    }

private:

    bool OnUpdate() {
        dream::Clock::sleepMilliseconds(10);

        if(!server.is_running()) return false;

        static std::vector<std::string> list = {
            "this is test data", "chunk of data", "something", "more data as a string placed here",
            "large piece of data:" + std::string(100000, 'x')};

        
        for(auto it = users.begin(); it != users.end(); ++it){
            User& u = *it;
            if(!u.user.is_connected()){
                it = users.erase(it);
                if(it == users.end()) break;
                --it;
            }
        }

        //user.send_string(list.at(rand() % list.size()));
        server.broadcast_string(list.at(rand() % list.size()));

        return true;
    }

    void OnClientConnect(dream::User& user) {
        using namespace dream;

        dream::dlog << "User connected: " << user.get_name() << "\n";

        // TEST and DEBUG
        static Clock tc;
        static std::atomic<size_t> msgs = 0;

        ::User& u = users.emplace_back(user);

        user.register_global_hook([&](dream::User client, const std::string& hook, const std::any& data) -> bool {
            if(hook == "pre_command"){
                const Command& cmd = std::any_cast<const Command&>(data);
                if(cmd.type == Command::STRING){
                    msgs++;
                    double ping = 0.0;
                    for(auto& u : users){
                        ping += u.ping_ms;
                    }
                    ping /= double(users.size());

                    dream::dlog << "                                \r" << std::setw(3) << std::setprecision(4) << (double(msgs) / tc.getSeconds()) << " packages per second; " << ping << "ms";
                }

                if(cmd.type == Command::RESPONSE){
                    msgs = 0;
                    tc.restart();
                    u.ping_ms = size_t(round(u.ping.getMilliseconds() / double(server.get_client_count())));
                }
            }

            if(hook == "on_send"){
                const Command& cmd = std::any_cast<const Command&>(data);
                if(cmd.type == Command::PING){
                    u.ping_ms = 0.0;
                    u.ping.restart();
                }
            }

            return true;
        });
    }
};


class TestClient {
    std::string ip;
    int port;
    dream::Client client;
    bool terminated;
    std::unique_ptr<User> server_user;

public:
    TestClient(const std::string& ipaddr, int port=5050): ip(ipaddr), port(port), terminated(false) {

    }

    int RunClient() {
        client.on_connect = std::bind(&TestClient::OnConnect, this, std::placeholders::_1);

        if(!client.start_client(port, ip)) {
            dream::dlog << "error connecting to server\n";
            return 1;
        }

        dream::Clock timeout;
        while(!client.is_connected() && timeout.getSeconds() < 6.0);

        while(OnUpdate()){}

        client.stop_client();

        return 0;
    }

    bool Terminated() {
        return terminated;
    }

private:
    bool OnUpdate() {
        dream::Clock::sleepMilliseconds(5);

        if(!client.is_running() || !client.is_connected()) return false;

        static std::vector<std::string> list = {
            "this is test data", "chunk of data", "something", "more data as a string placed here",
            "large piece of data:" + std::string(100000, 'x')};

        client.send_string(list.at(rand() % list.size()));

        return true;
    }

    void OnConnect(dream::User& user) {
        using namespace dream;

        dream::dlog << "connected to server\n";

        // TEST and DEBUG
        static Clock tc;
        static std::atomic<size_t> msgs = 0;

        server_user = std::make_unique<::User>(user);

        user.register_global_hook([&](dream::User client, const std::string& hook, const std::any& data) -> bool {
            if(hook == "pre_command"){
                const Command& cmd = std::any_cast<Command>(data);
                if(cmd.type == Command::STRING){
                    msgs++;
                    dream::dlog << "                                \r" << std::setw(3) << std::setprecision(4) << (double(msgs) / tc.getSeconds()) << " packages per second; " << server_user->ping_ms << "ms";
                }

                if(cmd.type == Command::PING){
                    msgs = 0;
                    tc.restart();
                    server_user->ping_ms = 0.0;
                    server_user->ping.restart();
                }

                if(cmd.type == Command::RESPONSE){
                    server_user->ping_ms = size_t(server_user->ping.getMilliseconds());
                }

                if(cmd.type == Command::TEST){
                    terminated = true;
                    this->client.stop_client();
                }
            }

            return true;
        });
    }

};

#include <fstream>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

struct FileHeader {
    size_t size;
    std::string filename;

    template<typename T>
    void serialize(T& archive) {
        archive(size, filename);
    }
};

#include <windows.h>

class FileTransfer {


    std::ifstream input;
    std::ofstream output;

    std::string path; // input file or output file

    std::optional<dream::User> duser {}; // currently connected user
    std::atomic_bool stream_to_file, finished;
    FileHeader incoming_fileheader; // current incoming file header
    std::mutex mtx; // protect from data race

    dream::Server server; // server waits for incoming file
    dream::Client client; // client connects to listening server and sends file on connect

public:
    FileTransfer(const std::string& filepath):
        path(filepath), stream_to_file(false), finished(false), incoming_fileheader({}) {}

    ~FileTransfer() {}

    void SendFile(const std::string& ipaddr, int port=5050) {
        finished = false;
        stream_to_file = false;

        input.open(path, std::ios::binary | std::ios::in);
        if(!input.is_open()){
            dream::dlog << "failed to open the file for reading\n";
            return;
        }

        client.on_connect = std::bind(&FileTransfer::ReadySend, this, std::placeholders::_1);

        if(!client.start_client(port, ipaddr)){
            dream::dlog << "failed to find a server listening\n";
            return;
        }

        int wait = 8;
        while(!client.is_connected()){
            dream::Clock::sleepSeconds(1);
            if(--wait == 0) break;
        }
        std::string outside_cache;
        while(client.is_connected() && !finished){
            if(GetAsyncKeyState(VK_UP) & 0x8000 && GetAsyncKeyState(VK_DOWN) & 0x8000){
                dream::Clock::sleepMilliseconds(15);
                continue;
            }
            if(stream_to_file){
                try {
                    if(!input.eof()){
                        alignas(128) char cache[4096]; // cache line for file streaming
                        input.read(cache, sizeof(cache));
                        outside_cache.assign(cache, std::min(sizeof(cache), size_t(input.gcount())));
                        duser.value().send_string(outside_cache);
                    }
                } catch(...) {
                    dream::dlog << "exception when processing file data...\n";
                    stream_to_file = false;
                    client.stop_client();
                    break;
                }
            } else {
                dream::Clock::sleepMilliseconds(15);
            }
        }
    }

    void ReceiveFile(int port=5050) {
        finished = false;
        stream_to_file = false;

        output.open(path, std::ios::binary | std::ios::out);
        if(!output.is_open()){
            dream::dlog << "failed to open the file for writing\n";
            return;
        }

        if(!server.start_server(port)){ // all iface
            dream::dlog << "failed to listen on port " << port << "\n";
            return;
        }
        dream::dlog << "Listening for incoming file transfer...\n";

        server.on_client_join = std::bind(&FileTransfer::ReadyReceive, this, std::placeholders::_1);

        while(server.is_running()){
            {
                std::scoped_lock lock(mtx);
                if(duser.has_value()) {
                    if(!duser.value().is_connected()){
                        server.stop_server();
                        break;
                    }
                }
                if(finished){
                    stream_to_file = false;
                    break;
                }
            }
            dream::Clock::sleepSeconds(5);
        }
    }

private:

    /*
        Currently Available Hooks:

        on_authorized - don't use
        on_send
        on_sent
        on_disconnected
        internal_error
        pre_command
        post_command

    */

    void ReadySend(dream::User& user) {
        duser = user;

        dream::dlog << "Preparing to send file to server...\n";

        user.register_global_hook([this](dream::User u, const std::string& event, const std::any& data) -> bool {
            
            if(event == "pre_command"){
                const dream::Command& cmd = std::any_cast<const dream::Command&>(data);

                if(cmd.type == dream::Command::STRING){
                    const std::string& msg = cmd.data;

                    if(msg == "READY"){
                        dream::dlog << "Processing file header information...\n";
                        FileHeader fileheader;

                        input.seekg(0, std::ios::end);
                        fileheader.size = input.tellg();
                        input.seekg(0, std::ios::beg);

                        fileheader.filename = fs::path(path).filename().string();
                        try {
                            std::stringstream blob;
                            cereal::BinaryOutputArchive cnv(blob);
                            cnv(fileheader);
                            u.send_string(blob.str());
                            incoming_fileheader = fileheader;
                        } catch(...) {
                            dream::dlog << "exception when processing/sending file header...\n";
                            return true;
                        }
                    }

                    if(msg == "SEND_ME_THE_FILE"){
                        dream::dlog << "Transmitting file data...\n";
                        stream_to_file = true;
                    }
                }
            }

            if(event == "on_sent"){
                if(stream_to_file){
                    dream::dlog << "                        \r"
                                << uint32_t(ceil(double(input.tellg()) / double(incoming_fileheader.size) * 100.0)) << "\% Sent";

                    if(input.tellg() >= incoming_fileheader.size){
                        dream::dlog << "Finished transferring file!\n";
                        finished = true;
                    }
                }
            }
            
            return true;
        });

    }

    void ReadyReceive(dream::User& user) {
        duser = user;

        user.register_global_hook([this](dream::User u, const std::string& event, const std::any& data) -> bool {
            if(event == "pre_command"){
                const dream::Command& cmd = std::any_cast<const dream::Command&>(data);

                if(cmd.type == dream::Command::STRING){
                    const std::string& msg = cmd.data;

                    if(stream_to_file){
                        output << msg;
                        if(output.tellp() >= incoming_fileheader.size){
                            dream::dlog << "File transfer finished!\n";

                            stream_to_file = false;
                            finished = true;
                            return false;
                        } else {
                            dream::dlog << "                        \r"
                                        << uint32_t(ceil(double(output.tellp()) / double(incoming_fileheader.size) * 100.0)) << "\% Received";
                        }
                        return true;
                    } else {
                        FileHeader fileheader;
                        try {
                            std::stringstream tmp;
                            tmp << msg;

                            cereal::BinaryInputArchive cnv(tmp);
                            cnv(fileheader);
                        } catch(...) {
                            dream::dlog << "File header failed to process!\nTrying again...\n";
                            u.send_string("READY");
                            return true;
                        }

                        incoming_fileheader = fileheader;
                        stream_to_file = true;
                        dream::dlog << "Transferring file " << fileheader.filename << "...\n";
                        u.send_string("SEND_ME_THE_FILE");
                    }
                }
            }
            return true;
        });

        dream::Clock::sleepSeconds(3); // wait 3 seconds for client to breath

        user.send_string("READY");
    }

};




using ArgumentList = const std::vector<std::string>&;

namespace entry {


struct Command {
    std::string description, help;
    std::function<bool(ArgumentList args, const Command& t)> cmd;
};

std::vector<std::string> arguments;
std::map<std::string, Command> commands {
    {"help", {
        "Command to display this help menu",
        "== no arguments ==",
        [](ArgumentList args, const Command& t) -> bool {
            dream::dlog << "------------------------- List Of Commands ------------------------\n";
            for(auto& [name, cmd] : commands){
                dream::dlog << "# " << name << "\t\t" << cmd.description << " #\n";
            }
            dream::dlog << "-------------------------------------------------------------------\n";

            return true;
        }
    }},
    {"server", {
        "Start the test server",
        "== no arguments ==",
        [](ArgumentList args, const Command& t) -> bool {
            TestServer* server = new TestServer();

            server->RunServer();

            delete server;
            return true;
        }
    }},
    {"client", {
        "Start the test client and connect to remote host",
        "ipaddr | ?port",
        [](ArgumentList args, const Command& t) -> bool {
            std::unique_ptr<TestClient> client;
            if(args.size() < 1 || args.size() > 2) return false;

            std::string ip = args.at(0);
            
            {
                asio::ip::address addr;
                if(!stoip(ip, addr)){
                    dream::dlog << "invalid ip address\n";
                    return false;
                }
            }
            
            int port = 0;
            if(args.size() > 1){
                try {
                    port = std::stoi(args.at(1));
                } catch(std::invalid_argument e) {
                    dream::dlog << "invalid argument" << "\n";
                    return false;
                }
            }

            while(1){
                client = port ? std::make_unique<TestClient>(ip, port)
                              : std::make_unique<TestClient>(ip);
                
                dream::dlog << "connecting to server..." << "\n";
                client->RunClient();

                if(client->Terminated()) break;
            }

            return true;
        }
    }},


    {"send_file", {
        "Send a file to a listening server",
        "ipaddr | filepath",
        [](ArgumentList args, const Command& t) -> bool {
            if(args.size() < 2) return false;

            std::string ip = args.at(0);
            std::string filepath = args.at(1);

            {
                asio::ip::address addr;
                if(!stoip(ip, addr)){
                    dream::dlog << "invalid ip address\n";
                    return false;
                }
            }

            FileTransfer client(filepath); // input file

            client.SendFile(ip, 5050);

            return true;
        }
    }},
    {"receive_file", {
        "Listen for an incoming file transfer",
        "output-filepath",
        [](ArgumentList args, const Command& t) -> bool {
            if(args.size() < 1) return false;

            std::string filepath = args.at(0);

            FileTransfer server(filepath); // input file

            server.ReceiveFile(5050);

            return true;
        }
    }},

};

std::string GetValue(const std::string& name) {
    auto it = std::find(arguments.begin(), arguments.end(), name);
    if(it == arguments.end() || ++it == arguments.end()) return "";

    return *it;
}

std::vector<std::string> GetArgumentList(const std::string& name) {
    auto it = std::find(arguments.begin(), arguments.end(), name);
    if(it == arguments.end() || ++it == arguments.end()) return {};

    std::vector<std::string> rlist(it, arguments.end());
    return rlist;
}

bool HasArgument(const std::string& name) {
    return std::find(arguments.begin(), arguments.end(), name) != arguments.end();
}


}


int main(int argc, char* argv[]) {
    using namespace entry;

    for(int i=1; i < argc; ++i){
        arguments.emplace_back(argv[i]);
    }

    for(auto& [name, cmd] : commands){
        if(HasArgument(name)){

            if(!cmd.cmd(GetArgumentList(name), cmd)){
                dream::dlog << "Usage:\n"
                          << "\t" << name << " " << cmd.help << "\n"
                          << cmd.description << "\n"
                          << "----------------------------------------\n";
            }

            return 0;
        }
    }

    dream::dlog << "Invalid command - use \"help\" command to display a list of commands\n";
    return 1;
}
