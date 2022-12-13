#include "ip_tools.h"

using asio::ip::tcp;

bool stoip(const std::string& str, asio::ip::address& ipaddr) {
    try { // try for ip address
        ipaddr = asio::ip::address::from_string(str); // ip address
        return true;
    } catch(asio::system_error err) {} // not an ip address

    asio::io_context context; // temporary context

    try {
        tcp::resolver::query query(tcp::v4(), str.data(), "");
        tcp::resolver resolver(context);
        tcp::resolver::iterator iter = resolver.resolve(query);
        tcp::resolver::iterator end; // empty iterator to determine end
        while (iter != end){
            const tcp::endpoint& ep = *iter++;
            if(ep.protocol() == tcp::v4()){
                ipaddr = ep.address();
                return true;
            }
        }
    } catch(std::system_error err) {} // not a hostname

    return false;
}

bool getIpv4Address(std::vector<asio::ip::address>& addrList) {
    asio::io_context context; // temporary context
    try {
        tcp::resolver resolver(context);
        tcp::resolver::query query(asio::ip::host_name(), "", tcp::resolver::query::address_configured);
        tcp::resolver::iterator iter = resolver.resolve(query);
        tcp::resolver::iterator end; // End marker.
        while (iter != end){
            const tcp::endpoint& ep = *iter++;
            if(ep.protocol() == tcp::v4()){
                addrList.push_back(ep.address());
            }
        }
        return true;
    } catch(std::exception err) {}

    return false;
}