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

struct User {
    dream::Connection user;
    dream::Clock ping;
    double ping_ms;

    User(dream::Connection& user): user(user) {}
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

    void OnClientConnect(dream::Connection& user) {
        using namespace dream;

        dream::dlog << "User connected: " << user.get_name() << "\n";

        // TEST and DEBUG
        static Clock tc;
        static std::atomic<size_t> msgs = 0;

        ::User& u = users.emplace_back(user);

        user.register_global_hook([&](dream::Connection client, const std::string& hook, const std::any& data) -> bool {
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
                    u.ping_ms = round(u.ping.getMilliseconds() / double(server.get_client_count()));
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
    dream::Clock lifetime_timer;

public:
    std::atomic<int32_t> lifetime;

    TestClient(const std::string& ipaddr, int port=5050):
        ip(ipaddr), port(port), terminated(false), lifetime(-1) {
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

        if(lifetime > 0 && int32_t(lifetime_timer.getMilliseconds()) > lifetime){
            return false;
        }

        return true;
    }

    void OnConnect(dream::Connection& user) {
        using namespace dream;

        dream::dlog << "connected to server\n";

        // TEST and DEBUG
        static Clock tc;
        static std::atomic<size_t> msgs = 0;

        server_user = std::make_unique<::User>(user);

        user.register_global_hook([&](dream::Connection client, const std::string& hook, const std::any& data) -> bool {
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
                    server_user->ping_ms = server_user->ping.getMilliseconds();
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



using ArgumentList = std::vector<std::string>;

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
        "Start the test client and connect to remote host. Disconnect and reconnect after timer if specified",
        "ipaddr | ?port | ?timer",
        [](ArgumentList args, const Command& t) -> bool {
            std::unique_ptr<TestClient> client;
            if(args.size() < 1) return false;

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
            int timer = -1;
            if(args.size() > 2){
                try {
                    timer = std::stoi(args.at(2));
                } catch(std::invalid_argument e) {
                    dream::dlog << "invalid argument" << "\n";
                    return false;
                }
            }

            while(1){
                client = port ? std::make_unique<TestClient>(ip, port)
                              : std::make_unique<TestClient>(ip);
                client->lifetime = timer;

                dream::dlog << "connecting to server..." << "\n";
                client->RunClient();

                if(client->Terminated()) break;
            }

            return true;
        }
    }}

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
