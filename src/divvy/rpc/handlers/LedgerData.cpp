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
#include <divvy/server/Role.h>

namespace divvy {

// Get state nodes from a ledger
//   Inputs:
//     limit:        integer, maximum number of entries
//     marker:       opaque, resume point
//     binary:       boolean, format
//   Outputs:
//     ledger_hash:  chosen ledger's hash
//     ledger_index: chosen ledger's index
//     state:        array of state nodes
//     marker:       resume point, if any
Json::Value doLedgerData (RPC::Context& context)
{
    int const BINARY_PAGE_LENGTH = 2048;
    int const JSON_PAGE_LENGTH = 256;

    Ledger::pointer lpLedger;
    auto const& params = context.params;

    Json::Value jvResult = RPC::lookupLedger (params, lpLedger, context.netOps);
    if (!lpLedger)
        return jvResult;

    uint256 resumePoint;
    if (params.isMember (jss::marker))
    {
        Json::Value const& jMarker = params[jss::marker];
        if (!jMarker.isString ())
            return RPC::expected_field_error (jss::marker, "valid");
        if (!resumePoint.SetHex (jMarker.asString ()))
            return RPC::expected_field_error (jss::marker, "valid");
    }

    bool isBinary = params[jss::binary].asBool();

    int limit = -1;
    int maxLimit = isBinary ? BINARY_PAGE_LENGTH : JSON_PAGE_LENGTH;

    if (params.isMember (jss::limit))
    {
        Json::Value const& jLimit = params[jss::limit];
        if (!jLimit.isIntegral ())
            return RPC::expected_field_error (jss::limit, "integer");

        limit = jLimit.asInt ();
    }

    if ((limit < 0) || ((limit > maxLimit) && (context.role != Role::ADMIN)))
        limit = maxLimit;

    jvResult[jss::ledger_hash] = to_string (lpLedger->getHash());
    jvResult[jss::ledger_index] = std::to_string( lpLedger->getLedgerSeq ());

    Json::Value& nodes = (jvResult[jss::state] = Json::arrayValue);
    SHAMap& map = *(lpLedger->peekAccountStateMap ());

    for (;;)
    {
       std::shared_ptr<SHAMapItem> item = map.peekNextItem (resumePoint);
       if (!item)
           break;
       resumePoint = item->getTag();

       if (limit-- <= 0)
       {
           --resumePoint;
           jvResult[jss::marker] = to_string (resumePoint);
           break;
       }

       if (isBinary)
       {
           Json::Value& entry = nodes.append (Json::objectValue);
           entry[jss::data] = strHex (
               item->peekData().begin(), item->peekData().size());
           entry[jss::index] = to_string (item->getTag ());
       }
       else
       {
           SLE sle (item->peekSerializer(), item->getTag ());
           Json::Value& entry = nodes.append (sle.getJson (0));
           entry[jss::index] = to_string (item->getTag ());
       }
    }

    return jvResult;
}

} // divvy
