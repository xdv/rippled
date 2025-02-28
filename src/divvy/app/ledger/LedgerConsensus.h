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

#ifndef RIPPLE_APP_CONSENSUS_LEDGERCONSENSUS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_LEDGERCONSENSUS_H_INCLUDED

#include <divvy/app/ledger/Ledger.h>
#include <divvy/app/ledger/LedgerProposal.h>
#include <divvy/app/misc/CanonicalTXSet.h>
#include <divvy/app/misc/FeeVote.h>
#include <divvy/app/tx/InboundTransactions.h>
#include <divvy/app/tx/LocalTxs.h>
#include <divvy/json/json_value.h>
#include <divvy/overlay/Peer.h>
#include <divvy/protocol/DivvyLedgerHash.h>
#include <chrono>

namespace divvy {

/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
{
public:
    virtual ~LedgerConsensus() = 0;

    virtual Json::Value getJson (bool full) = 0;

    virtual uint256 getLCL () = 0;

    virtual void mapComplete (uint256 const& hash,
        std::shared_ptr<SHAMap> const& map, bool acquired) = 0;

    virtual void timerEntry () = 0;

    virtual bool peerPosition (LedgerProposal::ref) = 0;

    /** Simulate the consensus process without any network traffic.

        The end result, is that consensus begins and completes as if everyone
        had agreed with whatever we propose.

        This function is only called from the rpc "ledger_accept" path with the
        server in standalone mode and SHOULD NOT be used during the normal
        consensus process.
    */
    virtual void simulate () = 0;
};

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (
    int previousProposers, int previousConvergeTime,
    InboundTransactions& inboundTransactions, LocalTxs& localtx,
    LedgerHash const & prevLCLHash, Ledger::ref previousLedger,
        std::uint32_t closeTime, FeeVote& feeVote);

void
applyTransactions(std::shared_ptr<SHAMap> const& set, Ledger::ref applyLedger,
                  Ledger::ref checkLedger,
                  CanonicalTXSet& retriableTransactions, bool openLgr);

} // divvy

#endif
