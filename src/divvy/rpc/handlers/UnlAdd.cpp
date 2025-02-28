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
#include <divvy/app/misc/UniqueNodeList.h>
#include <beast/utility/make_lock.h>

namespace divvy {

// {
//   node: <domain>|<node_public>,
//   comment: <comment>             // optional
// }
Json::Value doUnlAdd (RPC::Context& context)
{
    auto lock = beast::make_lock(getApp().getMasterMutex());

    std::string strNode = context.params.isMember (jss::node)
            ? context.params[jss::node].asString () : "";
    std::string strComment = context.params.isMember (jss::comment)
            ? context.params[jss::comment].asString () : "";

    DivvyAddress raNodePublic;

    if (raNodePublic.setNodePublic (strNode))
    {
        getApp().getUNL ().nodeAddPublic (
            raNodePublic, UniqueNodeList::vsManual, strComment);
        return RPC::makeObjectValue ("adding node by public key");
    }
    else
    {
        getApp().getUNL ().nodeAddDomain (
            strNode, UniqueNodeList::vsManual, strComment);
        return RPC::makeObjectValue ("adding node by domain");
    }
}

} // divvy
