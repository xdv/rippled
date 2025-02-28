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
#include <divvy/app/main/Application.h>
#include <divvy/app/paths/Tuning.h>
#include <divvy/app/paths/Pathfinder.h>
#include <divvy/app/paths/DivvyCalc.h>
#include <divvy/app/paths/DivvyLineCache.h>
#include <divvy/app/ledger/OrderBookDB.h>
#include <divvy/basics/Log.h>
#include <divvy/json/to_string.h>
#include <divvy/core/JobQueue.h>
#include <tuple>

/*

Core Pathfinding Engine

The pathfinding request is identified by category, XDV to XDV, XDV to
non-XDV, non-XDV to XDV, same currency non-XDV to non-XDV, cross-currency
non-XDV to non-XDV.  For each category, there is a table of paths that the
pathfinder searches for.  Complete paths are collected.

Each complete path is then rated and sorted. Paths with no or trivial
liquidity are dropped.  Otherwise, paths are sorted based on quality,
liquidity, and path length.

Path slots are filled in quality (ratio of out to in) order, with the
exception that the last path must have enough liquidity to complete the
payment (assuming no liquidity overlap).  In addition, if no selected path
is capable of providing enough liquidity to complete the payment by itself,
an extra "covering" path is returned.

The selected paths are then tested to determine if they can complete the
payment and, if so, at what cost.  If they fail and a covering path was
found, the test is repeated with the covering path.  If this succeeds, the
final paths and the estimated cost are returned.

The engine permits the search depth to be selected and the paths table
includes the depth at which each path type is found.  A search depth of zero
causes no searching to be done.  Extra paths can also be injected, and this
should be used to preserve previously-found paths across invokations for the
same path request (particularly if the search depth may change).

*/

namespace divvy {

namespace {

// We sort possible paths by:
//    cost of path
//    length of path
//    width of path

// Compare two PathRanks.  A better PathRank is lower, so the best are sorted to
// the beginning.
bool comparePathRank (
    Pathfinder::PathRank const& a, Pathfinder::PathRank const& b)
{
    // 1) Higher quality (lower cost) is better
    if (a.quality != b.quality)
        return a.quality < b.quality;

    // 2) More liquidity (higher volume) is better
    if (a.liquidity != b.liquidity)
        return a.liquidity > b.liquidity;

    // 3) Shorter paths are better
    if (a.length != b.length)
        return a.length < b.length;

    // 4) Tie breaker
    return a.index > b.index;
}

struct AccountCandidate
{
    int priority;
    AccountID account;

    static const int highPriority = 10000;
};

bool compareAccountCandidate (
    std::uint32_t seq,
    AccountCandidate const& first, AccountCandidate const& second)
{
    if (first.priority < second.priority)
        return false;

    if (first.account > second.account)
        return true;

    return (first.priority ^ seq) < (second.priority ^ seq);
}

using AccountCandidates = std::vector<AccountCandidate>;

struct CostedPath
{
    int searchLevel;
    Pathfinder::PathType type;
};

using CostedPathList = std::vector<CostedPath>;

using PathTable = std::map<Pathfinder::PaymentType, CostedPathList>;

struct PathCost {
    int cost;
    char const* path;
};
using PathCostList = std::vector<PathCost>;

static PathTable mPathTable;

std::string pathTypeToString (Pathfinder::PathType const& type)
{
    std::string ret;

    for (auto const& node : type)
    {
        switch (node)
        {
            case Pathfinder::nt_SOURCE:
                ret.append("s");
                break;
            case Pathfinder::nt_ACCOUNTS:
                ret.append("a");
                break;
            case Pathfinder::nt_BOOKS:
                ret.append("b");
                break;
            case Pathfinder::nt_XDV_BOOK:
                ret.append("x");
                break;
            case Pathfinder::nt_DEST_BOOK:
                ret.append("f");
                break;
            case Pathfinder::nt_DESTINATION:
                ret.append("d");
                break;
        }
    }

    return ret;
}

}  // namespace

Pathfinder::Pathfinder (
    DivvyLineCache::ref cache,
    AccountID const& uSrcAccount,
    AccountID const& uDstAccount,
    Currency const& uSrcCurrency,
    AccountID const& uSrcIssuer,
    STAmount const& saDstAmount)
    :   mSrcAccount (uSrcAccount),
        mDstAccount (uDstAccount),
        mEffectiveDst (isXDV(saDstAmount.getIssuer ()) ?
            uDstAccount : saDstAmount.getIssuer ()),
        mDstAmount (saDstAmount),
        mSrcCurrency (uSrcCurrency),
        mSrcIssuer (uSrcIssuer),
        mSrcAmount ({uSrcCurrency, uSrcIssuer}, 1u, 0, true),
        mLedger (cache->getLedger ()),
        mRLCache (cache)
{
    assert (isXDV(uSrcCurrency) == isXDV(uSrcIssuer));
}

Pathfinder::Pathfinder (
    DivvyLineCache::ref cache,
    AccountID const& uSrcAccount,
    AccountID const& uDstAccount,
    Currency const& uSrcCurrency,
    STAmount const& saDstAmount)
    :   mSrcAccount (uSrcAccount),
        mDstAccount (uDstAccount),
        mEffectiveDst (isXDV(saDstAmount.getIssuer ()) ?
            uDstAccount : saDstAmount.getIssuer ()),
        mDstAmount (saDstAmount),
        mSrcCurrency (uSrcCurrency),
        mSrcAmount (
        {
            uSrcCurrency,
            isXDV (uSrcCurrency) ? xdvAccount () : uSrcAccount
        }, 1u, 0, true),
        mLedger (cache->getLedger ()),
        mRLCache (cache)
{
}

Pathfinder::~Pathfinder()
{
}

bool Pathfinder::findPaths (int searchLevel)
{
    if (mDstAmount == zero)
    {
        // No need to send zero money.
        WriteLog (lsDEBUG, Pathfinder) << "Destination amount was zero.";
        mLedger.reset ();
        return false;

        // TODO(tom): why do we reset the ledger just in this case and the one
        // below - why don't we do it each time we return false?
    }

    if (mSrcAccount == mDstAccount &&
        mDstAccount == mEffectiveDst &&
        mSrcCurrency == mDstAmount.getCurrency ())
    {
        // No need to send to same account with same currency.
        WriteLog (lsDEBUG, Pathfinder) << "Tried to send to same issuer";
        mLedger.reset ();
        return false;
    }

    if (mSrcAccount == mEffectiveDst &&
        mSrcCurrency == mDstAmount.getCurrency())
    {
        // Default path might work, but any path would loop
        return true;
    }

    m_loadEvent = getApp ().getJobQueue ().getLoadEvent (
        jtPATH_FIND, "FindPath");
    auto currencyIsXDV = isXDV (mSrcCurrency);

    bool useIssuerAccount
            = mSrcIssuer && !currencyIsXDV && !isXDV (*mSrcIssuer);
    auto& account = useIssuerAccount ? *mSrcIssuer : mSrcAccount;
    auto issuer = currencyIsXDV ? AccountID() : account;
    mSource = STPathElement (account, mSrcCurrency, issuer);
    auto issuerString = mSrcIssuer
            ? to_string (*mSrcIssuer) : std::string ("none");
    WriteLog (lsTRACE, Pathfinder)
            << "findPaths>"
            << " mSrcAccount=" << mSrcAccount
            << " mDstAccount=" << mDstAccount
            << " mDstAmount=" << mDstAmount.getFullText ()
            << " mSrcCurrency=" << mSrcCurrency
            << " mSrcIssuer=" << issuerString;

    if (!mLedger)
    {
        WriteLog (lsDEBUG, Pathfinder) << "findPaths< no ledger";
        return false;
    }

    bool bSrcXrp = isXDV (mSrcCurrency);
    bool bDstXrp = isXDV (mDstAmount.getCurrency());

    if (! mLedger->exists (getAccountRootIndex (mSrcAccount)))
    {
        // We can't even start without a source account.
        WriteLog (lsDEBUG, Pathfinder) << "invalid source account";
        return false;
    }

    if ((mEffectiveDst != mDstAccount) &&
        ! mLedger->exists (getAccountRootIndex (mEffectiveDst)))
    {
        WriteLog (lsDEBUG, Pathfinder)
            << "Non-existent gateway";
        return false;
    }

    if (! mLedger->exists (getAccountRootIndex (mDstAccount)))
    {
        // Can't find the destination account - we must be funding a new
        // account.
        if (!bDstXrp)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "New account not being funded in XDV ";
            return false;
        }

        auto const reserve = STAmount (mLedger->getReserve (0));
        if (mDstAmount < reserve)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "New account not getting enough funding: "
                    << mDstAmount << " < " << reserve;
            return false;
        }
    }

    // Now compute the payment type from the types of the source and destination
    // currencies.
    PaymentType paymentType;
    if (bSrcXrp && bDstXrp)
    {
        // XDV -> XDV
        WriteLog (lsDEBUG, Pathfinder) << "XDV to XDV payment";
        paymentType = pt_XDV_to_XDV;
    }
    else if (bSrcXrp)
    {
        // XDV -> non-XDV
        WriteLog (lsDEBUG, Pathfinder) << "XDV to non-XDV payment";
        paymentType = pt_XDV_to_nonXDV;
    }
    else if (bDstXrp)
    {
        // non-XDV -> XDV
        WriteLog (lsDEBUG, Pathfinder) << "non-XDV to XDV payment";
        paymentType = pt_nonXDV_to_XDV;
    }
    else if (mSrcCurrency == mDstAmount.getCurrency ())
    {
        // non-XDV -> non-XDV - Same currency
        WriteLog (lsDEBUG, Pathfinder) << "non-XDV to non-XDV - same currency";
        paymentType = pt_nonXDV_to_same;
    }
    else
    {
        // non-XDV to non-XDV - Different currency
        WriteLog (lsDEBUG, Pathfinder) << "non-XDV to non-XDV - cross currency";
        paymentType = pt_nonXDV_to_nonXDV;
    }

    // Now iterate over all paths for that paymentType.
    for (auto const& costedPath : mPathTable[paymentType])
    {
        // Only use paths with at most the current search level.
        if (costedPath.searchLevel <= searchLevel)
        {
            addPathsForType (costedPath.type);

            // TODO(tom): we might be missing other good paths with this
            // arbitrary cut off.
            if (mCompletePaths.size () > PATHFINDER_MAX_COMPLETE_PATHS)
                break;
        }
    }

    WriteLog (lsDEBUG, Pathfinder)
            << mCompletePaths.size () << " complete paths found";

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

TER Pathfinder::getPathLiquidity (
    STPath const& path,            // IN:  The path to check.
    STAmount const& minDstAmount,  // IN:  The minimum output this path must
                                   //      deliver to be worth keeping.
    STAmount& amountOut,           // OUT: The actual liquidity along the path.
    uint64_t& qualityOut) const    // OUT: The returned initial quality
{
    STPathSet pathSet;
    pathSet.push_back (path);

    path::DivvyCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    LedgerEntrySet lesSandbox (mLedger, tapNONE);

    try
    {
        // Compute a path that provides at least the minimum liquidity.
        auto rc = path::DivvyCalc::divvyCalculate (
            lesSandbox,
            mSrcAmount,
            minDstAmount,
            mDstAccount,
            mSrcAccount,
            pathSet,
            &rcInput);

        // If we can't get even the minimum liquidity requested, we're done.
        if (rc.result () != tesSUCCESS)
            return rc.result ();

        qualityOut = getRate (rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        // Now try to compute the remaining liquidity.
        rcInput.partialPaymentAllowed = true;
        rc = path::DivvyCalc::divvyCalculate (
            lesSandbox,
            mSrcAmount,
            mDstAmount - amountOut,
            mDstAccount,
            mSrcAccount,
            pathSet,
            &rcInput);

        // If we found further liquidity, add it into the result.
        if (rc.result () == tesSUCCESS)
            amountOut += rc.actualAmountOut;

        return tesSUCCESS;
    }
    catch (std::exception const& e)
    {
        WriteLog (lsINFO, Pathfinder) <<
            "checkpath: exception (" << e.what() << ") " <<
            path.getJson (0);
        return tefEXCEPTION;
    }
}

namespace {

// Return the smallest amount of useful liquidity for a given amount, and the
// total number of paths we have to evaluate.
STAmount smallestUsefulAmount (STAmount const& amount, int maxPaths)
{
    return divide (amount, STAmount (maxPaths + 2), amount.issue ());
}

} // namespace

void Pathfinder::computePathRanks (int maxPaths)
{
    mRemainingAmount = mDstAmount;

    // Must subtract liquidity in default path from remaining amount.
    try
    {
        LedgerEntrySet lesSandbox (mLedger, tapNONE);

        path::DivvyCalc::Input rcInput;
        rcInput.partialPaymentAllowed = true;
        auto rc = path::DivvyCalc::divvyCalculate (
            lesSandbox,
            mSrcAmount,
            mDstAmount,
            mDstAccount,
            mSrcAccount,
            STPathSet(),
            &rcInput);

        if (rc.result () == tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "Default path contributes: " << rc.actualAmountIn;
            mRemainingAmount -= rc.actualAmountOut;
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder)
                << "Default path fails: " << transToken (rc.result ());
        }
    }
    catch (...)
    {
        WriteLog (lsDEBUG, Pathfinder) << "Default path causes exception";
    }

    rankPaths (maxPaths, mCompletePaths, mPathRanks);
}

static bool isDefaultPath (STPath const& path)
{
    // TODO(tom): default paths can consist of more than just an account:
    // https://forum.xdv.io/viewtopic.php?f=2&t=8206&start=10#p57713
    //
    // JoelKatz writes:
    // So the test for whether a path is a default path is incorrect. I'm not
    // sure it's worth the complexity of fixing though. If we are going to fix
    // it, I'd suggest doing it this way:
    //
    // 1) Compute the default path, probably by using 'expandPath' to expand an
    // empty path.  2) Chop off the source and destination nodes.
    //
    // 3) In the pathfinding loop, if the source issuer is not the sender,
    // reject all paths that don't begin with the issuer's account node or match
    // the path we built at step 2.
    return path.size() == 1;
}

static STPath removeIssuer (STPath const& path)
{
    // This path starts with the issuer, which is already implied
    // so remove the head node
    STPath ret;

    for (auto it = path.begin() + 1; it != path.end(); ++it)
        ret.push_back (*it);

    return ret;
}

// For each useful path in the input path set,
// create a ranking entry in the output vector of path ranks
void Pathfinder::rankPaths (
    int maxPaths,
    STPathSet const& paths,
    std::vector <PathRank>& rankedPaths)
{

    rankedPaths.clear ();
    rankedPaths.reserve (paths.size());

    // Ignore paths that move only very small amounts.
    auto saMinDstAmount = smallestUsefulAmount (mDstAmount, maxPaths);

    for (int i = 0; i < paths.size (); ++i)
    {
        auto const& currentPath = paths[i];
        STAmount liquidity;
        uint64_t uQuality;

        if (currentPath.empty ())
            continue;

        auto const resultCode = getPathLiquidity (
            currentPath, saMinDstAmount, liquidity, uQuality);

        if (resultCode != tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: dropping : " << transToken (resultCode) <<
                ": " << currentPath.getJson (0);
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: quality: " << uQuality <<
                ": " << currentPath.getJson (0);

            rankedPaths.push_back ({uQuality, currentPath.size (), liquidity, i});
        }
    }
    std::sort (rankedPaths.begin (), rankedPaths.end (), comparePathRank);
}

STPathSet Pathfinder::getBestPaths (
    int maxPaths,
    STPath& fullLiquidityPath,
    STPathSet& extraPaths,
    AccountID const& srcIssuer)
{
    WriteLog (lsDEBUG, Pathfinder) << "findPaths: " <<
        mCompletePaths.size() << " paths and " <<
        extraPaths.size () << " extras";

    if (mCompletePaths.empty() && extraPaths.empty())
        return mCompletePaths;

    assert (fullLiquidityPath.empty ());
    const bool issuerIsSender = isXDV (mSrcCurrency) || (srcIssuer == mSrcAccount);

    std::vector <PathRank> extraPathRanks;
    rankPaths (maxPaths, extraPaths, extraPathRanks);

    STPathSet bestPaths;

    // The best PathRanks are now at the start.  Pull off enough of them to
    // fill bestPaths, then look through the rest for the best individual
    // path that can satisfy the entire liquidity - if one exists.
    STAmount remaining = mRemainingAmount;

    auto pathsIterator = mPathRanks.begin();
    auto extraPathsIterator = extraPathRanks.begin();

    while (pathsIterator != mPathRanks.end() ||
        extraPathsIterator != extraPathRanks.end())
    {
        bool usePath = false;
        bool useExtraPath = false;

        if (pathsIterator == mPathRanks.end())
            useExtraPath = true;
        else if (extraPathsIterator == extraPathRanks.end())
            usePath = true;
        else if (extraPathsIterator->quality < pathsIterator->quality)
            useExtraPath = true;
        else if (extraPathsIterator->quality > pathsIterator->quality)
            usePath = true;
        else if (extraPathsIterator->liquidity > pathsIterator->liquidity)
            useExtraPath = true;
        else if (extraPathsIterator->liquidity < pathsIterator->liquidity)
            usePath = true;
        else
        {
            // Risk is high they have identical liquidity
            useExtraPath = true;
            usePath = true;
        }

        auto& pathRank = usePath ? *pathsIterator : *extraPathsIterator;

        auto& path = usePath ? mCompletePaths[pathRank.index] :
            extraPaths[pathRank.index];

        if (useExtraPath)
            ++extraPathsIterator;

        if (usePath)
            ++pathsIterator;

        auto iPathsLeft = maxPaths - bestPaths.size ();
        if (!(iPathsLeft > 0 || fullLiquidityPath.empty ()))
            break;

        if (path.empty ())
        {
            assert (false);
            continue;
        }

        bool startsWithIssuer = false;

        if (! issuerIsSender && usePath)
        {
            // Need to make sure path matches issuer constraints

            if (path.front ().getAccountID() != srcIssuer)
                continue;
            if (isDefaultPath (path))
            {
                continue;
            }
            startsWithIssuer = true;
        }

        if (iPathsLeft > 1 ||
            (iPathsLeft > 0 && pathRank.liquidity >= remaining))
            // last path must fill
        {
            --iPathsLeft;
            remaining -= pathRank.liquidity;
            bestPaths.push_back (startsWithIssuer ? removeIssuer (path) : path);
        }
        else if (iPathsLeft == 0 &&
                 pathRank.liquidity >= mDstAmount &&
                 fullLiquidityPath.empty ())
        {
            // We found an extra path that can move the whole amount.
            fullLiquidityPath = (startsWithIssuer ? removeIssuer (path) : path);
            WriteLog (lsDEBUG, Pathfinder) <<
                "Found extra full path: " << fullLiquidityPath.getJson (0);
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "Skipping a non-filling path: " << path.getJson (0);
        }
    }

    if (remaining > zero)
    {
        assert (fullLiquidityPath.empty ());
        WriteLog (lsINFO, Pathfinder) <<
            "Paths could not send " << remaining << " of " << mDstAmount;
    }
    else
    {
        WriteLog (lsDEBUG, Pathfinder) <<
            "findPaths: RESULTS: " << bestPaths.getJson (0);
    }
    return bestPaths;
}

bool Pathfinder::issueMatchesOrigin (Issue const& issue)
{
    bool matchingCurrency = (issue.currency == mSrcCurrency);
    bool matchingAccount =
            isXDV (issue.currency) ||
            (mSrcIssuer && issue.account == mSrcIssuer) ||
            issue.account == mSrcAccount;

    return matchingCurrency && matchingAccount;
}

int Pathfinder::getPathsOut (
    Currency const& currency,
    AccountID const& account,
    bool isDstCurrency,
    AccountID const& dstAccount)
{
    Issue const issue (currency, account);

    auto it = mPathsOutCountMap.emplace (issue, 0);

    // If it was already present, return the stored number of paths
    if (!it.second)
        return it.first->second;

    auto sleAccount = fetch(*mLedger, getAccountRootIndex (account),
        getApp().getSLECache());

    if (!sleAccount)
        return 0;

    int aFlags = sleAccount->getFieldU32 (sfFlags);
    bool const bAuthRequired = (aFlags & lsfRequireAuth) != 0;
    bool const bFrozen = ((aFlags & lsfGlobalFreeze) != 0);

    int count = 0;

    if (!bFrozen)
    {
        count = getApp ().getOrderBookDB ().getBookSize (issue);

        for (auto const& item : mRLCache->getDivvyLines (account))
        {
            DivvyState* rspEntry = (DivvyState*) item.get ();

            if (currency != rspEntry->getLimit ().getCurrency ())
            {
            }
            else if (rspEntry->getBalance () <= zero &&
                     (!rspEntry->getLimitPeer ()
                      || -rspEntry->getBalance () >= rspEntry->getLimitPeer ()
                      ||  (bAuthRequired && !rspEntry->getAuth ())))
            {
            }
            else if (isDstCurrency &&
                     dstAccount == rspEntry->getAccountIDPeer ())
            {
                count += 10000; // count a path to the destination extra
            }
            else if (rspEntry->getNoDivvyPeer ())
            {
                // This probably isn't a useful path out
            }
            else if (rspEntry->getFreezePeer ())
            {
                // Not a useful path out
            }
            else
            {
                ++count;
            }
        }
    }
    it.first->second = count;
    return count;
}

void Pathfinder::addLinks (
    STPathSet const& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    WriteLog (lsDEBUG, Pathfinder)
        << "addLink< on " << currentPaths.size ()
        << " source(s), flags=" << addFlags;
    for (auto const& path: currentPaths)
        addLink (path, incompletePaths, addFlags);
}

STPathSet& Pathfinder::addPathsForType (PathType const& pathType)
{
    // See if the set of paths for this type already exists.
    auto it = mPaths.find (pathType);
    if (it != mPaths.end ())
        return it->second;

    // Otherwise, if the type has no nodes, return the empty path.
    if (pathType.empty ())
        return mPaths[pathType];

    // Otherwise, get the paths for the parent PathType by calling
    // addPathsForType recursively.
    PathType parentPathType = pathType;
    parentPathType.pop_back ();

    STPathSet const& parentPaths = addPathsForType (parentPathType);
    STPathSet& pathsOut = mPaths[pathType];

    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths< adding onto '"
        << pathTypeToString (parentPathType) << "' to get '"
        << pathTypeToString (pathType) << "'";

    int initialSize = mCompletePaths.size ();

    // Add the last NodeType to the lists.
    auto nodeType = pathType.back ();
    switch (nodeType)
    {
    case nt_SOURCE:
        // Source must always be at the start, so pathsOut has to be empty.
        assert (pathsOut.empty ());
        pathsOut.push_back (STPath ());
        break;

    case nt_ACCOUNTS:
        addLinks (parentPaths, pathsOut, afADD_ACCOUNTS);
        break;

    case nt_BOOKS:
        addLinks (parentPaths, pathsOut, afADD_BOOKS);
        break;

    case nt_XDV_BOOK:
        addLinks (parentPaths, pathsOut, afADD_BOOKS | afOB_XDV);
        break;

    case nt_DEST_BOOK:
        addLinks (parentPaths, pathsOut, afADD_BOOKS | afOB_LAST);
        break;

    case nt_DESTINATION:
        // FIXME: What if a different issuer was specified on the
        // destination amount?
        // TODO(tom): what does this even mean?  Should it be a JIRA?
        addLinks (parentPaths, pathsOut, afADD_ACCOUNTS | afAC_LAST);
        break;
    }

    CondLog (mCompletePaths.size () != initialSize, lsDEBUG, Pathfinder)
        << (mCompletePaths.size () - initialSize)
        << " complete paths added";
    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths> " << pathsOut.size () << " partial paths found";
    return pathsOut;
}

bool Pathfinder::isNoDivvy (
    AccountID const& fromAccount,
    AccountID const& toAccount,
    Currency const& currency)
{
    auto sleDivvy = fetch (*mLedger,
        getDivvyStateIndex (toAccount, fromAccount, currency),
            getApp().getSLECache());

    auto const flag ((toAccount > fromAccount)
                     ? lsfHighNoDivvy : lsfLowNoDivvy);

    return sleDivvy && (sleDivvy->getFieldU32 (sfFlags) & flag);
}

// Does this path end on an account-to-account link whose last account has
// set "no divvy" on the link?
bool Pathfinder::isNoDivvyOut (STPath const& currentPath)
{
    // Must have at least one link.
    if (currentPath.empty ())
        return false;

    // Last link must be an account.
    STPathElement const& endElement = currentPath.back ();
    if (!(endElement.getNodeType () & STPathElement::typeAccount))
        return false;

    // If there's only one item in the path, return true if that item specifies
    // no divvy on the output. A path with no divvy on its output can't be
    // followed by a link with no divvy on its input.
    auto const& fromAccount = (currentPath.size () == 1)
        ? mSrcAccount
        : (currentPath.end () - 2)->getAccountID ();
    auto const& toAccount = endElement.getAccountID ();
    return isNoDivvy (fromAccount, toAccount, endElement.getCurrency ());
}

void addUniquePath (STPathSet& pathSet, STPath const& path)
{
    // TODO(tom): building an STPathSet this way is quadratic in the size
    // of the STPathSet!
    for (auto const& p : pathSet)
    {
        if (p == path)
            return;
    }
    pathSet.push_back (path);
}

void Pathfinder::addLink (
    const STPath& currentPath,      // The path to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    auto const& pathEnd = currentPath.empty() ? mSource : currentPath.back ();
    auto const& uEndCurrency = pathEnd.getCurrency ();
    auto const& uEndIssuer = pathEnd.getIssuerID ();
    auto const& uEndAccount = pathEnd.getAccountID ();
    bool const bOnXDV = uEndCurrency.isZero ();

    // Does pathfinding really need to get this to
    // a gateway (the issuer of the destination amount)
    // rather than the ultimate destination?
    bool const hasEffectiveDestination = mEffectiveDst != mDstAccount;

    WriteLog (lsTRACE, Pathfinder) << "addLink< flags="
                                   << addFlags << " onXDV=" << bOnXDV;
    WriteLog (lsTRACE, Pathfinder) << currentPath.getJson (0);

    if (addFlags & afADD_ACCOUNTS)
    {
        // add accounts
        if (bOnXDV)
        {
            if (mDstAmount.native () && !currentPath.empty ())
            { // non-default path to XDV destination
                WriteLog (lsTRACE, Pathfinder)
                    << "complete path found ax: " << currentPath.getJson(0);
                addUniquePath (mCompletePaths, currentPath);
            }
        }
        else
        {
            // search for accounts to add
            auto const sleEnd = fetch(
                *mLedger, getAccountRootIndex (uEndAccount),
                    getApp().getSLECache());

            if (sleEnd)
            {
                bool const bRequireAuth (
                    sleEnd->getFieldU32 (sfFlags) & lsfRequireAuth);
                bool const bIsEndCurrency (
                    uEndCurrency == mDstAmount.getCurrency ());
                bool const bIsNoDivvyOut (
                    isNoDivvyOut (currentPath));
                bool const bDestOnly (
                    addFlags & afAC_LAST);

                auto& divvyLines (mRLCache->getDivvyLines (uEndAccount));

                AccountCandidates candidates;
                candidates.reserve (divvyLines.size ());

                for (auto const& item : divvyLines)
                {
                    auto* rs = dynamic_cast<DivvyState const *> (item.get ());
                    if (!rs)
                    {
                        WriteLog (lsERROR, Pathfinder)
                                << "Couldn't decipher DivvyState";
                        continue;
                    }
                    auto const& acct = rs->getAccountIDPeer ();

                    if (hasEffectiveDestination && (acct == mDstAccount))
                    {
                        // We skipped the gateway
                        continue;
                    }

                    bool bToDestination = acct == mEffectiveDst;

                    if (bDestOnly && !bToDestination)
                    {
                        continue;
                    }

                    if ((uEndCurrency == rs->getLimit ().getCurrency ()) &&
                        !currentPath.hasSeen (acct, uEndCurrency, acct))
                    {
                        // path is for correct currency and has not been seen
                        if (rs->getBalance () <= zero
                            && (!rs->getLimitPeer ()
                                || -rs->getBalance () >= rs->getLimitPeer ()
                                || (bRequireAuth && !rs->getAuth ())))
                        {
                            // path has no credit
                        }
                        else if (bIsNoDivvyOut && rs->getNoDivvy ())
                        {
                            // Can't leave on this path
                        }
                        else if (bToDestination)
                        {
                            // destination is always worth trying
                            if (uEndCurrency == mDstAmount.getCurrency ())
                            {
                                // this is a complete path
                                if (!currentPath.empty ())
                                {
                                    WriteLog (lsTRACE, Pathfinder)
                                            << "complete path found ae: "
                                            << currentPath.getJson (0);
                                    addUniquePath
                                            (mCompletePaths, currentPath);
                                }
                            }
                            else if (!bDestOnly)
                            {
                                // this is a high-priority candidate
                                candidates.push_back (
                                    {AccountCandidate::highPriority, acct});
                            }
                        }
                        else if (acct == mSrcAccount)
                        {
                            // going back to the source is bad
                        }
                        else
                        {
                            // save this candidate
                            int out = getPathsOut (
                                uEndCurrency,
                                acct,
                                bIsEndCurrency,
                                mEffectiveDst);
                            if (out)
                                candidates.push_back ({out, acct});
                        }
                    }
                }

                if (!candidates.empty())
                {
                    std::sort (candidates.begin (), candidates.end (),
                        std::bind(compareAccountCandidate,
                                  mLedger->getLedgerSeq (),
                                  std::placeholders::_1,
                                  std::placeholders::_2));

                    int count = candidates.size ();
                    // allow more paths from source
                    if ((count > 10) && (uEndAccount != mSrcAccount))
                        count = 10;
                    else if (count > 50)
                        count = 50;

                    auto it = candidates.begin();
                    while (count-- != 0)
                    {
                        // Add accounts to incompletePaths
                        STPathElement pathElement (
                            STPathElement::typeAccount,
                            it->account,
                            uEndCurrency,
                            it->account);
                        incompletePaths.assembleAdd (currentPath, pathElement);
                        ++it;
                    }
                }

            }
            else
            {
                WriteLog (lsWARNING, Pathfinder)
                    << "Path ends on non-existent issuer";
            }
        }
    }
    if (addFlags & afADD_BOOKS)
    {
        // add order books
        if (addFlags & afOB_XDV)
        {
            // to XDV only
            if (!bOnXDV && getApp ().getOrderBookDB ().isBookToXDV (
                    {uEndCurrency, uEndIssuer}))
            {
                STPathElement pathElement(
                    STPathElement::typeCurrency,
                    xdvAccount (),
                    xdvCurrency (),
                    xdvAccount ());
                incompletePaths.assembleAdd (currentPath, pathElement);
            }
        }
        else
        {
            bool bDestOnly = (addFlags & afOB_LAST) != 0;
            auto books = getApp ().getOrderBookDB ().getBooksByTakerPays(
                {uEndCurrency, uEndIssuer});
            WriteLog (lsTRACE, Pathfinder)
                << books.size () << " books found from this currency/issuer";

            for (auto const& book : books)
            {
                if (!currentPath.hasSeen (
                        xdvAccount(),
                        book->getCurrencyOut (),
                        book->getIssuerOut ()) &&
                    !issueMatchesOrigin (book->book ().out) &&
                    (!bDestOnly ||
                     (book->getCurrencyOut () == mDstAmount.getCurrency ())))
                {
                    STPath newPath (currentPath);

                    if (book->getCurrencyOut().isZero())
                    { // to XDV

                        // add the order book itself
                        newPath.emplace_back (
                            STPathElement::typeCurrency,
                            xdvAccount (),
                            xdvCurrency (),
                            xdvAccount ());

                        if (mDstAmount.getCurrency ().isZero ())
                        {
                            // destination is XDV, add account and path is
                            // complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found bx: "
                                << currentPath.getJson(0);
                            addUniquePath (mCompletePaths, newPath);
                        }
                        else
                            incompletePaths.push_back (newPath);
                    }
                    else if (!currentPath.hasSeen(
                        book->getIssuerOut (),
                        book->getCurrencyOut (),
                        book->getIssuerOut ()))
                    {
                        // Don't want the book if we've already seen the issuer
                        // book -> account -> book
                        if ((newPath.size() >= 2) &&
                            (newPath.back().isAccount ()) &&
                            (newPath[newPath.size() - 2].isOffer ()))
                        {
                            // replace the redundant account with the order book
                            newPath[newPath.size() - 1] = STPathElement (
                                STPathElement::typeCurrency | STPathElement::typeIssuer,
                                xdvAccount(), book->getCurrencyOut(),
                                book->getIssuerOut());
                        }
                        else
                        {
                            // add the order book
                            newPath.emplace_back(
                                STPathElement::typeCurrency | STPathElement::typeIssuer,
                                xdvAccount(), book->getCurrencyOut(),
                                book->getIssuerOut());
                        }

                        if (hasEffectiveDestination &&
                            book->getIssuerOut() == mDstAccount &&
                            book->getCurrencyOut() == mDstAmount.getCurrency())
                        {
                            // We skipped a required issuer
                        }
                        else if (book->getIssuerOut() == mEffectiveDst &&
                            book->getCurrencyOut() == mDstAmount.getCurrency())
                        { // with the destination account, this path is complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found ba: "
                                << currentPath.getJson(0);
                            addUniquePath (mCompletePaths, newPath);
                        }
                        else
                        {
                            // add issuer's account, path still incomplete
                            incompletePaths.assembleAdd(newPath,
                                STPathElement (STPathElement::typeAccount,
                                               book->getIssuerOut (),
                                               book->getCurrencyOut (),
                                               book->getIssuerOut ()));
                        }
                    }

                }
            }
        }
    }
}

namespace {

Pathfinder::PathType makePath (char const *string)
{
    Pathfinder::PathType ret;

    while (true)
    {
        switch (*string++)
        {
            case 's': // source
                ret.push_back (Pathfinder::nt_SOURCE);
                break;

            case 'a': // accounts
                ret.push_back (Pathfinder::nt_ACCOUNTS);
                break;

            case 'b': // books
                ret.push_back (Pathfinder::nt_BOOKS);
                break;

            case 'x': // xdv book
                ret.push_back (Pathfinder::nt_XDV_BOOK);
                break;

            case 'f': // book to final currency
                ret.push_back (Pathfinder::nt_DEST_BOOK);
                break;

            case 'd':
                // Destination (with account, if required and not already
                // present).
                ret.push_back (Pathfinder::nt_DESTINATION);
                break;

            case 0:
                return ret;
        }
    }
}

void fillPaths (Pathfinder::PaymentType type, PathCostList const& costs)
{
    auto& list = mPathTable[type];
    assert (list.empty());
    for (auto& cost: costs)
        list.push_back ({cost.cost, makePath (cost.path)});
}

} // namespace


// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most agressive

void Pathfinder::initPathTable ()
{
    // CAUTION: Do not include rules that build default paths

    mPathTable.clear();
    fillPaths (pt_XDV_to_XDV, {});

    fillPaths(
        pt_XDV_to_nonXDV, {
            {1, "sfd"},   // source -> book -> gateway
            {3, "sfad"},  // source -> book -> account -> destination
            {5, "sfaad"}, // source -> book -> account -> account -> destination
            {6, "sbfd"},  // source -> book -> book -> destination
            {8, "sbafd"}, // source -> book -> account -> book -> destination
            {9, "sbfad"}, // source -> book -> book -> account -> destination
            {10, "sbafad"}
        });

    fillPaths(
        pt_nonXDV_to_XDV, {
            {1, "sxd"},       // gateway buys XDV
            {2, "saxd"},      // source -> gateway -> book(XDV) -> dest
            {6, "saaxd"},
            {7, "sbxd"},
            {8, "sabxd"},
            {9, "sabaxd"}
        });

    // non-XDV to non-XDV (same currency)
    fillPaths(
        pt_nonXDV_to_same,  {
            {1, "sad"},     // source -> gateway -> destination
            {1, "sfd"},     // source -> book -> destination
            {4, "safd"},    // source -> gateway -> book -> destination
            {4, "sfad"},
            {5, "saad"},
            {5, "sbfd"},
            {6, "sxfad"},
            {6, "safad"},
            {6, "saxfd"},   // source -> gateway -> book to XDV -> book ->
                            // destination
            {6, "saxfad"},
            {6, "sabfd"},   // source -> gateway -> book -> book -> destination
            {7, "saaad"},
        });

    // non-XDV to non-XDV (different currency)
    fillPaths(
        pt_nonXDV_to_nonXDV, {
            {1, "sfad"},
            {1, "safd"},
            {3, "safad"},
            {4, "sxfd"},
            {5, "saxfd"},
            {5, "sxfad"},
            {5, "sbfd"},
            {6, "saxfad"},
            {6, "sabfd"},
            {7, "saafd"},
            {8, "saafad"},
            {9, "safaad"},
        });
}

} // divvy
