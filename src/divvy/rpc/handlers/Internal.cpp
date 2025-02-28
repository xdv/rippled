//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2012-2014 Ripple Labs Inc.

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
#include <divvy/rpc/InternalHandler.h>

namespace divvy {

RPC::InternalHandler* RPC::InternalHandler::headHandler = nullptr;

Json::Value doInternal (RPC::Context& context)
{
    // Used for debug or special-purpose RPC commands
    if (!context.params.isMember (jss::internal_command))
        return rpcError (rpcINVALID_PARAMS);

    auto name = context.params[jss::internal_command].asString ();
    auto params = context.params[jss::params];

    for (auto* h = RPC::InternalHandler::headHandler; h; )
    {
        if (name == h->name_)
        {
            WriteLog (lsWARNING, RPCHandler)
                << "Internal command " << name << ": " << params;
            Json::Value ret = h->handler_ (params);
            WriteLog (lsWARNING, RPCHandler)
                << "Internal command returns: " << ret;
            return ret;
        }

        h = h->nextHandler_;
    }

    return rpcError (rpcBAD_SYNTAX);
}

} // divvy
