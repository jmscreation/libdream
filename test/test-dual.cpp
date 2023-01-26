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

struct Test {
    int x, y, z;
    double d;
    std::string name;

    template<typename Archive>
    void serialize(Archive& ar) {
        ar(x,y,z,d,name);
    }
};

class TestServer {
    dream::Server server;
public:
    TestServer() {}

    int RunServer() {
        server.on_client_join = std::bind(&OnClientConnect, this, std::placeholders::_1);

        if(!server.start_server(5050)){
            std::cout << "error starting server\n";
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

        
        if(server.get_client_count() > 0){
            auto clients = server.get_client_list();
            for(dream::User& user : clients){
                user.send_string(list.at(rand() % list.size()));
            }
        }

        // server.broadcast_string();

        return true;
    }

    void OnClientConnect(dream::User& user) {
        using namespace dream;

        std::cout << "User connected: " << user.get_name() << "\n";

        // TEST and DEBUG
        static Clock tc;
        static std::atomic<size_t> msgs = 0;

        user.register_global_hook([&](User client, const std::string& hook, const std::any& data) -> bool {
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
};


class TestClient {
    std::string ip;
    int port;
    dream::Client client;

public:
    TestClient(const std::string& ipaddr, int port=5050): ip(ipaddr), port(port) {

    }

    int RunClient() {

        if(!client.start_client(port, ip)) {
            std::cout << "error connecting to server\n";
            return 1;
        }

        dream::Clock timeout;
        while(!client.is_connected() && timeout.getSeconds() < 6.0);

        dream::Clock fpsT;
        double fps;
        while(OnUpdate()){
            fps = 1.0 / fpsT.getSeconds();
            fpsT.restart();

            std::cout << "                          \r" << std::setprecision(3) << fps;
        }

        client.stop_client();

        return 0;
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
            std::cout << "------------------------- List Of Commands ------------------------\n";
            for(auto& [name, cmd] : commands){
                std::cout << "# " << name << "\t\t" << cmd.description << " #\n";
            }
            std::cout << "-------------------------------------------------------------------\n";

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
            TestClient* client = nullptr;
            if(args.size() < 1 || args.size() > 2) return false;

            std::string ip = args.at(0);
            
            {
                asio::ip::address addr;
                if(!stoip(ip, addr)){
                    std::cout << "invalid ip address\n";
                    return false;
                }
            }
            
            int port = 0;
            if(args.size() > 1){
                try {
                    port = std::stoi(args.at(1));
                } catch(std::invalid_argument e) {
                    std::cout << "invalid argument" << "\n";
                    return false;
                }
            }

            client = port ? new TestClient(ip, port) : new TestClient(ip);
            
            client->RunClient();
            
            delete client;
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
                std::cout << "Usage:\n"
                          << "\t" << name << " " << cmd.help << "\n"
                          << cmd.description << "\n"
                          << "----------------------------------------\n";
            }

            return 0;
        }
    }

    std::cout << "Invalid command - use \"help\" command to display a list of commands\n";
    return 1;
}
