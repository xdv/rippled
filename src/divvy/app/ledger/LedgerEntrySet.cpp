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
#include <divvy/protocol/Quality.h>
#include <divvy/app/ledger/LedgerEntrySet.h>
#include <divvy/app/ledger/DeferredCredits.h>
#include <divvy/basics/Log.h>
#include <divvy/basics/StringUtilities.h>
#include <divvy/json/to_string.h>
#include <divvy/protocol/JsonFields.h>
#include <divvy/protocol/Indexes.h>

namespace divvy {

// #define META_DEBUG

// VFALCO TODO Replace this macro with a documented language constant
//        NOTE Is this part of the protocol?
//
#define DIR_NODE_MAX  32

LedgerEntrySet::LedgerEntrySet (LedgerEntrySet const& other)
    : mLedger(other.mLedger)
    , mEntries(other.mEntries)
    , mDeferredCredits(other.mDeferredCredits)
    , mSet(other.mSet)
    // VFALCO NOTE This is a change in behavior,
    //        previous version set tapNONE
    , mParams(other.mParams)
    , mSeq(other.mSeq + 1)
{
}

LedgerEntrySet::LedgerEntrySet(Ledger::ref ledger,
    uint256 const& transactionID,
        std::uint32_t ledgerID,
            TransactionEngineParams params)
    : mLedger(ledger)
    , mParams(params)
{
    mSet.init (transactionID, ledgerID);
}

LedgerEntrySet::LedgerEntrySet (Ledger::ref ledger,
    TransactionEngineParams tep, bool immutable)
    : mLedger (ledger)
    , mParams (tep)
    , mImmutable (immutable)
{
}

void LedgerEntrySet::apply()
{
    // Write back the account states
    for (auto const& item : mEntries)
    {
        // VFALCO TODO rvalue move the mEntry, make
        //             sure the mNodes is not used after
        //             this function is called.
        auto sle = item.second.mEntry;
        switch (item.second.mAction)
        {
        case taaNONE:
            assert (false);
            break;

        case taaCACHED:
            break;

        case taaCREATE:
            // VFALCO Is this logging necessary anymore?
            WriteLog (lsDEBUG, LedgerEntrySet) <<
                "applyTransaction: taaCREATE: " << sle->getText ();
            mLedger->insert(*sle);
            break;

        case taaMODIFY:
            WriteLog (lsDEBUG, LedgerEntrySet) <<
                "applyTransaction: taaMODIFY: " << sle->getText ();
            mLedger->replace(*sle);
            break;

        case taaDELETE:
            WriteLog (lsDEBUG, LedgerEntrySet) <<
                "applyTransaction: taaDELETE: " << sle->getText ();
            mLedger->erase(item.first);
            break;
        }
    }
}

void LedgerEntrySet::swapWith (LedgerEntrySet& e)
{
    using std::swap;
    swap (mLedger, e.mLedger);
    mEntries.swap (e.mEntries);
    mSet.swap (e.mSet);
    swap (mParams, e.mParams);
    swap (mSeq, e.mSeq);
    swap (mDeferredCredits, e.mDeferredCredits);
}

// Find an entry in the set.  If it has the wrong sequence number, copy it and update the sequence number.
// This is basically: copy-on-read.
SLE::pointer LedgerEntrySet::getEntry (uint256 const& index, Action& action)
{
    auto it = mEntries.find (index);

    if (it == mEntries.end ())
    {
        action = taaNONE;
        return SLE::pointer ();
    }

    if (it->second.mSeq != mSeq)
    {
        assert (it->second.mSeq < mSeq);
        it->second.mEntry = std::make_shared<SLE> (*it->second.mEntry);
        it->second.mSeq = mSeq;
    }

    action = it->second.mAction;
    return it->second.mEntry;
}

SLE::pointer LedgerEntrySet::entryCache (LedgerEntryType letType, uint256 const& key)
{
    assert(key.isNonZero ());
    assert (mLedger);
    SLE::pointer sle;

    if (key.isNonZero ())
    {
        Action action;
        sle = getEntry (key, action);

        if (! sle)
        {
            assert (action != taaDELETE);
            if (mImmutable)
            {
                // VFALCO NOTE We'd like all immutable callers to go through
                //             entryCacheI, then we can avoid calling getSLEi.
                sle = mLedger->getSLEi(key);
            }
            else
            {
                sle = mLedger->fetch(key, letType);
            }

            if (sle)
                entryCache (sle);
        }
        else if (action == taaDELETE)
        {
            sle = nullptr;
        }
    }

    return sle;
}

std::shared_ptr<SLE const>
LedgerEntrySet::entryCacheI (LedgerEntryType letType, uint256 const& key)
{
    return entryCache(letType, key);
}

void LedgerEntrySet::entryCache (SLE::ref sle)
{
    assert (mLedger);
    assert (sle->isMutable () || mImmutable); // Don't put an immutable SLE in a mutable LES
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), Item (sle, taaCACHED, mSeq)));
        return;
    }

    switch (it->second.mAction)
    {
    case taaCACHED:
        assert (sle == it->second.mEntry);
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        return;

    default:
        throw std::runtime_error ("Cache after modify/delete/create");
    }
}

void LedgerEntrySet::entryCreate (SLE::ref sle)
{
    assert (mLedger && !mImmutable);
    assert (sle->isMutable ());
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), Item (sle, taaCREATE, mSeq)));
        return;
    }

    switch (it->second.mAction)
    {

    case taaDELETE:
        WriteLog (lsDEBUG, LedgerEntrySet) << "Create after Delete = Modify";
        it->second.mEntry = sle;
        it->second.mAction = taaMODIFY;
        it->second.mSeq = mSeq;
        break;

    case taaMODIFY:
        throw std::runtime_error ("Create after modify");

    case taaCREATE:
        throw std::runtime_error ("Create after create"); // This could be made to work

    case taaCACHED:
        throw std::runtime_error ("Create after cache");

    default:
        throw std::runtime_error ("Unknown taa");
    }

    assert (it->second.mSeq == mSeq);
}

void LedgerEntrySet::entryModify (SLE::ref sle)
{
    assert (sle->isMutable () && !mImmutable);
    assert (mLedger);
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), Item (sle, taaMODIFY, mSeq)));
        return;
    }

    assert (it->second.mSeq == mSeq);
    assert (it->second.mEntry == sle);

    switch (it->second.mAction)
    {
    case taaCACHED:
        it->second.mAction  = taaMODIFY;

        // Fall through

    case taaCREATE:
    case taaMODIFY:
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        break;

    case taaDELETE:
        throw std::runtime_error ("Modify after delete");

    default:
        throw std::runtime_error ("Unknown taa");
    }
}

void LedgerEntrySet::entryDelete (SLE::ref sle)
{
    assert (sle->isMutable () && !mImmutable);
    assert (mLedger);
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        assert (false); // deleting an entry not cached?
        mEntries.insert (std::make_pair (sle->getIndex (), Item (sle, taaDELETE, mSeq)));
        return;
    }

    assert (it->second.mSeq == mSeq);
    assert (it->second.mEntry == sle);

    switch (it->second.mAction)
    {
    case taaCACHED:
    case taaMODIFY:
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        it->second.mAction  = taaDELETE;
        break;

    case taaCREATE:
        mEntries.erase (it);
        break;

    case taaDELETE:
        break;

    default:
        throw std::runtime_error ("Unknown taa");
    }
}

Json::Value LedgerEntrySet::getJson (int) const
{
    Json::Value ret (Json::objectValue);

    Json::Value nodes (Json::arrayValue);

    for (auto it = mEntries.begin (), end = mEntries.end (); it != end; ++it)
    {
        Json::Value entry (Json::objectValue);
        entry[jss::node] = to_string (it->first);

        switch (it->second.mEntry->getType ())
        {
        case ltINVALID:
            entry[jss::type] = "invalid";
            break;

        case ltACCOUNT_ROOT:
            entry[jss::type] = "acccount_root";
            break;

        case ltDIR_NODE:
            entry[jss::type] = "dir_node";
            break;

        case ltRIPPLE_STATE:
            entry[jss::type] = "divvy_state";
            break;

        case ltNICKNAME:
            entry[jss::type] = "nickname";
            break;

        case ltOFFER:
            entry[jss::type] = "offer";
            break;

        default:
            assert (false);
        }

        switch (it->second.mAction)
        {
        case taaCACHED:
            entry[jss::action] = "cache";
            break;

        case taaMODIFY:
            entry[jss::action] = "modify";
            break;

        case taaDELETE:
            entry[jss::action] = "delete";
            break;

        case taaCREATE:
            entry[jss::action] = "create";
            break;

        default:
            assert (false);
        }

        nodes.append (entry);
    }

    ret[jss::nodes] = nodes;

    ret[jss::metaData] = mSet.getJson (0);

    return ret;
}

SLE::pointer LedgerEntrySet::getForMod (uint256 const& node, Ledger::ref ledger,
                                        NodeToLedgerEntry& newMods)
{
    auto it = mEntries.find (node);

    if (it != mEntries.end ())
    {
        if (it->second.mAction == taaDELETE)
        {
            WriteLog (lsFATAL, LedgerEntrySet) << "Trying to thread to deleted node";
            return SLE::pointer ();
        }

        if (it->second.mAction == taaCACHED)
            it->second.mAction = taaMODIFY;

        if (it->second.mSeq != mSeq)
        {
            it->second.mEntry =
                std::make_shared<SLE> (*it->second.mEntry);
            it->second.mSeq = mSeq;
        }

        return it->second.mEntry;
    }

    auto me = newMods.find (node);

    if (me != newMods.end ())
    {
        assert (me->second);
        return me->second;
    }

    auto sle = ledger->fetch(node);
    if (! sle)
        return nullptr;
    newMods.insert (std::make_pair (node, sle));
    return sle;
}

bool LedgerEntrySet::threadTx (DivvyAddress const& threadTo, Ledger::ref ledger,
                               NodeToLedgerEntry& newMods)
{
    SLE::pointer sle = getForMod (
        getAccountRootIndex (threadTo.getAccountID ()), ledger, newMods);

#ifdef META_DEBUG
    WriteLog (lsTRACE, LedgerEntrySet) << "Thread to " << threadTo.getAccountID ();
#endif

    if (!sle)
    {
        WriteLog (lsFATAL, LedgerEntrySet) <<
            "Threading to non-existent account: " << threadTo.humanAccountID ();
        assert (false);
        return false;
    }

    return threadTx (sle, ledger, newMods);
}

bool LedgerEntrySet::threadTx (SLE::ref threadTo, Ledger::ref ledger,
                               NodeToLedgerEntry& newMods)
{
    // node = the node that was modified/deleted/created
    // threadTo = the node that needs to know
    uint256 prevTxID;
    std::uint32_t prevLgrID;

    if (!threadTo->thread (mSet.getTxID (), mSet.getLgrSeq (), prevTxID, prevLgrID))
        return false;

    if (prevTxID.isZero () ||
            TransactionMetaSet::thread (mSet.getAffectedNode (threadTo, sfModifiedNode), prevTxID, prevLgrID))
        return true;

    assert (false);
    return false;
}

bool LedgerEntrySet::threadOwners(
    std::shared_ptr<SLE const> const& node,
        Ledger::ref ledger, NodeToLedgerEntry& newMods)
{
    // thread new or modified node to owner or owners
    if (node->hasOneOwner ()) // thread to owner's account
    {
#ifdef META_DEBUG
        WriteLog (lsTRACE, LedgerEntrySet) << "Thread to single owner";
#endif
        return threadTx (node->getOwner (), ledger, newMods);
    }
    else if (node->hasTwoOwners ()) // thread to owner's accounts
    {
#ifdef META_DEBUG
        WriteLog (lsTRACE, LedgerEntrySet) << "Thread to two owners";
#endif
        return
            threadTx (node->getFirstOwner (), ledger, newMods) &&
            threadTx (node->getSecondOwner (), ledger, newMods);
    }
    else
        return false;
}

void LedgerEntrySet::calcRawMeta (Serializer& s, TER result, std::uint32_t index)
{
    // calculate the raw meta data and return it. This must be called before the set is committed

    // Entries modified only as a result of building the transaction metadata
    NodeToLedgerEntry newMod;

    for (auto& it : mEntries)
    {
        auto type = &sfGeneric;

        switch (it.second.mAction)
        {
        case taaMODIFY:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Modified Node " << it.first;
#endif
            type = &sfModifiedNode;
            break;

        case taaDELETE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Deleted Node " << it.first;
#endif
            type = &sfDeletedNode;
            break;

        case taaCREATE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Created Node " << it.first;
#endif
            type = &sfCreatedNode;
            break;

        default: // ignore these
            break;
        }

        if (type == &sfGeneric)
            continue;

        std::shared_ptr<SLE const> const origNode =
            mLedger->getSLEi(it.first);
        SLE::pointer curNode = it.second.mEntry;

        if ((type == &sfModifiedNode) && (*curNode == *origNode))
            continue;

        std::uint16_t nodeType = curNode
            ? curNode->getFieldU16 (sfLedgerEntryType)
            : origNode->getFieldU16 (sfLedgerEntryType);

        mSet.setAffectedNode (it.first, *type, nodeType);

        if (type == &sfDeletedNode)
        {
            assert (origNode && curNode);
            threadOwners (origNode, mLedger, newMod); // thread transaction to owners

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // go through the original node for modified fields saved on modification
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.emplace_back (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(prevs));

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // go through the final node for final fields
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_DeleteFinal))
                    finals.emplace_back (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(finals));
        }
        else if (type == &sfModifiedNode)
        {
            assert (curNode && origNode);

            if (curNode->isThreadedType ()) // thread transaction to node it modified
                threadTx (curNode, mLedger, newMod);

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // search the original node for values saved on modify
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.emplace_back (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(prevs));

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // search the final node for values saved always
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_ChangeNew))
                    finals.emplace_back (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(finals));
        }
        else if (type == &sfCreatedNode) // if created, thread to owner(s)
        {
            assert (curNode && !origNode);
            threadOwners (curNode, mLedger, newMod);

            if (curNode->isThreadedType ()) // always thread to self
                threadTx (curNode, mLedger, newMod);

            STObject news (sfNewFields);
            for (auto const& obj : *curNode)
            {
                // save non-default values
                if (!obj.isDefault () && obj.getFName ().shouldMeta (SField::sMD_Create | SField::sMD_Always))
                    news.emplace_back (obj);
            }

            if (!news.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(news));
        }
        else assert (false);
    }

    // add any new modified nodes to the modification set
    for (auto& it : newMod)
        entryModify (it.second);

    mSet.addRaw (s, result, index);
    WriteLog (lsTRACE, LedgerEntrySet) << "Metadata:" << mSet.getJson (0);
}

bool LedgerEntrySet::dirIsEmpty (uint256 const& uRootIndex)
{
    std::uint64_t  uNodeDir = 0;

    SLE::pointer sleNode = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeDir));

    if (!sleNode)
        return true;

    if (!sleNode->getFieldV256 (sfIndexes).empty ())
        return false;

    // If there's another page, it must be non-empty
    return sleNode->getFieldU64 (sfIndexNext) == 0;
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER LedgerEntrySet::dirAdd (
    std::uint64_t&                          uNodeDir,
    uint256 const&                          uRootIndex,
    uint256 const&                          uLedgerIndex,
    std::function<void (SLE::ref, bool)>    fDescriber)
{
    WriteLog (lsTRACE, LedgerEntrySet) << "dirAdd:" <<
        " uRootIndex=" << to_string (uRootIndex) <<
        " uLedgerIndex=" << to_string (uLedgerIndex);

    SLE::pointer        sleNode;
    STVector256         svIndexes;
    SLE::pointer        sleRoot     = entryCache (ltDIR_NODE, uRootIndex);

    if (!sleRoot)
    {
        // No root, make it.
        sleRoot = std::make_shared<SLE>(ltDIR_NODE, uRootIndex);
        sleRoot->setFieldH256 (sfRootIndex, uRootIndex);
        entryCreate (sleRoot);
        fDescriber (sleRoot, true);
        sleNode     = sleRoot;
        uNodeDir    = 0;
    }
    else
    {
        uNodeDir    = sleRoot->getFieldU64 (sfIndexPrevious);       // Get index to last directory node.

        if (uNodeDir)
        {
            // Try adding to last node.
            sleNode     = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeDir));

            assert (sleNode);
        }
        else
        {
            // Try adding to root.  Didn't have a previous set to the last node.
            sleNode     = sleRoot;
        }

        svIndexes   = sleNode->getFieldV256 (sfIndexes);

        if (DIR_NODE_MAX != svIndexes.size ())
        {
            // Add to current node.
            entryModify (sleNode);
        }
        // Add to new node.
        else if (!++uNodeDir)
        {
            return tecDIR_FULL;
        }
        else
        {
            // Have old last point to new node
            sleNode->setFieldU64 (sfIndexNext, uNodeDir);
            entryModify (sleNode);

            // Have root point to new node.
            sleRoot->setFieldU64 (sfIndexPrevious, uNodeDir);
            entryModify (sleRoot);

            // Create the new node.
            sleNode = std::make_shared<SLE>(
                ltDIR_NODE, getDirNodeIndex(uRootIndex, uNodeDir));
            sleNode->setFieldH256 (sfRootIndex, uRootIndex);
            entryCreate (sleNode);

            if (uNodeDir != 1)
                sleNode->setFieldU64 (sfIndexPrevious, uNodeDir - 1);

            fDescriber (sleNode, false);

            svIndexes   = STVector256 ();
        }
    }

    svIndexes.push_back (uLedgerIndex); // Append entry.
    sleNode->setFieldV256 (sfIndexes, svIndexes);   // Save entry.

    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:   creating: root: " << to_string (uRootIndex);
    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:  appending: Entry: " << to_string (uLedgerIndex);
    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:  appending: Node: " << strHex (uNodeDir);

    return tesSUCCESS;
}

// Ledger must be in a state for this to work.
TER LedgerEntrySet::dirDelete (
    const bool                      bKeepRoot,      // --> True, if we never completely clean up, after we overflow the root node.
    const std::uint64_t&            uNodeDir,       // --> Node containing entry.
    uint256 const&                  uRootIndex,     // --> The index of the base of the directory.  Nodes are based off of this.
    uint256 const&                  uLedgerIndex,   // --> Value to remove from directory.
    const bool                      bStable,        // --> True, not to change relative order of entries.
    const bool                      bSoft)          // --> True, uNodeDir is not hard and fast (pass uNodeDir=0).
{
    std::uint64_t       uNodeCur    = uNodeDir;
    SLE::pointer        sleNode     = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeCur));

    if (!sleNode)
    {
        WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: no such node:" <<
            " uRootIndex=" << to_string (uRootIndex) <<
            " uNodeDir=" << strHex (uNodeDir) <<
            " uLedgerIndex=" << to_string (uLedgerIndex);

        if (!bSoft)
        {
            assert (false);
            return tefBAD_LEDGER;
        }
        else if (uNodeDir < 20)
        {
            // Go the extra mile. Even if node doesn't exist, try the next node.

            return dirDelete (bKeepRoot, uNodeDir + 1, uRootIndex, uLedgerIndex, bStable, true);
        }
        else
        {
            return tefBAD_LEDGER;
        }
    }

    STVector256 svIndexes = sleNode->getFieldV256 (sfIndexes);

    auto it = std::find (svIndexes.begin (), svIndexes.end (), uLedgerIndex);

    if (svIndexes.end () == it)
    {
        if (!bSoft)
        {
            assert (false);
            WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: no such entry";
            return tefBAD_LEDGER;
        }

        if (uNodeDir < 20)
        {
            // Go the extra mile. Even if entry not in node, try the next node.
            return dirDelete (bKeepRoot, uNodeDir + 1, uRootIndex, uLedgerIndex,
                bStable, true);
        }

        return tefBAD_LEDGER;
    }

    // Remove the element.
    if (svIndexes.size () > 1)
    {
        if (bStable)
        {
            svIndexes.erase (it);
        }
        else
        {
            *it = svIndexes[svIndexes.size () - 1];
            svIndexes.resize (svIndexes.size () - 1);
        }
    }
    else
    {
        svIndexes.clear ();
    }

    sleNode->setFieldV256 (sfIndexes, svIndexes);
    entryModify (sleNode);

    if (svIndexes.empty ())
    {
        // May be able to delete nodes.
        std::uint64_t       uNodePrevious   = sleNode->getFieldU64 (sfIndexPrevious);
        std::uint64_t       uNodeNext       = sleNode->getFieldU64 (sfIndexNext);

        if (!uNodeCur)
        {
            // Just emptied root node.

            if (!uNodePrevious)
            {
                // Never overflowed the root node.  Delete it.
                entryDelete (sleNode);
            }
            // Root overflowed.
            else if (bKeepRoot)
            {
                // If root overflowed and not allowed to delete overflowed root node.
            }
            else if (uNodePrevious != uNodeNext)
            {
                // Have more than 2 nodes.  Can't delete root node.
            }
            else
            {
                // Have only a root node and a last node.
                SLE::pointer        sleLast = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeNext));

                assert (sleLast);

                if (sleLast->getFieldV256 (sfIndexes).empty ())
                {
                    // Both nodes are empty.

                    entryDelete (sleNode);  // Delete root.
                    entryDelete (sleLast);  // Delete last.
                }
                else
                {
                    // Have an entry, can't delete root node.
                }
            }
        }
        // Just emptied a non-root node.
        else if (uNodeNext)
        {
            // Not root and not last node. Can delete node.

            SLE::pointer        slePrevious = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodePrevious));

            assert (slePrevious);

            SLE::pointer        sleNext     = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeNext));

            assert (slePrevious);
            assert (sleNext);

            if (!slePrevious)
            {
                WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: previous node is missing";

                return tefBAD_LEDGER;
            }

            if (!sleNext)
            {
                WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: next node is missing";

                return tefBAD_LEDGER;
            }

            // Fix previous to point to its new next.
            slePrevious->setFieldU64 (sfIndexNext, uNodeNext);
            entryModify (slePrevious);

            // Fix next to point to its new previous.
            sleNext->setFieldU64 (sfIndexPrevious, uNodePrevious);
            entryModify (sleNext);

            entryDelete(sleNode);
        }
        // Last node.
        else if (bKeepRoot || uNodePrevious)
        {
            // Not allowed to delete last node as root was overflowed.
            // Or, have pervious entries preventing complete delete.
        }
        else
        {
            // Last and only node besides the root.
            SLE::pointer            sleRoot = entryCache (ltDIR_NODE, uRootIndex);

            assert (sleRoot);

            if (sleRoot->getFieldV256 (sfIndexes).empty ())
            {
                // Both nodes are empty.

                entryDelete (sleRoot);  // Delete root.
                entryDelete (sleNode);  // Delete last.
            }
            else
            {
                // Root has an entry, can't delete.
            }
        }
    }

    return tesSUCCESS;
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirFirst (
    uint256 const& uRootIndex,  // --> Root of directory.
    SLE::pointer& sleNode,      // <-- current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    sleNode     = entryCache (ltDIR_NODE, uRootIndex);
    uDirEntry   = 0;

    assert (sleNode);           // Never probe for directories.

    return LedgerEntrySet::dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirFirst (
    uint256 const& uRootIndex,  // --> Root of directory.
    std::shared_ptr<SLE const>& sleNode,      // <-- current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    sleNode     = entryCacheI (ltDIR_NODE, uRootIndex);
    uDirEntry   = 0;

    assert (sleNode);           // Never probe for directories.

    return LedgerEntrySet::dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirNext (
    uint256 const& uRootIndex,  // --> Root of directory
    SLE::pointer& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    STVector256             svIndexes   = sleNode->getFieldV256 (sfIndexes);

    assert (uDirEntry <= svIndexes.size ());

    if (uDirEntry >= svIndexes.size ())
    {
        std::uint64_t         uNodeNext   = sleNode->getFieldU64 (sfIndexNext);

        if (!uNodeNext)
        {
            uEntryIndex.zero ();

            return false;
        }
        else
        {
            SLE::pointer sleNext = entryCache (ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeNext));
            uDirEntry   = 0;

            if (!sleNext)
            { // This should never happen
                WriteLog (lsFATAL, LedgerEntrySet)
                        << "Corrupt directory: index:"
                        << uRootIndex << " next:" << uNodeNext;
                return false;
            }

            sleNode = sleNext;
            // TODO(tom): make this iterative.
            return dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
        }
    }

    uEntryIndex = svIndexes[uDirEntry++];

    WriteLog (lsTRACE, LedgerEntrySet) << "dirNext:" <<
        " uDirEntry=" << uDirEntry <<
        " uEntryIndex=" << uEntryIndex;

    return true;
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirNext (
    uint256 const& uRootIndex,  // --> Root of directory
    std::shared_ptr<SLE const>& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    STVector256 const svIndexes = sleNode->getFieldV256 (sfIndexes);

    assert (uDirEntry <= svIndexes.size ());

    if (uDirEntry >= svIndexes.size ())
    {
        std::uint64_t const uNodeNext   = sleNode->getFieldU64 (sfIndexNext);

        if (!uNodeNext)
        {
            uEntryIndex.zero ();

            return false;
        }
        else
        {
            auto const sleNext = entryCacheI(
                ltDIR_NODE, getDirNodeIndex (uRootIndex, uNodeNext));
            uDirEntry   = 0;

            if (!sleNext)
            { // This should never happen
                WriteLog (lsFATAL, LedgerEntrySet)
                        << "Corrupt directory: index:"
                        << uRootIndex << " next:" << uNodeNext;
                return false;
            }

            sleNode = sleNext;
            // TODO(tom): make this iterative.
            return dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
        }
    }

    uEntryIndex = svIndexes[uDirEntry++];

    WriteLog (lsTRACE, LedgerEntrySet) << "dirNext:" <<
        " uDirEntry=" << uDirEntry <<
        " uEntryIndex=" << uEntryIndex;

    return true;
}

uint256 LedgerEntrySet::getNextLedgerIndex (uint256 const& uHash)
{
    // find next node in ledger that isn't deleted by LES
    uint256 ledgerNext = uHash;
    std::map<uint256, Item>::const_iterator it;

    do
    {
        ledgerNext = mLedger->getNextLedgerIndex (ledgerNext);
        it  = mEntries.find (ledgerNext);
    }
    while ((it != mEntries.end ()) && (it->second.mAction == taaDELETE));

    // find next node in LES that isn't deleted
    for (it = mEntries.upper_bound (uHash); it != mEntries.end (); ++it)
    {
        // node found in LES, node found in ledger, return earliest
        if (it->second.mAction != taaDELETE)
            return (ledgerNext.isNonZero () && (ledgerNext < it->first)) ?
                    ledgerNext : it->first;
    }

    // nothing next in LES, return next ledger node
    return ledgerNext;
}

uint256 LedgerEntrySet::getNextLedgerIndex (
    uint256 const& uHash, uint256 const& uEnd)
{
    uint256 next = getNextLedgerIndex (uHash);

    if (next > uEnd)
        return uint256 ();

    return next;
}

TER LedgerEntrySet::offerDelete (SLE::pointer sleOffer)
{
    if (!sleOffer)
        return tesSUCCESS;

    auto offerIndex = sleOffer->getIndex ();
    auto owner = sleOffer->getFieldAccount160  (sfAccount);

    // Detect legacy directories.
    bool bOwnerNode = sleOffer->isFieldPresent (sfOwnerNode);
    std::uint64_t uOwnerNode = sleOffer->getFieldU64 (sfOwnerNode);
    uint256 uDirectory = sleOffer->getFieldH256 (sfBookDirectory);
    std::uint64_t uBookNode  = sleOffer->getFieldU64 (sfBookNode);

    TER terResult  = dirDelete (
        false, uOwnerNode,
        getOwnerDirIndex (owner), offerIndex, false, !bOwnerNode);
    TER terResult2 = dirDelete (
        false, uBookNode, uDirectory, offerIndex, true, false);

    if (tesSUCCESS == terResult)
        adjustOwnerCount(*this, entryCache(ltACCOUNT_ROOT,
            getAccountRootIndex(owner)), -1);

    entryDelete (sleOffer);

    return (terResult == tesSUCCESS) ? terResult2 : terResult;
}

// Return how much of issuer's currency IOUs that account holds.  May be
// negative.
// <-- IOU's account has of issuer.
STAmount LedgerEntrySet::divvyHolds (
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen)
{
    STAmount saBalance;
    SLE::pointer sleDivvyState = entryCache (ltRIPPLE_STATE,
        getDivvyStateIndex (account, issuer, currency));

    if (!sleDivvyState)
    {
        saBalance.clear ({currency, issuer});
    }
    else if ((zeroIfFrozen == fhZERO_IF_FROZEN) && isFrozen (account, currency, issuer))
    {
        saBalance.clear (IssueRef (currency, issuer));
    }
    else if (account > issuer)
    {
        saBalance   = sleDivvyState->getFieldAmount (sfBalance);
        saBalance.negate ();    // Put balance in account terms.

        saBalance.setIssuer (issuer);
    }
    else
    {
        saBalance   = sleDivvyState->getFieldAmount (sfBalance);

        saBalance.setIssuer (issuer);
    }

    return adjustedBalance(account, issuer, saBalance);
}

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
STAmount LedgerEntrySet::accountHolds (
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen)
{
    STAmount    saAmount;

    if (!currency)
    {
        SLE::pointer sleAccount = entryCache (ltACCOUNT_ROOT,
            getAccountRootIndex (account));
        std::uint64_t uReserve = mLedger->getReserve (
            sleAccount->getFieldU32 (sfOwnerCount));

        STAmount const saBalance = sleAccount->getFieldAmount (sfBalance);
        STAmount const saReserve (uReserve);

        if (saBalance < saReserve)
        {
            saAmount.clear ();
        }
        else
        {
            saAmount = saBalance - saReserve;
        }

        WriteLog (lsTRACE, LedgerEntrySet) << "accountHolds:" <<
            " account=" << to_string (account) <<
            " saAmount=" << saAmount.getFullText () <<
            " saBalance=" << saBalance.getFullText () <<
            " saReserve=" << saReserve.getFullText ();

        return adjustedBalance(account, issuer, saAmount);
    }
    else
    {
        saAmount = divvyHolds (account, currency, issuer, zeroIfFrozen);

        WriteLog (lsTRACE, LedgerEntrySet) << "accountHolds:" <<
            " account=" << to_string (account) <<
            " saAmount=" << saAmount.getFullText ();

        return saAmount;
    }

}

bool LedgerEntrySet::isGlobalFrozen (AccountID const& issuer)
{
    if (isXDV (issuer))
        return false;

    SLE::pointer sle = entryCache (ltACCOUNT_ROOT, getAccountRootIndex (issuer));
    if (sle && sle->isFlag (lsfGlobalFreeze))
        return true;

    return false;
}

// Can the specified account spend the specified currency issued by
// the specified issuer or does the freeze flag prohibit it?
bool LedgerEntrySet::isFrozen(
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
{
    if (isXDV (currency))
        return false;

    SLE::pointer sle = entryCache (ltACCOUNT_ROOT, getAccountRootIndex (issuer));
    if (sle && sle->isFlag (lsfGlobalFreeze))
        return true;

    if (issuer != account)
    {
        // Check if the issuer froze the line
        sle = entryCache (ltRIPPLE_STATE,
            getDivvyStateIndex (account, issuer, currency));
        if (sle && sle->isFlag ((issuer > account) ? lsfHighFreeze : lsfLowFreeze))
        {
            return true;
        }
    }

    return false;
}

TER LedgerEntrySet::trustCreate (
    const bool      bSrcHigh,
    AccountID const&  uSrcAccountID,
    AccountID const&  uDstAccountID,
    uint256 const&  uIndex,             // --> divvy state entry
    SLE::ref        sleAccount,         // --> the account being set.
    const bool      bAuth,              // --> authorize account.
    const bool      bNoDivvy,          // --> others cannot divvy through
    const bool      bFreeze,            // --> funds cannot leave
    STAmount const& saBalance,          // --> balance of account being set.
                                        // Issuer should be noAccount()
    STAmount const& saLimit,            // --> limit for account being set.
                                        // Issuer should be the account being set.
    const std::uint32_t uQualityIn,
    const std::uint32_t uQualityOut)
{
    WriteLog (lsTRACE, LedgerEntrySet)
        << "trustCreate: " << to_string (uSrcAccountID) << ", "
        << to_string (uDstAccountID) << ", " << saBalance.getFullText ();

    auto const& uLowAccountID   = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    auto const& uHighAccountID  =  bSrcHigh ? uSrcAccountID : uDstAccountID;

    auto sleDivvyState = std::make_shared<SLE>(
        ltRIPPLE_STATE, uIndex);
    entryCreate (sleDivvyState);

    std::uint64_t   uLowNode;
    std::uint64_t   uHighNode;

    TER terResult = dirAdd (
        uLowNode,
        getOwnerDirIndex (uLowAccountID),
        sleDivvyState->getIndex (),
        std::bind (&Ledger::ownerDirDescriber,
                   std::placeholders::_1, std::placeholders::_2,
                   uLowAccountID));

    if (tesSUCCESS == terResult)
    {
        terResult = dirAdd (
            uHighNode,
            getOwnerDirIndex (uHighAccountID),
            sleDivvyState->getIndex (),
            std::bind (&Ledger::ownerDirDescriber,
                       std::placeholders::_1, std::placeholders::_2,
                       uHighAccountID));
    }

    if (tesSUCCESS == terResult)
    {
        const bool bSetDst = saLimit.getIssuer () == uDstAccountID;
        const bool bSetHigh = bSrcHigh ^ bSetDst;

        assert (sleAccount->getFieldAccount160 (sfAccount) ==
            (bSetHigh ? uHighAccountID : uLowAccountID));
        SLE::pointer slePeer = entryCache (ltACCOUNT_ROOT,
            getAccountRootIndex (bSetHigh ? uLowAccountID : uHighAccountID));
        assert (slePeer);

        // Remember deletion hints.
        sleDivvyState->setFieldU64 (sfLowNode, uLowNode);
        sleDivvyState->setFieldU64 (sfHighNode, uHighNode);

        sleDivvyState->setFieldAmount (
            bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
        sleDivvyState->setFieldAmount (
            bSetHigh ? sfLowLimit : sfHighLimit,
            STAmount ({saBalance.getCurrency (),
                       bSetDst ? uSrcAccountID : uDstAccountID}));

        if (uQualityIn)
            sleDivvyState->setFieldU32 (
                bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

        if (uQualityOut)
            sleDivvyState->setFieldU32 (
                bSetHigh ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

        std::uint32_t uFlags = bSetHigh ? lsfHighReserve : lsfLowReserve;

        if (bAuth)
        {
            uFlags |= (bSetHigh ? lsfHighAuth : lsfLowAuth);
        }
        if (bNoDivvy)
        {
            uFlags |= (bSetHigh ? lsfHighNoDivvy : lsfLowNoDivvy);
        }
        if (bFreeze)
        {
            uFlags |= (!bSetHigh ? lsfLowFreeze : lsfHighFreeze);
        }

        if ((slePeer->getFlags() & lsfDefaultDivvy) == 0)
        {
            // The other side's default is no rippling
            uFlags |= (bSetHigh ? lsfLowNoDivvy : lsfHighNoDivvy);
        }

        sleDivvyState->setFieldU32 (sfFlags, uFlags);
        adjustOwnerCount(*this, sleAccount, 1);

        // ONLY: Create divvy balance.
        sleDivvyState->setFieldAmount (sfBalance, bSetHigh ? -saBalance : saBalance);

        cacheCredit (uSrcAccountID, uDstAccountID, saBalance);
    }

    return terResult;
}

TER LedgerEntrySet::trustDelete (
    SLE::ref sleDivvyState, AccountID const& uLowAccountID,
    AccountID const& uHighAccountID)
{
    // Detect legacy dirs.
    bool        bLowNode    = sleDivvyState->isFieldPresent (sfLowNode);
    bool        bHighNode   = sleDivvyState->isFieldPresent (sfHighNode);
    std::uint64_t uLowNode    = sleDivvyState->getFieldU64 (sfLowNode);
    std::uint64_t uHighNode   = sleDivvyState->getFieldU64 (sfHighNode);
    TER         terResult;

    WriteLog (lsTRACE, LedgerEntrySet)
        << "trustDelete: Deleting divvy line: low";
    terResult   = dirDelete (
        false,
        uLowNode,
        getOwnerDirIndex (uLowAccountID),
        sleDivvyState->getIndex (),
        false,
        !bLowNode);

    if (tesSUCCESS == terResult)
    {
        WriteLog (lsTRACE, LedgerEntrySet)
                << "trustDelete: Deleting divvy line: high";
        terResult   = dirDelete (
            false,
            uHighNode,
            getOwnerDirIndex (uHighAccountID),
            sleDivvyState->getIndex (),
            false,
            !bHighNode);
    }

    WriteLog (lsTRACE, LedgerEntrySet) << "trustDelete: Deleting divvy line: state";
    entryDelete (sleDivvyState);

    return terResult;
}

void LedgerEntrySet::enableDeferredCredits (bool enable)
{
    assert(enable == !mDeferredCredits);

    if (!enable)
    {
        mDeferredCredits.reset ();
        return;
    }

    if (!mDeferredCredits)
        mDeferredCredits.emplace ();
}

bool LedgerEntrySet::areCreditsDeferred () const
{
    return static_cast<bool> (mDeferredCredits);
}

STAmount LedgerEntrySet::adjustedBalance (AccountID const& main,
                                            AccountID const& other,
                                            STAmount const& amount) const
{
    if (mDeferredCredits)
        return mDeferredCredits->adjustedBalance (main, other, amount);
    return amount;
}

void LedgerEntrySet::cacheCredit (AccountID const& sender,
                                  AccountID const& receiver,
                                  STAmount const& amount)
{
    if (mDeferredCredits)
        return mDeferredCredits->credit (sender, receiver, amount);
}

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
TER LedgerEntrySet::divvyCredit (
    AccountID const& uSenderID, AccountID const& uReceiverID,
    STAmount const& saAmount, bool bCheckIssuer)
{
    auto issuer = saAmount.getIssuer ();
    auto currency = saAmount.getCurrency ();

    // Make sure issuer is involved.
    assert (
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer);
    (void) issuer;

    // Disallow sending to self.
    assert (uSenderID != uReceiverID);

    bool bSenderHigh = uSenderID > uReceiverID;
    uint256 uIndex = getDivvyStateIndex (
        uSenderID, uReceiverID, saAmount.getCurrency ());
    auto sleDivvyState  = entryCache (ltRIPPLE_STATE, uIndex);

    TER terResult;

    assert (!isXDV (uSenderID) && uSenderID != noAccount());
    assert (!isXDV (uReceiverID) && uReceiverID != noAccount());

    if (!sleDivvyState)
    {
        STAmount saReceiverLimit({currency, uReceiverID});
        STAmount saBalance = saAmount;

        saBalance.setIssuer (noAccount());

        WriteLog (lsDEBUG, LedgerEntrySet) << "divvyCredit: "
            "create line: " << to_string (uSenderID) <<
            " -> " << to_string (uReceiverID) <<
            " : " << saAmount.getFullText ();

        SLE::pointer sleAccount = entryCache (ltACCOUNT_ROOT,
            getAccountRootIndex (uReceiverID));

        bool noDivvy = (sleAccount->getFlags() & lsfDefaultDivvy) == 0;

        terResult = trustCreate (
            bSenderHigh,
            uSenderID,
            uReceiverID,
            uIndex,
            sleAccount,
            false,
            noDivvy,
            false,
            saBalance,
            saReceiverLimit);
    }
    else
    {
        cacheCredit (uSenderID, uReceiverID, saAmount);

        STAmount    saBalance   = sleDivvyState->getFieldAmount (sfBalance);

        if (bSenderHigh)
            saBalance.negate ();    // Put balance in sender terms.

        STAmount    saBefore    = saBalance;

        saBalance   -= saAmount;

        WriteLog (lsTRACE, LedgerEntrySet) << "divvyCredit: " <<
            to_string (uSenderID) <<
            " -> " << to_string (uReceiverID) <<
            " : before=" << saBefore.getFullText () <<
            " amount=" << saAmount.getFullText () <<
            " after=" << saBalance.getFullText ();

        std::uint32_t const uFlags (sleDivvyState->getFieldU32 (sfFlags));
        bool bDelete = false;

        // YYY Could skip this if rippling in reverse.
        if (saBefore > zero
            // Sender balance was positive.
            && saBalance <= zero
            // Sender is zero or negative.
            && (uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
            // Sender reserve is set.
            && static_cast <bool> (uFlags & (!bSenderHigh ? lsfLowNoDivvy : lsfHighNoDivvy)) !=
               static_cast <bool> (entryCache (ltACCOUNT_ROOT,
                   getAccountRootIndex (uSenderID))->getFlags() & lsfDefaultDivvy)
            && !(uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze))
            && !sleDivvyState->getFieldAmount (
                !bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && !sleDivvyState->getFieldU32 (
                !bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
            // Sender quality in is 0.
            && !sleDivvyState->getFieldU32 (
                !bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
            // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            adjustOwnerCount(*this, entryCache(ltACCOUNT_ROOT,
                getAccountRootIndex(uSenderID)), -1);

            // Clear reserve flag.
            sleDivvyState->setFieldU32 (
                sfFlags,
                uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance        // Balance is zero.
                && !(uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve));
            // Receiver reserve is clear.
        }

        if (bSenderHigh)
            saBalance.negate ();

        // Want to reflect balance to zero even if we are deleting line.
        sleDivvyState->setFieldAmount (sfBalance, saBalance);
        // ONLY: Adjust divvy balance.

        if (bDelete)
        {
            terResult   = trustDelete (
                sleDivvyState,
                bSenderHigh ? uReceiverID : uSenderID,
                !bSenderHigh ? uReceiverID : uSenderID);
        }
        else
        {
            entryModify (sleDivvyState);
            terResult   = tesSUCCESS;
        }
    }

    return terResult;
}

// Calculate the fee needed to transfer IOU assets between two parties.
STAmount LedgerEntrySet::divvyTransferFee (
    AccountID const& from,
    AccountID const& to,
    AccountID const& issuer,
    STAmount const& saAmount)
{
    if (from != issuer && to != issuer)
    {
        std::uint32_t uTransitRate = divvyTransferRate (*this, issuer);

        if (QUALITY_ONE != uTransitRate)
        {
            STAmount saTransferTotal = multiply (
                saAmount, amountFromRate (uTransitRate), saAmount.issue ());
            STAmount saTransferFee = saTransferTotal - saAmount;

            WriteLog (lsDEBUG, LedgerEntrySet) << "divvyTransferFee:" <<
                " saTransferFee=" << saTransferFee.getFullText ();

            return saTransferFee;
        }
    }

    return saAmount.zeroed();
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to reciever.
// <-- saActual: Amount actually cost.  Sender pay's fees.
TER LedgerEntrySet::divvySend (
    AccountID const& uSenderID, AccountID const& uReceiverID,
    STAmount const& saAmount, STAmount& saActual)
{
    auto const issuer   = saAmount.getIssuer ();
    TER             terResult;

    assert (!isXDV (uSenderID) && !isXDV (uReceiverID));
    assert (uSenderID != uReceiverID);

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        terResult   = divvyCredit (uSenderID, uReceiverID, saAmount, false);
        saActual    = saAmount;
        terResult   = tesSUCCESS;
    }
    else
    {
        // Sending 3rd party IOUs: transit.

        STAmount saTransitFee = divvyTransferFee (
            uSenderID, uReceiverID, issuer, saAmount);

        saActual = !saTransitFee ? saAmount : saAmount + saTransitFee;

        saActual.setIssuer (issuer); // XXX Make sure this done in + above.

        WriteLog (lsDEBUG, LedgerEntrySet) << "divvySend> " <<
            to_string (uSenderID) <<
            " - > " << to_string (uReceiverID) <<
            " : deliver=" << saAmount.getFullText () <<
            " fee=" << saTransitFee.getFullText () <<
            " cost=" << saActual.getFullText ();

        terResult   = divvyCredit (issuer, uReceiverID, saAmount);

        if (tesSUCCESS == terResult)
            terResult   = divvyCredit (uSenderID, issuer, saActual);
    }

    return terResult;
}

TER LedgerEntrySet::accountSend (
    AccountID const& uSenderID, AccountID const& uReceiverID,
    STAmount const& saAmount)
{
    assert (saAmount >= zero);

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (!saAmount.native ())
    {
        STAmount saActual;

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend: " <<
            to_string (uSenderID) << " -> " << to_string (uReceiverID) <<
            " : " << saAmount.getFullText ();

        return divvySend (uSenderID, uReceiverID, saAmount, saActual);
    }

    cacheCredit (uSenderID, uReceiverID, saAmount);

    /* XDV send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */

    TER terResult (tesSUCCESS);

    SLE::pointer sender = uSenderID != beast::zero
        ? entryCache (ltACCOUNT_ROOT, getAccountRootIndex (uSenderID))
        : SLE::pointer ();
    SLE::pointer receiver = uReceiverID != beast::zero
        ? entryCache (ltACCOUNT_ROOT, getAccountRootIndex (uReceiverID))
        : SLE::pointer ();

    if (ShouldLog (lsTRACE, LedgerEntrySet))
    {
        std::string sender_bal ("-");
        std::string receiver_bal ("-");

        if (sender)
            sender_bal = sender->getFieldAmount (sfBalance).getFullText ();

        if (receiver)
            receiver_bal = receiver->getFieldAmount (sfBalance).getFullText ();

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend> " <<
            to_string (uSenderID) << " (" << sender_bal <<
            ") -> " << to_string (uReceiverID) << " (" << receiver_bal <<
            ") : " << saAmount.getFullText ();
    }

    if (sender)
    {
        if (sender->getFieldAmount (sfBalance) < saAmount)
        {
            terResult = (mParams & tapOPEN_LEDGER)
                ? telFAILED_PROCESSING
                : tecFAILED_PROCESSING;
        }
        else
        {
            // Decrement XDV balance.
            sender->setFieldAmount (sfBalance,
                sender->getFieldAmount (sfBalance) - saAmount);
            entryModify (sender);
        }
    }

    if (tesSUCCESS == terResult && receiver)
    {
        // Increment XDV balance.
        receiver->setFieldAmount (sfBalance,
            receiver->getFieldAmount (sfBalance) + saAmount);
        entryModify (receiver);
    }

    if (ShouldLog (lsTRACE, LedgerEntrySet))
    {
        std::string sender_bal ("-");
        std::string receiver_bal ("-");

        if (sender)
            sender_bal = sender->getFieldAmount (sfBalance).getFullText ();

        if (receiver)
            receiver_bal = receiver->getFieldAmount (sfBalance).getFullText ();

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend< " <<
            to_string (uSenderID) << " (" << sender_bal <<
            ") -> " << to_string (uReceiverID) << " (" << receiver_bal <<
            ") : " << saAmount.getFullText ();
    }

    return terResult;
}

bool LedgerEntrySet::checkState (
    SLE::pointer state,
    bool bSenderHigh,
    AccountID const& sender,
    STAmount const& before,
    STAmount const& after)
{
    std::uint32_t const flags (state->getFieldU32 (sfFlags));

    auto sle = entryCache (ltACCOUNT_ROOT,
        getAccountRootIndex (sender));
    assert (sle);

    // YYY Could skip this if rippling in reverse.
    if (before > zero
        // Sender balance was positive.
        && after <= zero
        // Sender is zero or negative.
        && (flags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
        // Sender reserve is set.
        && static_cast <bool> (flags & (!bSenderHigh ? lsfLowNoDivvy : lsfHighNoDivvy)) !=
           static_cast <bool> (sle->getFlags() & lsfDefaultDivvy)
        && !(flags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze))
        && !state->getFieldAmount (
            !bSenderHigh ? sfLowLimit : sfHighLimit)
        // Sender trust limit is 0.
        && !state->getFieldU32 (
            !bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
        // Sender quality in is 0.
        && !state->getFieldU32 (
            !bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
        // Sender quality out is 0.
    {
        // VFALCO Where is the line being deleted?
        // Clear the reserve of the sender, possibly delete the line!
        adjustOwnerCount(*this, sle, -1);

        // Clear reserve flag.
        state->setFieldU32 (sfFlags,
            flags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

        // Balance is zero, receiver reserve is clear.
        if (!after        // Balance is zero.
            && !(flags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)))
            return true;
    }

    return false;
}

TER LedgerEntrySet::issue_iou (
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue)
{
    assert (!isXDV (account) && !isXDV (issue.account));

    // Consistency check
    assert (issue == amount.issue ());

    // Can't send to self!
    assert (issue.account != account);

    WriteLog (lsTRACE, LedgerEntrySet) << "issue_iou: " <<
        to_string (account) << ": " <<
        amount.getFullText ();

    bool bSenderHigh = issue.account > account;
    uint256 const index = getDivvyStateIndex (
        issue.account, account, issue.currency);
    auto state = entryCache (ltRIPPLE_STATE, index);

    if (!state)
    {
        // NIKB TODO: The limit uses the receiver's account as the issuer and
        // this is unnecessarily inefficient as copying which could be avoided
        // is now required. Consider available options.
        STAmount limit({issue.currency, account});
        STAmount final_balance = amount;

        final_balance.setIssuer (noAccount());

        SLE::pointer receiverAccount = entryCache (ltACCOUNT_ROOT,
            getAccountRootIndex (account));

        bool noDivvy = (receiverAccount->getFlags() & lsfDefaultDivvy) == 0;

        return trustCreate (bSenderHigh, issue.account, account, index,
            receiverAccount, false, noDivvy, false, final_balance, limit);
    }

    STAmount final_balance = state->getFieldAmount (sfBalance);

    if (bSenderHigh)
        final_balance.negate ();    // Put balance in sender terms.

    STAmount const start_balance = final_balance;

    final_balance -= amount;

    auto const must_delete = checkState (state, bSenderHigh, issue.account,
        start_balance, final_balance);

    if (bSenderHigh)
        final_balance.negate ();

    cacheCredit (issue.account, account, amount);

    // Adjust the balance on the trust line if necessary. We do this even if we
    // are going to delete the line to reflect the correct balance at the time
    // of deletion.
    state->setFieldAmount (sfBalance, final_balance);

    if (must_delete)
    {
        return trustDelete (state,
            bSenderHigh ? account : issue.account,
            bSenderHigh ? issue.account : account);
    }

    entryModify (state);
    return tesSUCCESS;
}

TER LedgerEntrySet::redeem_iou (
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue)
{
    assert (!isXDV (account) && !isXDV (issue.account));

    // Consistency check
    assert (issue == amount.issue ());

    // Can't send to self!
    assert (issue.account != account);

    WriteLog (lsTRACE, LedgerEntrySet) << "redeem_iou: " <<
        to_string (account) << ": " <<
        amount.getFullText ();

    bool bSenderHigh = account > issue.account;
    uint256 const index = getDivvyStateIndex (
        account, issue.account, issue.currency);
    auto state  = entryCache (ltRIPPLE_STATE, index);

    if (!state)
    {
        // In order to hold an IOU, a trust line *MUST* exist to track the
        // balance. If it doesn't, then something is very wrong. Don't try
        // to continue.
        WriteLog (lsFATAL, LedgerEntrySet) << "redeem_iou: " <<
            to_string (account) << " attempts to redeem " <<
            amount.getFullText () << " but no trust line exists!";

        return tefINTERNAL;
    }

    STAmount final_balance = state->getFieldAmount (sfBalance);

    if (bSenderHigh)
        final_balance.negate ();    // Put balance in sender terms.

    STAmount const start_balance = final_balance;

    final_balance -= amount;

    auto const must_delete = checkState (state, bSenderHigh, account,
        start_balance, final_balance);

    if (bSenderHigh)
        final_balance.negate ();

    cacheCredit (account, issue.account, amount);

    // Adjust the balance on the trust line if necessary. We do this even if we
    // are going to delete the line to reflect the correct balance at the time
    // of deletion.
    state->setFieldAmount (sfBalance, final_balance);

    if (must_delete)
    {
        return trustDelete (state,
            bSenderHigh ? issue.account : account,
            bSenderHigh ? account : issue.account);
    }

    entryModify (state);
    return tesSUCCESS;
}

TER LedgerEntrySet::transfer_xdv (
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount)
{
    assert (from != beast::zero);
    assert (to != beast::zero);
    assert (from != to);
    assert (amount.native ());

    SLE::pointer sender = entryCache (ltACCOUNT_ROOT,
        getAccountRootIndex (from));
    SLE::pointer receiver = entryCache (ltACCOUNT_ROOT,
        getAccountRootIndex (to));

    WriteLog (lsTRACE, LedgerEntrySet) << "transfer_xdv: " <<
        to_string (from) <<  " -> " << to_string (to) <<
        ") : " << amount.getFullText ();

    if (sender->getFieldAmount (sfBalance) < amount)
    {
        // FIXME: this logic should be moved to callers maybe?
        return (mParams & tapOPEN_LEDGER)
            ? telFAILED_PROCESSING
            : tecFAILED_PROCESSING;
    }

    // Decrement XDV balance.
    sender->setFieldAmount (sfBalance,
        sender->getFieldAmount (sfBalance) - amount);
    entryModify (sender);

    receiver->setFieldAmount (sfBalance,
        receiver->getFieldAmount (sfBalance) + amount);
    entryModify (receiver);

    return tesSUCCESS;
}

std::uint32_t
divvyTransferRate (LedgerEntrySet& ledger, AccountID const& issuer)
{
    SLE::pointer sleAccount (ledger.entryCache (
        ltACCOUNT_ROOT, getAccountRootIndex (issuer)));

    std::uint32_t quality = QUALITY_ONE;

    if (sleAccount && sleAccount->isFieldPresent (sfTransferRate))
        quality = sleAccount->getFieldU32 (sfTransferRate);

    return quality;
}

std::uint32_t
divvyTransferRate (LedgerEntrySet& ledger, AccountID const& uSenderID,
    AccountID const& uReceiverID, AccountID const& issuer)
{
    // If calculating the transfer rate from or to the issuer of the currency
    // no fees are assessed.
    return (uSenderID == issuer || uReceiverID == issuer)
           ? QUALITY_ONE
           : divvyTransferRate (ledger, issuer);
}

ScopedDeferCredits::ScopedDeferCredits (LedgerEntrySet& l)
    : les_ (l), enabled_ (false)
{
    if (!les_.areCreditsDeferred ())
    {
        WriteLog (lsTRACE, DeferredCredits) << "Enable";
        les_.enableDeferredCredits (true);
        enabled_ = true;
    }
}

ScopedDeferCredits::~ScopedDeferCredits ()
{
    if (enabled_)
    {
        WriteLog (lsTRACE, DeferredCredits) << "Disable";
        les_.enableDeferredCredits (false);
    }
}

//------------------------------------------------------------------------------

void
adjustOwnerCount (LedgerEntrySet& view,
    std::shared_ptr<SLE> const& sle, int amount)
{
    assert(amount != 0);
    auto const current =
        sle->getFieldU32 (sfOwnerCount);
    auto adjusted = current + amount;
    if (amount > 0)
    {
        // Overflow is well defined on unsigned
        if (adjusted < current)
        {
            WriteLog (lsFATAL, LedgerEntrySet) <<
                "Account " << sle->getFieldAccount160(sfAccount) <<
                " owner count exceeds max!";
            adjusted =
                std::numeric_limits<std::uint32_t>::max ();
        }
    }
    else
    {
        // Underflow is well defined on unsigned
        if (adjusted > current)
        {
            WriteLog (lsFATAL, LedgerEntrySet) <<
                "Account " << sle->getFieldAccount160 (sfAccount) <<
                " owner count set below 0!";
            adjusted = 0;
            assert(false);
        }
    }
    sle->setFieldU32 (sfOwnerCount, adjusted);
    view.entryModify (sle);
}

STAmount
funds (LedgerEntrySet& view, AccountID const& id,
    STAmount const& saDefault, FreezeHandling freezeHandling)
{
    STAmount saFunds;

    if (!saDefault.native () &&
        saDefault.getIssuer () == id)
    {
        saFunds = saDefault;
        WriteLog (lsTRACE, LedgerEntrySet) << "accountFunds:" <<
            " account=" << to_string (id) <<
            " saDefault=" << saDefault.getFullText () <<
            " SELF-FUNDED";
    }
    else
    {
        saFunds = view.accountHolds(id,
            saDefault.getCurrency(), saDefault.getIssuer(),
                freezeHandling);
        WriteLog (lsTRACE, LedgerEntrySet) << "accountFunds:" <<
            " account=" << to_string (id) <<
            " saDefault=" << saDefault.getFullText () <<
            " saFunds=" << saFunds.getFullText ();
    }
    return saFunds;
}

} // divvy
