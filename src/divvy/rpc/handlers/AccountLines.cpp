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
#include <divvy/app/main/Application.h>
#include <divvy/rpc/impl/Tuning.h>
#include <divvy/app/paths/DivvyState.h>

namespace divvy {

struct VisitData
{
    std::vector <DivvyState::pointer> items;
    AccountID const& accountID;
    DivvyAddress const& divvyAddressPeer;
    AccountID const& raPeerAccount;
};

void addLine (Json::Value& jsonLines, DivvyState const& line)
{
    STAmount const& saBalance (line.getBalance ());
    STAmount const& saLimit (line.getLimit ());
    STAmount const& saLimitPeer (line.getLimitPeer ());
    Json::Value& jPeer (jsonLines.append (Json::objectValue));

    jPeer[jss::account] = to_string (line.getAccountIDPeer ());
    // Amount reported is positive if current account holds other
    // account's IOUs.
    //
    // Amount reported is negative if other account holds current
    // account's IOUs.
    jPeer[jss::balance] = saBalance.getText ();
    jPeer[jss::currency] = to_string (saBalance.issue ().currency);
    jPeer[jss::limit] = saLimit.getText ();
    jPeer[jss::limit_peer] = saLimitPeer.getText ();
    jPeer[jss::quality_in]
        = static_cast<Json::UInt> (line.getQualityIn ());
    jPeer[jss::quality_out]
        = static_cast<Json::UInt> (line.getQualityOut ());
    if (line.getAuth ())
        jPeer[jss::authorized] = true;
    if (line.getAuthPeer ())
        jPeer[jss::peer_authorized] = true;
    if (line.getNoDivvy ())
        jPeer[jss::no_divvy] = true;
    if (line.getNoDivvyPeer ())
        jPeer[jss::no_divvy_peer] = true;
    if (line.getFreeze ())
        jPeer[jss::freeze] = true;
    if (line.getFreezePeer ())
        jPeer[jss::freeze_peer] = true;
}

// {
//   account: <account>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value doAccountLines (RPC::Context& context)
{
    auto const& params (context.params);
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);

    Ledger::pointer ledger;
    Json::Value result (RPC::lookupLedger (params, ledger, context.netOps));
    if (! ledger)
        return result;

    std::string strIdent (params[jss::account].asString ());
    bool bIndex (params.isMember (jss::account_index));
    int iIndex (bIndex ? params[jss::account_index].asUInt () : 0);
    DivvyAddress divvyAddress;

    auto jv = RPC::accountFromString (
        divvyAddress, bIndex, strIdent, iIndex, false);
    if (! jv.empty ())
    {
        for (auto it = jv.begin (); it != jv.end (); ++it)
            result[it.memberName ()] = it.key ();

        return result;
    }

    if (! ledger->exists(getAccountRootIndex(
            divvyAddress.getAccountID())))
        return rpcError (rpcACT_NOT_FOUND);

    std::string strPeer (params.isMember (jss::peer)
        ? params[jss::peer].asString () : "");
    bool bPeerIndex (params.isMember (jss::peer_index));
    int iPeerIndex (bIndex ? params[jss::peer_index].asUInt () : 0);

    DivvyAddress divvyAddressPeer;

    if (! strPeer.empty ())
    {
        result[jss::peer] = divvyAddress.humanAccountID ();

        if (bPeerIndex)
            result[jss::peer_index] = iPeerIndex;

        result = RPC::accountFromString (
            divvyAddressPeer, bPeerIndex, strPeer, iPeerIndex, false);

        if (! result.empty ())
            return result;
    }

    AccountID raPeerAccount;
    if (divvyAddressPeer.isValid ())
        raPeerAccount = divvyAddressPeer.getAccountID ();

    unsigned int limit;
    if (params.isMember (jss::limit))
    {
        auto const& jvLimit (params[jss::limit]);
        if (! jvLimit.isIntegral ())
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.isUInt () ? jvLimit.asUInt () :
            std::max (0, jvLimit.asInt ());

        if (context.role != Role::ADMIN)
        {
            limit = std::max (RPC::Tuning::minLinesPerRequest,
                std::min (limit, RPC::Tuning::maxLinesPerRequest));
        }
    }
    else
    {
        limit = RPC::Tuning::defaultLinesPerRequest;
    }

    Json::Value& jsonLines (result[jss::lines] = Json::arrayValue);
    AccountID const& raAccount(divvyAddress.getAccountID ());
    VisitData visitData = { {}, raAccount, divvyAddressPeer, raPeerAccount };
    unsigned int reserve (limit);
    uint256 startAfter;
    std::uint64_t startHint;

    if (params.isMember (jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker (params[jss::marker]);

        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");

        startAfter.SetHex (marker.asString ());
        auto const sleLine = fetch(*ledger, startAfter,
            getApp().getSLECache());

        if (sleLine == nullptr || sleLine->getType () != ltRIPPLE_STATE)
            return rpcError (rpcINVALID_PARAMS);

        if (sleLine->getFieldAmount (sfLowLimit).getIssuer () == raAccount)
            startHint = sleLine->getFieldU64 (sfLowNode);
        else if (sleLine->getFieldAmount (sfHighLimit).getIssuer () == raAccount)
            startHint = sleLine->getFieldU64 (sfHighNode);
        else
            return rpcError (rpcINVALID_PARAMS);

        // Caller provided the first line (startAfter), add it as first result
        auto const line = DivvyState::makeItem (raAccount, sleLine);
        if (line == nullptr)
            return rpcError (rpcINVALID_PARAMS);

        addLine (jsonLines, *line);
        visitData.items.reserve (reserve);
    }
    else
    {
        startHint = 0;
        // We have no start point, limit should be one higher than requested.
        visitData.items.reserve (++reserve);
    }

    if (! forEachItemAfter(*ledger, raAccount, getApp().getSLECache(),
            startAfter, startHint, reserve,
        [&visitData](std::shared_ptr<SLE const> const& sleCur)
        {
            auto const line =
                DivvyState::makeItem (visitData.accountID, sleCur);
            if (line != nullptr &&
                (! visitData.divvyAddressPeer.isValid () ||
                visitData.raPeerAccount == line->getAccountIDPeer ()))
            {
                visitData.items.emplace_back (line);
                return true;
            }

            return false;
        }))
    {
        return rpcError (rpcINVALID_PARAMS);
    }

    if (visitData.items.size () == reserve)
    {
        result[jss::limit] = limit;

        DivvyState::pointer line (visitData.items.back ());
        result[jss::marker] = to_string (line->key());
        visitData.items.pop_back ();
    }

    result[jss::account] = divvyAddress.humanAccountID ();

    for (auto const& item : visitData.items)
        addLine (jsonLines, *item.get ());

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // divvy
