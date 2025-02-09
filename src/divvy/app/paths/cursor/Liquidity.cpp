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
#include <divvy/app/paths/cursor/DivvyLiquidity.h>
#include <divvy/basics/Log.h>
#include <tuple>

namespace divvy {
namespace path {

TER PathCursor::liquidity (LedgerEntrySet const& lesCheckpoint) const
{
    TER resultCode = tecPATH_DRY;
    PathCursor pc = *this;

    // duplicate
    reconstruct(divvyCalc_.mActiveLedger, lesCheckpoint);

    for (pc.nodeIndex_ = pc.nodeSize(); pc.nodeIndex_--; )
    {
        WriteLog (lsTRACE, DivvyCalc)
            << "reverseLiquidity>"
            << " nodeIndex=" << pc.nodeIndex_
            << ".issue_.account=" << to_string (pc.node().issue_.account);

        resultCode  = pc.reverseLiquidity();

        WriteLog (lsTRACE, DivvyCalc)
            << "reverseLiquidity< "
            << "nodeIndex=" << pc.nodeIndex_
            << " resultCode=" << transToken (resultCode)
            << " transferRate_=" << pc.node().transferRate_
            << "/" << resultCode;

        if (resultCode != tesSUCCESS)
            break;
    }

    // VFALCO-FIXME this generates errors
    // WriteLog (lsTRACE, DivvyCalc)
    //     << "nextIncrement: Path after reverse: " << pathState_.getJson ();

    if (resultCode != tesSUCCESS)
        return resultCode;

    // Do forward.
    // duplicate
    reconstruct(divvyCalc_.mActiveLedger, lesCheckpoint);

    for (pc.nodeIndex_ = 0; pc.nodeIndex_ < pc.nodeSize(); ++pc.nodeIndex_)
    {
        WriteLog (lsTRACE, DivvyCalc)
            << "forwardLiquidity> nodeIndex=" << nodeIndex_;

        resultCode = pc.forwardLiquidity();
        if (resultCode != tesSUCCESS)
            return resultCode;

        WriteLog (lsTRACE, DivvyCalc)
            << "forwardLiquidity<"
            << " nodeIndex:" << pc.nodeIndex_
            << " resultCode:" << resultCode;

        if (pathState_.isDry())
            resultCode = tecPATH_DRY;
    }
    return resultCode;
}

} // path
} // divvy
