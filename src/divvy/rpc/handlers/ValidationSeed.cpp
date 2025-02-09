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
#include <beast/utility/make_lock.h>

namespace divvy {

// {
//   secret: <string>
// }
Json::Value doValidationSeed (RPC::Context& context)
{
    auto lock = beast::make_lock(getApp().getMasterMutex());
    Json::Value obj (Json::objectValue);

    if (!context.params.isMember (jss::secret))
    {
        std::cerr << "Unset validation seed." << std::endl;

        getConfig ().VALIDATION_SEED.clear ();
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();
    }
    else if (!getConfig ().VALIDATION_SEED.setSeedGeneric (
        context.params[jss::secret].asString ()))
    {
        getConfig ().VALIDATION_PUB.clear ();
        getConfig ().VALIDATION_PRIV.clear ();

        return rpcError (rpcBAD_SEED);
    }
    else
    {
        auto& seed = getConfig ().VALIDATION_SEED;
        auto& pub = getConfig ().VALIDATION_PUB;

        pub = DivvyAddress::createNodePublic (seed);
        getConfig ().VALIDATION_PRIV = DivvyAddress::createNodePrivate (seed);

        obj[jss::validation_public_key] = pub.humanNodePublic ();
        obj[jss::validation_seed] = seed.humanSeed ();
        obj[jss::validation_key] = seed.humanSeed1751 ();
    }

    return obj;
}

} // divvy
