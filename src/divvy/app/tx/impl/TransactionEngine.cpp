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
#include <divvy/app/tx/TransactionEngine.h>
#include <divvy/app/tx/impl/Transactor.h>
#include <divvy/basics/Log.h>
#include <divvy/json/to_string.h>
#include <divvy/protocol/Indexes.h>
#include <cassert>

namespace divvy {

//
// XXX Make sure all fields are recognized in transactions.
//

std::pair<TER, bool>
TransactionEngine::applyTransaction (
    STTx const& txn,
    TransactionEngineParams params)
{
    TER terResult;
    bool didApply = false;

    try
    {
        assert (mLedger);

        WriteLog (lsTRACE, TransactionEngine) << "applyTransaction>";

        uint256 const& txID = txn.getTransactionID ();

        if (!txID)
        {
            WriteLog (lsWARNING, TransactionEngine) <<
                "applyTransaction: invalid transaction id";
            return std::make_pair(temINVALID_FLAG, false);
        }

        mNodes.emplace(mLedger, txID,
            mLedger->getLedgerSeq(), params);

    #ifdef BEAST_DEBUG
        if (1)
        {
            Serializer ser;
            txn.add (ser);
            SerialIter sit(ser.slice());
            STTx s2 (sit);

            if (!s2.isEquivalent (txn))
            {
                WriteLog (lsFATAL, TransactionEngine) <<
                    "Transaction serdes mismatch";
                WriteLog (lsINFO, TransactionEngine) << txn.getJson (0);
                WriteLog (lsFATAL, TransactionEngine) << s2.getJson (0);
                assert (false);
            }
        }
    #endif

        terResult = Transactor::transact (txn, params, this);

        if (terResult == temUNKNOWN)
        {
            WriteLog (lsWARNING, TransactionEngine) <<
                "applyTransaction: Invalid transaction: unknown transaction type";
            return std::make_pair(temUNKNOWN, false);
        }

        if (ShouldLog (lsDEBUG, TransactionEngine))
        {
            std::string strToken;
            std::string strHuman;

            transResultInfo (terResult, strToken, strHuman);

            WriteLog (lsDEBUG, TransactionEngine) <<
                "applyTransaction: terResult=" << strToken <<
                " : " << terResult <<
                " : " << strHuman;
        }

        didApply = isTesSuccess (terResult);

        if (isTecClaim (terResult) && !(params & tapRETRY))
        {
            // only claim the transaction fee
            WriteLog (lsDEBUG, TransactionEngine) <<
                "Reprocessing tx " << txID << " to only claim fee";
            mNodes.emplace(mLedger, txID,
                mLedger->getLedgerSeq(), params);

            SLE::pointer txnAcct = mNodes->entryCache (ltACCOUNT_ROOT,
                getAccountRootIndex (txn.getSourceAccount ()));

            if (!txnAcct)
                terResult = terNO_ACCOUNT;
            else
            {
                std::uint32_t t_seq = txn.getSequence ();
                std::uint32_t a_seq = txnAcct->getFieldU32 (sfSequence);

                if (a_seq < t_seq)
                    terResult = terPRE_SEQ;
                else if (a_seq > t_seq)
                    terResult = tefPAST_SEQ;
                else
                {
                    STAmount fee        = txn.getTransactionFee ();
                    STAmount balance    = txnAcct->getFieldAmount (sfBalance);

                    // We retry/reject the transaction if the account
                    // balance is zero or we're applying against an open
                    // ledger and the balance is less than the fee
                    if ((balance == zero) ||
                        ((params & tapOPEN_LEDGER) && (balance < fee)))
                    {
                        // Account has no funds or ledger is open
                        terResult = terINSUF_FEE_B;
                    }
                    else
                    {
                        if (fee > balance)
                            fee = balance;
                        txnAcct->setFieldAmount (sfBalance, balance - fee);
                        txnAcct->setFieldU32 (sfSequence, t_seq + 1);
                        mNodes->entryModify (txnAcct);
                        didApply = true;
                    }
                }
            }
        }
        else if (!didApply)
        {
            WriteLog (lsDEBUG, TransactionEngine) << "Not applying transaction " << txID;
        }

        if (didApply && !checkInvariants (terResult, txn, params))
        {
            WriteLog (lsFATAL, TransactionEngine) <<
                "Transaction violates invariants";
            WriteLog (lsFATAL, TransactionEngine) <<
                txn.getJson (0);
            WriteLog (lsFATAL, TransactionEngine) <<
                transToken (terResult) << ": " << transHuman (terResult);
            WriteLog (lsFATAL, TransactionEngine) <<
                mNodes->getJson (0);
            didApply = false;
            terResult = tefINTERNAL;
        }

        if (didApply)
        {
            // Transaction succeeded fully or (retries are not allowed and the
            // transaction could claim a fee)
            Serializer m;
            mNodes->calcRawMeta (m, terResult, mTxnSeq++);

            assert(mLedger == mNodes->getLedger());
            mNodes->apply();

            Serializer s;
            txn.add (s);

            if (params & tapOPEN_LEDGER)
            {
                if (!mLedger->addTransaction (txID, s))
                {
                    WriteLog (lsFATAL, TransactionEngine) <<
                        "Duplicate transaction applied";
                    assert (false);
                    throw std::runtime_error ("Duplicate transaction applied");
                }
            }
            else
            {
                if (!mLedger->addTransaction (txID, s, m))
                {
                    WriteLog (lsFATAL, TransactionEngine) <<
                        "Duplicate transaction applied to closed ledger";
                    assert (false);
                    throw std::runtime_error ("Duplicate transaction applied to closed ledger");
                }

                // Charge whatever fee they specified. We break the encapsulation of
                // STAmount here and use "special knowledge" - namely that a native
                // amount is stored fully in the mantissa:
                auto const fee = txn.getTransactionFee ();

                // The transactor guarantees these will never trigger
                if (!fee.native () || fee.negative ())
                    throw std::runtime_error ("amount is negative!");

                if (fee != zero)
                    mLedger->destroyCoins (fee.mantissa ());
            }
        }

        mNodes = boost::none;

        if (!(params & tapOPEN_LEDGER) && isTemMalformed (terResult))
        {
            // XXX Malformed or failed transaction in closed ledger must bow out.
        }
    }
    catch(std::exception const& e)
    {
        WriteLog (lsFATAL, TransactionEngine) <<
            "Caught exception: " << e.what();
        return { tefEXCEPTION, false };
    }
    catch(...)
    {
        WriteLog (lsFATAL, TransactionEngine) <<
            "Caught unknown exception";
        return { tefEXCEPTION, false };
    }

    return { terResult, didApply };
}

bool
TransactionEngine::checkInvariants (
    TER result,
    STTx const& txn,
    TransactionEngineParams params)
{
    // VFALCO I deleted a bunch of code that was wrapped in #if 0.
    //        If you need it, check the commit log.

    return true;
}

} // divvy
