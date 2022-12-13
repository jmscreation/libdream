#pragma once

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <asio.hpp>

bool stoip(const std::string& str, asio::ip::address& ipaddr);

bool getIpv4Address(std::vector<asio::ip::address>& addrList);