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

#ifndef RIPPLE_APP_PATHS_CURSOR_PATHCURSOR_H_INCLUDED
#define RIPPLE_APP_PATHS_CURSOR_PATHCURSOR_H_INCLUDED

#include <divvy/app/paths/DivvyCalc.h>

namespace divvy {
namespace path {

// The next section contains methods that compute the liquidity along a path,
// either backward or forward.
//
// We need to do these computations twice - once backward to figure out the
// maximum possible liqiudity along a path, and then forward to compute the
// actual liquidity of the paths we actually chose.
//
// Some of these routines use recursion to loop over all nodes in a path.
// TODO(tom): replace this recursion with a loop.

class PathCursor
{
public:
    PathCursor(
        DivvyCalc& divvyCalc,
        PathState& pathState,
        bool multiQuality,
        NodeIndex nodeIndex = 0)
            : divvyCalc_(divvyCalc),
              pathState_(pathState),
              multiQuality_(multiQuality),
              nodeIndex_(restrict(nodeIndex))
    {
    }

    void nextIncrement(LedgerEntrySet const& checkpoint) const;

private:
    PathCursor(PathCursor const&) = default;

    PathCursor increment(int delta = 1) const
    {
        return {divvyCalc_, pathState_, multiQuality_, nodeIndex_ + delta};
    }

    TER liquidity(LedgerEntrySet const& lesCheckpoint) const;
    TER reverseLiquidity () const;
    TER forwardLiquidity () const;

    TER forwardLiquidityForAccount () const;
    TER reverseLiquidityForOffer () const;
    TER forwardLiquidityForOffer () const;
    TER reverseLiquidityForAccount () const;

    // To send money out of an account.
    /** advanceNode advances through offers in an order book.
        If needed, advance to next funded offer.
     - Automatically advances to first offer.
     --> bEntryAdvance: true, to advance to next entry. false, recalculate.
      <-- uOfferIndex : 0=end of list.
    */
    TER advanceNode (bool reverse) const;
    TER advanceNode (STAmount const& amount, bool reverse) const;

    // To deliver from an order book, when computing
    TER deliverNodeReverse (
        AccountID const& uOutAccountID,
        STAmount const& saOutReq,
        STAmount& saOutAct) const;

    TER deliverNodeForward (
        AccountID const& uInAccountID,
        STAmount const& saInReq,
        STAmount& saInAct,
        STAmount& saInFees) const;

    LedgerEntrySet& ledger() const
    {
        return divvyCalc_.mActiveLedger;
    }

    NodeIndex nodeSize() const
    {
        return pathState_.nodes().size();
    }

    NodeIndex restrict(NodeIndex i) const
    {
        return std::min (i, nodeSize() - 1);
    }

    Node& node(NodeIndex i) const
    {
        return pathState_.nodes()[i];
    }

    Node& node() const
    {
        return node (nodeIndex_);
    }

    Node& previousNode() const
    {
        return node (restrict (nodeIndex_ - 1));
    }

    Node& nextNode() const
    {
        return node (restrict (nodeIndex_ + 1));
    }

    DivvyCalc& divvyCalc_;
    PathState& pathState_;
    bool multiQuality_;
    NodeIndex nodeIndex_;
};

} // path
} // divvy

#endif
