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

namespace divvy {

// {
//   secret: <string>   // optional
// }
//
// This command requires Role::ADMIN access because it makes no sense to ask
// an untrusted server for this.
Json::Value doValidationCreate (RPC::Context& context)
{
    DivvyAddress   raSeed;
    Json::Value     obj (Json::objectValue);

    if (!context.params.isMember (jss::secret))
    {
        WriteLog (lsDEBUG, RPCHandler) << "Creating random validation seed.";

        raSeed.setSeedRandom ();                // Get a random seed.
    }
    else if (!raSeed.setSeedGeneric (context.params[jss::secret].asString ()))
    {
        return rpcError (rpcBAD_SEED);
    }

    obj[jss::validation_public_key]
            = DivvyAddress::createNodePublic (raSeed).humanNodePublic ();
    obj[jss::validation_seed] = raSeed.humanSeed ();
    obj[jss::validation_key] = raSeed.humanSeed1751 ();

    return obj;
}

} // divvy
