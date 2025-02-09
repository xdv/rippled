//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2014 Ripple Labs Inc.

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
#include <divvy/app/tx/impl/Transactor.h>
#include <divvy/basics/Log.h>
#include <divvy/protocol/Indexes.h>

namespace divvy {

class CancelTicket
    : public Transactor
{
public:
    CancelTicket (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("CancelTicket"))
    {

    }

    TER doApply () override
    {
        assert (mTxnAccount);

        uint256 const ticketId = mTxn.getFieldH256 (sfTicketID);

        SLE::pointer sleTicket = mEngine->view ().entryCache (ltTICKET, ticketId);

        if (!sleTicket)
            return tecNO_ENTRY;

        auto const ticket_owner =
            sleTicket->getFieldAccount160 (sfAccount);

        bool authorized =
            mTxnAccountID == ticket_owner;

        // The target can also always remove a ticket
        if (!authorized && sleTicket->isFieldPresent (sfTarget))
            authorized = (mTxnAccountID == sleTicket->getFieldAccount160 (sfTarget));

        // And finally, anyone can remove an expired ticket
        if (!authorized && sleTicket->isFieldPresent (sfExpiration))
        {
            std::uint32_t const expiration = sleTicket->getFieldU32 (sfExpiration);

            if (mEngine->getLedger ()->getParentCloseTimeNC () >= expiration)
                authorized = true;
        }

        if (!authorized)
            return tecNO_PERMISSION;

        std::uint64_t const hint (sleTicket->getFieldU64 (sfOwnerNode));

        TER const result = mEngine->view ().dirDelete (false, hint,
            getOwnerDirIndex (ticket_owner), ticketId, false, (hint == 0));

        adjustOwnerCount(mEngine->view(), mEngine->view().entryCache(
            ltACCOUNT_ROOT, getAccountRootIndex(ticket_owner)), -1);
        mEngine->view ().entryDelete (sleTicket);

        return result;
    }
};

TER
transact_CancelTicket (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    if (! engine->enableTickets())
        return temDISABLED;
    return CancelTicket (txn, params, engine).apply();
}


}
