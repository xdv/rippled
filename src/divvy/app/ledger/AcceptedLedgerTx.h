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

#ifndef RIPPLE_APP_LEDGER_ACCEPTEDLEDGERTX_H_INCLUDED
#define RIPPLE_APP_LEDGER_ACCEPTEDLEDGERTX_H_INCLUDED

#include <divvy/app/ledger/Ledger.h>

namespace divvy {

/**
    A transaction that is in a closed ledger.

    Description

    An accepted ledger transaction contains additional information that the
    server needs to tell clients about the transaction. For example,
        - The transaction in JSON form
        - Which accounts are affected
          * This is used by InfoSub to report to clients
        - Cached stuff

    @code
    @endcode

    @see {uri}

    @ingroup divvy_ledger
*/
class AcceptedLedgerTx
{
public:
    using pointer = std::shared_ptr <AcceptedLedgerTx>;
    using ref = const pointer&;

public:
    AcceptedLedgerTx (Ledger::ref ledger, SerialIter& sit);
    AcceptedLedgerTx (Ledger::ref ledger, STTx::ref,
        TransactionMetaSet::ref);
    AcceptedLedgerTx (Ledger::ref ledger, STTx::ref, TER result);

    STTx::ref getTxn () const
    {
        return mTxn;
    }
    TransactionMetaSet::ref getMeta () const
    {
        return mMeta;
    }
    std::vector <DivvyAddress> const& getAffected () const
    {
        return mAffected;
    }

    TxID getTransactionID () const
    {
        return mTxn->getTransactionID ();
    }
    TxType getTxnType () const
    {
        return mTxn->getTxnType ();
    }
    TER getResult () const
    {
        return mResult;
    }
    std::uint32_t getTxnSeq () const
    {
        return mMeta->getIndex ();
    }

    bool isApplied () const
    {
        return bool(mMeta);
    }
    int getIndex () const
    {
        return mMeta ? mMeta->getIndex () : 0;
    }
    std::string getEscMeta () const;
    Json::Value getJson () const
    {
        return mJson;
    }

private:
    Ledger::pointer                 mLedger;
    STTx::pointer  mTxn;
    TransactionMetaSet::pointer     mMeta;
    TER                             mResult;
    std::vector <DivvyAddress>     mAffected;
    Blob        mRawMeta;
    Json::Value                     mJson;

    void buildJson ();
};

} // divvy

#endif
