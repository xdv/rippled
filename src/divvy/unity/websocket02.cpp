//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include <beast/module/core/text/LexicalCast.h>

// Must come first to prevent compilation errors.
#include <websocketpp_02/src/uri.cpp>

#include <websocketpp_02/src/sockets/socket_base.hpp>
#include <websocketpp_02/src/websocketpp.hpp>
#include <websocketpp_02/src/base64/base64.cpp>
#include <websocketpp_02/src/messages/data.cpp>
#include <websocketpp_02/src/processors/hybi_header.cpp>
#include <websocketpp_02/src/processors/hybi_util.cpp>
#include <websocketpp_02/src/network_utilities.cpp>
#include <websocketpp_02/src/sha1/sha1.h>
#include <websocketpp_02/src/sha1/sha1.cpp>

#include <divvy/websocket/WebSocket02.cpp>
#include <divvy/websocket/MakeServer.cpp>
#include <divvy/websocket/LogWebsockets.cpp>

// Must come last to prevent compilation errors.
#include <websocketpp_02/src/md5/md5.c>
