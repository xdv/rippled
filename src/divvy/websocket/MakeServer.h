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

#ifndef RIPPLED_RIPPLE_WEBSOCKET_MAKESERVER_H
#define RIPPLED_RIPPLE_WEBSOCKET_MAKESERVER_H

#include <divvy/app/main/CollectorManager.h>
#include <divvy/net/InfoSub.h>
#include <divvy/server/Port.h>

namespace beast { class Stoppable; }

namespace divvy {

class BasicConfig;

namespace Resource { class Manager; }

namespace websocket {

struct ServerDescription
{
    HTTP::Port port;
    Resource::Manager& resourceManager;
    InfoSub::Source& source;
    beast::Journal& journal;
    BasicConfig const& config;
    CollectorManager& collectorManager;
};

std::unique_ptr<beast::Stoppable> makeServer (ServerDescription const&);

} // websocket
} // divvy

#endif
