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
#include <divvy/app/misc/Validations.h>
#include <divvy/core/DatabaseCon.h>
#include <divvy/app/ledger/LedgerMaster.h>
#include <divvy/app/ledger/LedgerTiming.h>
#include <divvy/app/main/Application.h>
#include <divvy/app/misc/NetworkOPs.h>
#include <divvy/app/misc/UniqueNodeList.h>
#include <divvy/basics/Log.h>
#include <divvy/basics/StringUtilities.h>
#include <divvy/basics/seconds_clock.h>
#include <divvy/core/JobQueue.h>
#include <beast/cxx14/memory.h> // <memory>
#include <mutex>
#include <thread>

namespace divvy {

class ValidationsImp : public Validations
{
private:
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    using ScopedUnlockType = beast::GenericScopedUnlock <LockType>;
    std::mutex mutable mLock;

    TaggedCache<uint256, ValidationSet> mValidations;
    ValidationSet mCurrentValidations;
    ValidationVector mStaleValidations;

    bool mWriting;

private:
    std::shared_ptr<ValidationSet> findCreateSet (uint256 const& ledgerHash)
    {
        auto j = mValidations.fetch (ledgerHash);

        if (!j)
        {
            j = std::make_shared<ValidationSet> ();
            mValidations.canonicalize (ledgerHash, j);
        }

        return j;
    }

    std::shared_ptr<ValidationSet> findSet (uint256 const& ledgerHash)
    {
        return mValidations.fetch (ledgerHash);
    }

public:
    ValidationsImp ()
        : mValidations ("Validations", 128, 600, get_seconds_clock (),
            deprecatedLogs().journal("TaggedCache"))
        , mWriting (false)
    {
        mStaleValidations.reserve (512);
    }

private:
    bool addValidation (STValidation::ref val, std::string const& source)
    {
        DivvyAddress signer = val->getSignerPublic ();
        bool isCurrent = false;

        if (!val->isTrusted() && getApp().getUNL().nodeInUNL (signer))
            val->setTrusted();

        std::uint32_t now = getApp().getOPs().getCloseTimeNC();
        std::uint32_t valClose = val->getSignTime();

        if ((now > (valClose - LEDGER_EARLY_INTERVAL)) && (now < (valClose + LEDGER_VAL_INTERVAL)))
            isCurrent = true;
        else
            WriteLog (lsWARNING, Validations) << "Received stale validation now=" << now << ", close=" << valClose;

        if (!val->isTrusted ())
        {
            WriteLog (lsDEBUG, Validations) << "Node " << signer.humanNodePublic () << " not in UNL st=" << val->getSignTime () <<
                                            ", hash=" << val->getLedgerHash () << ", shash=" << val->getSigningHash () << " src=" << source;
        }

        auto hash = val->getLedgerHash ();
        auto node = signer.getNodeID ();

        if (val->isTrusted () && isCurrent)
        {
            ScopedLockType sl (mLock);

            if (!findCreateSet (hash)->insert (std::make_pair (node, val)).second)
                return false;

            auto it = mCurrentValidations.find (node);

            if (it == mCurrentValidations.end ())
            {
                // No previous validation from this validator
                mCurrentValidations.emplace (node, val);
            }
            else if (!it->second)
            {
                // Previous validation has expired
                it->second = val;
            }
            else if (val->getSignTime () > it->second->getSignTime ())
            {
                // This is a newer validation
                val->setPreviousHash (it->second->getLedgerHash ());
                mStaleValidations.push_back (it->second);
                it->second = val;
                condWrite ();
            }
            else
            {
                // We already have a newer validation from this source
                isCurrent = false;
            }
        }

        WriteLog (lsDEBUG, Validations) << "Val for " << hash << " from " << signer.humanNodePublic ()
                                        << " added " << (val->isTrusted () ? "trusted/" : "UNtrusted/") << (isCurrent ? "current" : "stale");

        if (val->isTrusted () && isCurrent)
        {
            getApp().getLedgerMaster ().checkAccept (hash, val->getFieldU32 (sfLedgerSequence));
            return true;
        }

        // FIXME: This never forwards untrusted validations
        return false;
    }

    void tune (int size, int age)
    {
        mValidations.setTargetSize (size);
        mValidations.setTargetAge (age);
    }

    ValidationSet getValidations (uint256 const& ledger)
    {
        {
            ScopedLockType sl (mLock);
            auto set = findSet (ledger);

            if (set)
                return *set;
        }
        return ValidationSet ();
    }

    void getValidationCount (uint256 const& ledger, bool currentOnly, int& trusted, int& untrusted)
    {
        trusted = untrusted = 0;
        ScopedLockType sl (mLock);
        auto set = findSet (ledger);

        if (set)
        {
            std::uint32_t now = getApp().getOPs ().getNetworkTimeNC ();
            for (auto& it: *set)
            {
                bool isTrusted = it.second->isTrusted ();

                if (isTrusted && currentOnly)
                {
                    std::uint32_t closeTime = it.second->getSignTime ();

                    if ((now < (closeTime - LEDGER_EARLY_INTERVAL)) || (now > (closeTime + LEDGER_VAL_INTERVAL)))
                        isTrusted = false;
                    else
                    {
                        WriteLog (lsTRACE, Validations) << "VC: Untrusted due to time " << ledger;
                    }
                }

                if (isTrusted)
                    ++trusted;
                else
                    ++untrusted;
            }
        }

        WriteLog (lsTRACE, Validations) << "VC: " << ledger << "t:" << trusted << " u:" << untrusted;
    }

    void getValidationTypes (uint256 const& ledger, int& full, int& partial)
    {
        full = partial = 0;
        ScopedLockType sl (mLock);
        auto set = findSet (ledger);

        if (set)
        {
            for (auto& it:*set)
            {
                if (it.second->isTrusted ())
                {
                    if (it.second->isFull ())
                        ++full;
                    else
                        ++partial;
                }
            }
        }

        WriteLog (lsTRACE, Validations) << "VC: " << ledger << "f:" << full << " p:" << partial;
    }


    int getTrustedValidationCount (uint256 const& ledger)
    {
        int trusted = 0;
        ScopedLockType sl (mLock);
        auto set = findSet (ledger);

        if (set)
        {
            for (auto& it: *set)
            {
                if (it.second->isTrusted ())
                    ++trusted;
            }
        }

        return trusted;
    }

    std::vector <std::uint64_t>
    fees (uint256 const& ledger, std::uint64_t base) override
    {
        std::vector <std::uint64_t> result;
        std::lock_guard <std::mutex> lock (mLock);
        auto const set = findSet (ledger);
        if (set)
        {
            for (auto const& v : *set)
            {
                if (v.second->isTrusted())
                {
                    if (v.second->isFieldPresent(sfLoadFee))
                        result.push_back(v.second->getFieldU32(sfLoadFee));
                    else
                        result.push_back(base);
                }
            }
        }

        return result;
    }

    int getNodesAfter (uint256 const& ledger)
    {
        // Number of trusted nodes that have moved past this ledger
        int count = 0;
        ScopedLockType sl (mLock);
        for (auto& it: mCurrentValidations)
        {
            if (it.second->isTrusted () && it.second->isPreviousHash (ledger))
                ++count;
        }
        return count;
    }

    int getLoadRatio (bool overLoaded)
    {
        // how many trusted nodes are able to keep up, higher is better
        int goodNodes = overLoaded ? 1 : 0;
        int badNodes = overLoaded ? 0 : 1;
        {
            ScopedLockType sl (mLock);
            for (auto& it: mCurrentValidations)
            {
                if (it.second->isTrusted ())
                {
                    if (it.second->isFull ())
                        ++goodNodes;
                    else
                        ++badNodes;
                }
            }
        }
        return (goodNodes * 100) / (goodNodes + badNodes);
    }

    std::list<STValidation::pointer> getCurrentTrustedValidations ()
    {
        std::uint32_t cutoff = getApp().getOPs ().getNetworkTimeNC () - LEDGER_VAL_INTERVAL;

        std::list<STValidation::pointer> ret;

        ScopedLockType sl (mLock);
        auto it = mCurrentValidations.begin ();

        while (it != mCurrentValidations.end ())
        {
            if (!it->second) // contains no record
                it = mCurrentValidations.erase (it);
            else if (it->second->getSignTime () < cutoff)
            {
                // contains a stale record
                mStaleValidations.push_back (it->second);
                it->second.reset ();
                condWrite ();
                it = mCurrentValidations.erase (it);
            }
            else
            {
                // contains a live record
                if (it->second->isTrusted ())
                    ret.push_back (it->second);

                ++it;
            }
        }

        return ret;
    }

    LedgerToValidationCounter getCurrentValidations (
        uint256 currentLedger, uint256 priorLedger)
    {
        std::uint32_t cutoff = getApp().getOPs ().getNetworkTimeNC () - LEDGER_VAL_INTERVAL;
        bool valCurrentLedger = currentLedger.isNonZero ();
        bool valPriorLedger = priorLedger.isNonZero ();

        LedgerToValidationCounter ret;

        ScopedLockType sl (mLock);
        auto it = mCurrentValidations.begin ();

        while (it != mCurrentValidations.end ())
        {
            if (!it->second) // contains no record
                it = mCurrentValidations.erase (it);
            else if (it->second->getSignTime () < cutoff)
            {
                // contains a stale record
                mStaleValidations.push_back (it->second);
                it->second.reset ();
                condWrite ();
                it = mCurrentValidations.erase (it);
            }
            else
            {
                // contains a live record
                bool countPreferred = valCurrentLedger && (it->second->getLedgerHash () == currentLedger);

                if (!countPreferred && // allow up to one ledger slip in either direction
                        ((valCurrentLedger && it->second->isPreviousHash (currentLedger)) ||
                         (valPriorLedger && (it->second->getLedgerHash () == priorLedger))))
                {
                    countPreferred = true;
                    WriteLog (lsTRACE, Validations) << "Counting for " << currentLedger << " not " << it->second->getLedgerHash ();
                }

                ValidationCounter& p = countPreferred ? ret[currentLedger] : ret[it->second->getLedgerHash ()];
                ++ (p.first);
                auto ni = it->second->getNodeID ();

                if (ni > p.second)
                    p.second = ni;

                ++it;
            }
        }

        return ret;
    }

    std::vector<uint32_t>
    getValidationTimes (uint256 const& hash)
    {
        std::vector <std::uint32_t> times;
        ScopedLockType sl (mLock);
        if (auto j = findSet (hash))
            for (auto& it : *j)
                if (it.second->isTrusted())
                    times.push_back (it.second->getSignTime());
        return times;
    }

    void flush ()
    {
        bool anyNew = false;

        WriteLog (lsINFO, Validations) << "Flushing validations";
        ScopedLockType sl (mLock);
        for (auto& it: mCurrentValidations)
        {
            if (it.second)
                mStaleValidations.push_back (it.second);

            anyNew = true;
        }
        mCurrentValidations.clear ();

        if (anyNew)
            condWrite ();

        while (mWriting)
        {
            ScopedUnlockType sul (mLock);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }

        WriteLog (lsDEBUG, Validations) << "Validations flushed";
    }

    void condWrite ()
    {
        if (mWriting)
            return;

        mWriting = true;
        getApp().getJobQueue ().addJob (jtWRITE, "Validations::doWrite",
                                       std::bind (&ValidationsImp::doWrite,
                                                  this, std::placeholders::_1));
    }

    void doWrite (Job&)
    {
        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtDISK, "ValidationWrite"));
        boost::format insVal ("INSERT INTO Validations "
                              "(LedgerHash,NodePubKey,SignTime,RawData) VALUES ('%s','%s','%u',%s);");

        ScopedLockType sl (mLock);
        assert (mWriting);

        while (!mStaleValidations.empty ())
        {
            ValidationVector vector;
            vector.reserve (512);
            mStaleValidations.swap (vector);

            {
                ScopedUnlockType sul (mLock);
                {
                    auto db = getApp().getLedgerDB ().checkoutDb ();

                    Serializer s (1024);
                    soci::transaction tr(*db);
                    for (auto it: vector)
                    {
                        s.erase ();
                        it->add (s);
                        *db << boost::str (
                            insVal % to_string (it->getLedgerHash ()) %
                            it->getSignerPublic ().humanNodePublic () %
                            it->getSignTime () % sqlEscape (s.peekData ()));
                    }

                    tr.commit ();
                }
            }
        }

        mWriting = false;
    }

    void sweep ()
    {
        ScopedLockType sl (mLock);
        mValidations.sweep ();
    }
};

std::unique_ptr <Validations> make_Validations ()
{
    return std::make_unique <ValidationsImp> ();
}

} // divvy
