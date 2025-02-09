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

#ifndef RIPPLE_APP_TX_TRANSACTIONENGINE_H_INCLUDED
#define RIPPLE_APP_TX_TRANSACTIONENGINE_H_INCLUDED

#include <divvy/app/ledger/Ledger.h>
#include <divvy/app/ledger/LedgerEntrySet.h>
#include <boost/optional.hpp>
#include <utility>

namespace divvy {

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

struct tx_enable_test_t { tx_enable_test_t() { } };
static tx_enable_test_t const tx_enable_test;

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
    bool enableMultiSign_ =
#if RIPPLE_ENABLE_MULTI_SIGN
        true;
#else
        false;
#endif

    bool enableTickets_ =
#if RIPPLE_ENABLE_TICKETS
        true;
#else
        false;
#endif

    boost::optional<LedgerEntrySet> mNodes;

    void txnWrite();

protected:
    Ledger::pointer mLedger;
    int mTxnSeq = 0;

public:
    TransactionEngine() = default;

    explicit
    TransactionEngine (Ledger::ref ledger)
        : mLedger (ledger)
    {
        assert (mLedger);
    }

    TransactionEngine (Ledger::ref ledger,
            tx_enable_test_t)
        : enableMultiSign_(true)
        , enableTickets_(true)
        , mLedger (ledger)
    {
        assert (mLedger);
    }

    bool
    enableMultiSign() const
    {
        return enableMultiSign_;
    }

    bool
    enableTickets() const
    {
        return enableTickets_;
    }

    LedgerEntrySet&
    view ()
    {
        return *mNodes;
    }

    Ledger::ref
    getLedger ()
    {
        return mLedger;
    }

    void
    setLedger (Ledger::ref ledger)
    {
        assert (ledger);
        mLedger = ledger;
    }

    std::pair<TER, bool>
    applyTransaction (STTx const&, TransactionEngineParams);

    bool
    checkInvariants (TER result, STTx const& txn, TransactionEngineParams params);
};

inline TransactionEngineParams operator| (const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
    return static_cast<TransactionEngineParams> (static_cast<int> (l1) | static_cast<int> (l2));
}

inline TransactionEngineParams operator& (const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
    return static_cast<TransactionEngineParams> (static_cast<int> (l1) & static_cast<int> (l2));
}

} // divvy

#endif
