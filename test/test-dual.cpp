#include "libdream.h"
#include "clock.h"

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

        if(!server.start_server(5050)){
            std::cout << "error starting server\n";
            return 1;
        }

        while(OnUpdate());

        server.stop_server();

        return 0;
    }

private:

    Clock endtimer;
    bool OnUpdate() {
        Clock::sleepMilliseconds(15);


        if(endtimer.getSeconds() > 900) return false;

        return true;
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

        while(OnUpdate());

        client.stop_client();

        return 0;
    }

private:
    bool OnUpdate() {
        Clock::sleepMilliseconds(15);

        return true;
    }

};



using ArgumentList = const std::vector<std::string>&;

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

int main(int argc, char* argv[]) {
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