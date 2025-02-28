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

namespace divvy {
namespace path {

TER PathCursor::advanceNode (STAmount const& amount, bool reverse) const
{
    bool multi = multiQuality_ || amount == zero;

    // If the multiQuality_ is unchanged, use the PathCursor we're using now.
    if (multi == multiQuality_)
        return advanceNode (reverse);

    // Otherwise, use a new PathCursor with the new multiQuality_.
    PathCursor withMultiQuality {divvyCalc_, pathState_, multi, nodeIndex_};
    return withMultiQuality.advanceNode (reverse);
}

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.
//
TER PathCursor::advanceNode (bool const bReverse) const
{
    TER resultCode = tesSUCCESS;

    // Taker is the active party against an offer in the ledger - the entity
    // that is taking advantage of an offer in the order book.
    WriteLog (lsTRACE, DivvyCalc)
            << "advanceNode: TakerPays:"
            << node().saTakerPays << " TakerGets:" << node().saTakerGets;

    int loopCount = 0;

    do
    {
        // VFALCO NOTE Why not use a for() loop?
        // VFALCO TODO The limit on loop iterations puts an
        //             upper limit on the number of different quality
        // levels (ratio of pay:get) that will be considered for one path.
        // Changing this value has repercusssions on validation and consensus.
        //
        if (++loopCount > NODE_ADVANCE_MAX_LOOPS)
        {
            WriteLog (lsWARNING, DivvyCalc) << "Loop count exceeded";
            return tefEXCEPTION;
        }

        bool bDirectDirDirty = node().directory.initialize (
            {previousNode().issue_, node().issue_},
            ledger());

        if (auto advance = node().directory.advance (ledger()))
        {
            bDirectDirDirty = true;
            if (advance == NodeDirectory::NEW_QUALITY)
            {
                // We didn't run off the end of this order book and found
                // another quality directory.
                WriteLog (lsTRACE, DivvyCalc)
                    << "advanceNode: Quality advance: node.directory.current="
                    << node().directory.current;
            }
            else if (bReverse)
            {
                WriteLog (lsTRACE, DivvyCalc)
                    << "advanceNode: No more offers.";

                node().offerIndex_ = 0;
                break;
            }
            else
            {
                // No more offers. Should be done rather than fall off end of
                // book.
                WriteLog (lsWARNING, DivvyCalc)
                    << "advanceNode: Unreachable: "
                    << "Fell off end of order book.";
                // FIXME: why?
                return telFAILED_PROCESSING;
            }
        }

        if (bDirectDirDirty)
        {
            // Our quality changed since last iteration.
            // Use the rate from the directory.
            node().saOfrRate = amountFromQuality (
                getQuality (node().directory.current));
            // For correct ratio
            node().uEntry = 0;
            node().bEntryAdvance   = true;

            WriteLog (lsTRACE, DivvyCalc)
                << "advanceNode: directory dirty: node.saOfrRate="
                << node().saOfrRate;
        }

        if (!node().bEntryAdvance)
        {
            if (node().bFundsDirty)
            {
                // We were called again probably merely to update structure
                // variables.
                node().saTakerPays
                        = node().sleOffer->getFieldAmount (sfTakerPays);
                node().saTakerGets
                        = node().sleOffer->getFieldAmount (sfTakerGets);

                // Funds left.
                node().saOfferFunds = funds (ledger(),
                    node().offerOwnerAccount_,
                    node().saTakerGets,
                    fhZERO_IF_FROZEN);
                node().bFundsDirty = false;

                WriteLog (lsTRACE, DivvyCalc)
                    << "advanceNode: funds dirty: node().saOfrRate="
                    << node().saOfrRate;
            }
            else
            {
                WriteLog (lsTRACE, DivvyCalc) << "advanceNode: as is";
            }
        }
        else if (!ledger().dirNext (
            node().directory.current,
            node().directory.ledgerEntry,
            node().uEntry,
            node().offerIndex_))
            // This is the only place that offerIndex_ changes.
        {
            // Failed to find an entry in directory.
            // Do another cur directory iff multiQuality_
            if (multiQuality_)
            {
                // We are allowed to process multiple qualities if this is the
                // only path.
                WriteLog (lsTRACE, DivvyCalc)
                    << "advanceNode: next quality";
                node().directory.advanceNeeded  = true;  // Process next quality.
            }
            else if (!bReverse)
            {
                // We didn't run dry going backwards - why are we running dry
                // going forwards - this should be impossible!
                // TODO(tom): these warnings occur in production!  They
                // shouldn't.
                WriteLog (lsWARNING, DivvyCalc)
                    << "advanceNode: unreachable: ran out of offers";
                return telFAILED_PROCESSING;
            }
            else
            {
                // Ran off end of offers.
                node().bEntryAdvance   = false;        // Done.
                node().offerIndex_ = 0;            // Report no more entries.
            }
        }
        else
        {
            // Got a new offer.
            node().sleOffer = ledger().entryCache (
                ltOFFER, node().offerIndex_);

            if (!node().sleOffer)
            {
                // Corrupt directory that points to an entry that doesn't exist.
                // This has happened in production.
                WriteLog (lsWARNING, DivvyCalc) <<
                    "Missing offer in directory";
                node().bEntryAdvance = true;
            }
            else
            {
                node().offerOwnerAccount_
                        = node().sleOffer->getFieldAccount160 (sfAccount);
                node().saTakerPays
                        = node().sleOffer->getFieldAmount (sfTakerPays);
                node().saTakerGets
                        = node().sleOffer->getFieldAmount (sfTakerGets);

                AccountIssue const accountIssue (
                    node().offerOwnerAccount_, node().issue_);

                WriteLog (lsTRACE, DivvyCalc)
                    << "advanceNode: offerOwnerAccount_="
                    << to_string (node().offerOwnerAccount_)
                    << " node.saTakerPays=" << node().saTakerPays
                    << " node.saTakerGets=" << node().saTakerGets
                    << " node.offerIndex_=" << node().offerIndex_;

                if (node().sleOffer->isFieldPresent (sfExpiration) &&
                    (node().sleOffer->getFieldU32 (sfExpiration) <=
                     ledger().getLedger ()->
                     getParentCloseTimeNC ()))
                {
                    // Offer is expired.
                    WriteLog (lsTRACE, DivvyCalc)
                        << "advanceNode: expired offer";
                    divvyCalc_.permanentlyUnfundedOffers_.insert(
                        node().offerIndex_);
                    continue;
                }

                if (node().saTakerPays <= zero || node().saTakerGets <= zero)
                {
                    // Offer has bad amounts. Offers should never have a bad
                    // amounts.
                    auto const index = node().offerIndex_;
                    if (bReverse)
                    {
                        // Past internal error, offer had bad amounts.
                        // This has occurred in production.
                        WriteLog (lsWARNING, DivvyCalc)
                            << "advanceNode: PAST INTERNAL ERROR"
                            << " REVERSE: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node().saTakerPays
                            << " node.saTakerGets=" << node().saTakerGets;

                        // Mark offer for always deletion.
                        divvyCalc_.permanentlyUnfundedOffers_.insert (
                            node().offerIndex_);
                    }
                    else if (divvyCalc_.permanentlyUnfundedOffers_.find (index)
                             != divvyCalc_.permanentlyUnfundedOffers_.end ())
                    {
                        // Past internal error, offer was found failed to place
                        // this in permanentlyUnfundedOffers_.
                        // Just skip it. It will be deleted.
                        WriteLog (lsDEBUG, DivvyCalc)
                            << "advanceNode: PAST INTERNAL ERROR "
                            << " FORWARD CONFIRM: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node().saTakerPays
                            << " node.saTakerGets=" << node().saTakerGets;

                    }
                    else
                    {
                        // Reverse should have previously put bad offer in list.
                        // An internal error previously left a bad offer.
                        WriteLog (lsWARNING, DivvyCalc)
                            << "advanceNode: INTERNAL ERROR"

                            <<" FORWARD NEWLY FOUND: OFFER NON-POSITIVE:"
                            << " node.saTakerPays=" << node().saTakerPays
                            << " node.saTakerGets=" << node().saTakerGets;

                        // Don't process at all, things are in an unexpected
                        // state for this transactions.
                        resultCode = tefEXCEPTION;
                    }

                    continue;
                }

                // Allowed to access source from this node?
                //
                // XXX This can get called multiple times for same source in a
                // row, caching result would be nice.
                //
                // XXX Going forward could we fund something with a worse
                // quality which was previously skipped? Might need to check
                // quality.
                auto itForward = pathState_.forward().find (accountIssue);
                const bool bFoundForward =
                        itForward != pathState_.forward().end ();

                // Only allow a source to be used once, in the first node
                // encountered from initial path scan.  This prevents
                // conflicting uses of the same balance when going reverse vs
                // forward.
                if (bFoundForward &&
                    itForward->second != nodeIndex_ &&
                    node().offerOwnerAccount_ != node().issue_.account)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, DivvyCalc)
                        << "advanceNode: temporarily unfunded offer"
                        << " (forward)";
                    continue;
                }

                // This is overly strict. For contributions to past. We should
                // only count source if actually used.
                auto itReverse = pathState_.reverse().find (accountIssue);
                bool bFoundReverse = itReverse != pathState_.reverse().end ();

                // For this quality increment, only allow a source to be used
                // from a single node, in the first node encountered from
                // applying offers in reverse.
                if (bFoundReverse &&
                    itReverse->second != nodeIndex_ &&
                    node().offerOwnerAccount_ != node().issue_.account)
                {
                    // Temporarily unfunded. Another node uses this source,
                    // ignore in this offer.
                    WriteLog (lsTRACE, DivvyCalc)
                        << "advanceNode: temporarily unfunded offer"
                        <<" (reverse)";
                    continue;
                }

                // Determine if used in past.
                // We only need to know if it might need to be marked unfunded.
                auto itPast = divvyCalc_.mumSource_.find (accountIssue);
                bool bFoundPast = (itPast != divvyCalc_.mumSource_.end ());

                // Only the current node is allowed to use the source.

                node().saOfferFunds = funds(ledger(),
                    node().offerOwnerAccount_,
                    node().saTakerGets,
                    fhZERO_IF_FROZEN);
                // Funds held.

                if (node().saOfferFunds <= zero)
                {
                    // Offer is unfunded.
                    WriteLog (lsTRACE, DivvyCalc)
                        << "advanceNode: unfunded offer";

                    if (bReverse && !bFoundReverse && !bFoundPast)
                    {
                        // Never mentioned before, clearly just: found unfunded.
                        // That is, even if this offer fails due to fill or kill
                        // still do deletions.
                        // Mark offer for always deletion.
                        divvyCalc_.permanentlyUnfundedOffers_.insert (node().offerIndex_);
                    }
                    else
                    {
                        // Moving forward, don't need to insert again
                        // Or, already found it.
                    }

                    // YYY Could verify offer is correct place for unfundeds.
                    continue;
                }

                if (bReverse            // Need to remember reverse mention.
                    && !bFoundPast      // Not mentioned in previous passes.
                    && !bFoundReverse)  // New to pass.
                {
                    // Consider source mentioned by current path state.
                    WriteLog (lsTRACE, DivvyCalc)
                        << "advanceNode: remember="
                        <<  node().offerOwnerAccount_
                        << "/"
                        << node().issue_;

                    pathState_.insertReverse (accountIssue, nodeIndex_);
                }

                node().bFundsDirty = false;
                node().bEntryAdvance = false;
            }
        }
    }
    while (resultCode == tesSUCCESS &&
           (node().bEntryAdvance || node().directory.advanceNeeded));

    if (resultCode == tesSUCCESS)
    {
        WriteLog (lsTRACE, DivvyCalc)
            << "advanceNode: node.offerIndex_=" << node().offerIndex_;
    }
    else
    {
        WriteLog (lsDEBUG, DivvyCalc)
            << "advanceNode: resultCode=" << transToken (resultCode);
    }

    return resultCode;
}

}  // path
}  // divvy
