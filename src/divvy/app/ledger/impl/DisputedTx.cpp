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
#include <divvy/app/ledger/impl/DisputedTx.h>
#include <divvy/app/ledger/LedgerTiming.h>
#include <divvy/basics/Log.h>
#include <divvy/json/to_string.h>

namespace divvy {

// Track a peer's yes/no vote on a particular disputed transaction
void DisputedTx::setVote (NodeID const& peer, bool votesYes)
{
    auto res = mVotes.insert (std::make_pair (peer, votesYes));

    // new vote
    if (res.second)
    {
        if (votesYes)
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                    << "Peer " << peer << " votes YES on " << mTransactionID;
            ++mYays;
        }
        else
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                    << "Peer " << peer << " votes NO on " << mTransactionID;
            ++mNays;
        }
    }
    // changes vote to yes
    else if (votesYes && !res.first->second)
    {
        WriteLog (lsDEBUG, LedgerConsensus)
                << "Peer " << peer << " now votes YES on " << mTransactionID;
        --mNays;
        ++mYays;
        res.first->second = true;
    }
    // changes vote to no
    else if (!votesYes && res.first->second)
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Peer " << peer
                                            << " now votes NO on " << mTransactionID;
        ++mNays;
        --mYays;
        res.first->second = false;
    }
}

// Remove a peer's vote on this disputed transasction
void DisputedTx::unVote (NodeID const& peer)
{
    auto it = mVotes.find (peer);

    if (it != mVotes.end ())
    {
        if (it->second)
            --mYays;
        else
            --mNays;

        mVotes.erase (it);
    }
}

bool DisputedTx::updateVote (int percentTime, bool proposing)
{
    // VFALCO TODO Give the return value a descriptive local variable name
    //             and don't return from the middle.

    if (mOurVote && (mNays == 0))
        return false;

    if (!mOurVote && (mYays == 0))
        return false;

    bool newPosition;
    int weight;

    if (proposing) // give ourselves full weight
    {
        // This is basically the percentage of nodes voting 'yes' (including us)
        weight = (mYays * 100 + (mOurVote ? 100 : 0)) / (mNays + mYays + 1);

        // VFALCO TODO Rename these macros and turn them into language
        //             constructs.  consolidate them into a class that collects
        //             all these related values.
        //
        // To prevent avalanche stalls, we increase the needed weight slightly
        // over time.
        if (percentTime < AV_MID_CONSENSUS_TIME)
            newPosition = weight >  AV_INIT_CONSENSUS_PCT;
        else if (percentTime < AV_LATE_CONSENSUS_TIME)
            newPosition = weight > AV_MID_CONSENSUS_PCT;
        else if (percentTime < AV_STUCK_CONSENSUS_TIME)
            newPosition = weight > AV_LATE_CONSENSUS_PCT;
        else
            newPosition = weight > AV_STUCK_CONSENSUS_PCT;
    }
    else // don't let us outweigh a proposing node, just recognize consensus
    {
        weight = -1;
        newPosition = mYays > mNays;
    }

    if (newPosition == mOurVote)
    {
        WriteLog (lsINFO, LedgerConsensus)
                << "No change (" << (mOurVote ? "YES" : "NO") << ") : weight "
                << weight << ", percent " << percentTime;
        WriteLog (lsDEBUG, LedgerConsensus) << getJson ();
        return false;
    }

    mOurVote = newPosition;
    WriteLog (lsDEBUG, LedgerConsensus)
            << "We now vote " << (mOurVote ? "YES" : "NO")
            << " on " << mTransactionID;
    WriteLog (lsDEBUG, LedgerConsensus) << getJson ();
    return true;
}

Json::Value DisputedTx::getJson ()
{
    Json::Value ret (Json::objectValue);

    ret["yays"] = mYays;
    ret["nays"] = mNays;
    ret["our_vote"] = mOurVote;

    if (!mVotes.empty ())
    {
        Json::Value votesj (Json::objectValue);
        for (auto& vote : mVotes)
            votesj[to_string (vote.first)] = vote.second;
        ret["votes"] = votesj;
    }

    return ret;
}

} // divvy
