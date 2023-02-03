#pragma once

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#ifndef DREAM_NO_CONNECTION_LIMIT
	#ifndef DREAM_CONNECTION_LIMIT
	#define DREAM_CONNECTION_LIMIT 20
	#endif
#endif

#include <asio.hpp>

#include "ip_tools.h"

#include "lib_cereal.h"

// include all dreamlib headers


#include "dream_clock.h"
#include "dream_externs.h"
#include "dream_block.h"
#include "dream_blob.h"
#include "dream_server.h"
#include "dream_client.h"
#include "dream_connection.h"

namespace dream {

}