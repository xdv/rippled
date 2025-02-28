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
#include <divvy/app/misc/AmendmentTable.h>
#include <divvy/app/misc/NetworkOPs.h>
#include <divvy/app/tx/impl/Transactor.h>
#include <divvy/basics/Log.h>
#include <divvy/protocol/Indexes.h>

namespace divvy {

class Change
    : public Transactor
{
public:
    Change (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("Change"))
    {
    }

    TER doApply () override
    {
        if (mTxn.getTxnType () == ttAMENDMENT)
            return applyAmendment ();

        if (mTxn.getTxnType () == ttFEE)
            return applyFee ();

        return temUNKNOWN;
    }

    TER checkSign () override
    {
        if (mTxn.getFieldAccount160 (sfAccount).isNonZero ())
        {
            m_journal.warning << "Bad source account";
            return temBAD_SRC_ACCOUNT;
        }

        if (!mTxn.getSigningPubKey ().empty () || !mTxn.getSignature ().empty ())
        {
            m_journal.warning << "Bad signature";
            return temBAD_SIGNATURE;
        }

        return tesSUCCESS;
    }

    TER checkSeq () override
    {
        if ((mTxn.getSequence () != 0) || mTxn.isFieldPresent (sfPreviousTxnID))
        {
            m_journal.warning << "Bad sequence";
            return temBAD_SEQUENCE;
        }

        return tesSUCCESS;
    }

    TER payFee () override
    {
        if (mTxn.getTransactionFee () != STAmount ())
        {
            m_journal.warning << "Non-zero fee";
            return temBAD_FEE;
        }

        return tesSUCCESS;
    }

    TER preCheck () override
    {
        mTxnAccountID = mTxn.getSourceAccount ().getAccountID ();

        if (mTxnAccountID.isNonZero ())
        {
            m_journal.warning << "Bad source id";
            return temBAD_SRC_ACCOUNT;
        }

        if (mParams & tapOPEN_LEDGER)
        {
            m_journal.warning << "Change transaction against open ledger";
            return temINVALID;
        }

        return tesSUCCESS;
    }

private:
    TER applyAmendment ()
    {
        uint256 amendment (mTxn.getFieldH256 (sfAmendment));

        auto const index = getLedgerAmendmentIndex ();

        SLE::pointer amendmentObject (mEngine->view().entryCache (ltAMENDMENTS, index));

        if (!amendmentObject)
        {
            amendmentObject = std::make_shared<SLE>(
                ltAMENDMENTS, index);
            mEngine->view().entryCreate(amendmentObject);
        }

        STVector256 amendments =
            amendmentObject->getFieldV256(sfAmendments);

        if (std::find (amendments.begin(), amendments.end(),
                amendment) != amendments.end ())
            return tefALREADY;

        amendments.push_back (amendment);
        amendmentObject->setFieldV256 (sfAmendments, amendments);
        mEngine->view().entryModify (amendmentObject);

        getApp().getAmendmentTable ().enable (amendment);

        if (!getApp().getAmendmentTable ().isSupported (amendment))
            getApp().getOPs ().setAmendmentBlocked ();

        return tesSUCCESS;
    }

    TER applyFee ()
    {
        auto const index = getLedgerFeeIndex ();

        SLE::pointer feeObject = mEngine->view().entryCache (ltFEE_SETTINGS, index);

        if (!feeObject)
        {
            feeObject = std::make_shared<SLE>(
                ltFEE_SETTINGS, index);
            mEngine->view().entryCreate(feeObject);
        }

        // VFALCO-FIXME this generates errors
        // m_journal.trace <<
        //     "Previous fee object: " << feeObject->getJson (0);

        feeObject->setFieldU64 (
            sfBaseFee, mTxn.getFieldU64 (sfBaseFee));
        feeObject->setFieldU32 (
            sfReferenceFeeUnits, mTxn.getFieldU32 (sfReferenceFeeUnits));
        feeObject->setFieldU32 (
            sfReserveBase, mTxn.getFieldU32 (sfReserveBase));
        feeObject->setFieldU32 (
            sfReserveIncrement, mTxn.getFieldU32 (sfReserveIncrement));

        mEngine->view().entryModify (feeObject);

        // VFALCO-FIXME this generates errors
        // m_journal.trace <<
        //     "New fee object: " << feeObject->getJson (0);
        m_journal.warning << "Fees have been changed";
        return tesSUCCESS;
    }

    // VFALCO TODO Can this be removed?
    bool mustHaveValidAccount () override
    {
        return false;
    }
};


TER
transact_Change (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return Change (txn, params, engine).apply ();
}

}
