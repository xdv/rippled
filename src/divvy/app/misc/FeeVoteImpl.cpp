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
#include <divvy/app/misc/FeeVote.h>
#include <divvy/app/main/Application.h>
#include <divvy/app/misc/Validations.h>
#include <divvy/basics/BasicConfig.h>
#include <beast/utility/Journal.h>

namespace divvy {

namespace detail {

template <typename Integer>
class VotableInteger
{
private:
    using map_type = std::map <Integer, int>;
    Integer mCurrent;   // The current setting
    Integer mTarget;    // The setting we want
    map_type mVoteMap;

public:
    VotableInteger (Integer current, Integer target)
        : mCurrent (current)
        , mTarget (target)
    {
        // Add our vote
        ++mVoteMap[mTarget];
    }

    void
    addVote(Integer vote)
    {
        ++mVoteMap[vote];
    }

    void
    noVote()
    {
        addVote (mCurrent);
    }

    Integer
    getVotes() const;
};

template <class Integer>
Integer
VotableInteger <Integer>::getVotes() const
{
    Integer ourVote = mCurrent;
    int weight = 0;
    for (auto const& e : mVoteMap)
    {
        // Take most voted value between current and target, inclusive
        if ((e.first <= std::max (mTarget, mCurrent)) &&
                (e.first >= std::min (mTarget, mCurrent)) &&
                (e.second > weight))
        {
            ourVote = e.first;
            weight = e.second;
        }
    }

    return ourVote;
}

}

//------------------------------------------------------------------------------

class FeeVoteImpl : public FeeVote
{
private:
    Setup target_;
    beast::Journal journal_;

public:
    FeeVoteImpl (Setup const& setup, beast::Journal journal);

    void
    doValidation (Ledger::ref lastClosedLedger,
        STObject& baseValidation) override;

    void
    doVoting (Ledger::ref lastClosedLedger,
        std::shared_ptr<SHAMap> const& initialPosition) override;
};

//--------------------------------------------------------------------------

FeeVoteImpl::FeeVoteImpl (Setup const& setup, beast::Journal journal)
    : target_ (setup)
    , journal_ (journal)
{
}

void
FeeVoteImpl::doValidation (Ledger::ref lastClosedLedger,
    STObject& baseValidation)
{
    if (lastClosedLedger->getBaseFee () != target_.reference_fee)
    {
        if (journal_.info) journal_.info <<
            "Voting for base fee of " << target_.reference_fee;

        baseValidation.setFieldU64 (sfBaseFee, target_.reference_fee);
    }

    if (lastClosedLedger->getReserve (0) != target_.account_reserve)
    {
        if (journal_.info) journal_.info <<
            "Voting for base resrve of " << target_.account_reserve;

        baseValidation.setFieldU32(sfReserveBase, target_.account_reserve);
    }

    if (lastClosedLedger->getReserveInc () != target_.owner_reserve)
    {
        if (journal_.info) journal_.info <<
            "Voting for reserve increment of " << target_.owner_reserve;

        baseValidation.setFieldU32 (sfReserveIncrement,
            target_.owner_reserve);
    }
}

void
FeeVoteImpl::doVoting (Ledger::ref lastClosedLedger,
    std::shared_ptr<SHAMap> const& initialPosition)
{
    // LCL must be flag ledger
    assert ((lastClosedLedger->getLedgerSeq () % 256) == 0);

    detail::VotableInteger<std::uint64_t> baseFeeVote (
        lastClosedLedger->getBaseFee (), target_.reference_fee);

    detail::VotableInteger<std::uint32_t> baseReserveVote (
        lastClosedLedger->getReserve (0), target_.account_reserve);

    detail::VotableInteger<std::uint32_t> incReserveVote (
        lastClosedLedger->getReserveInc (), target_.owner_reserve);

    // get validations for ledger before flag
    ValidationSet const set =
        getApp().getValidations ().getValidations (
            lastClosedLedger->getParentHash ());
    for (auto const& e : set)
    {
        STValidation const& val = *e.second;

        if (val.isTrusted ())
        {
            if (val.isFieldPresent (sfBaseFee))
            {
                baseFeeVote.addVote (val.getFieldU64 (sfBaseFee));
            }
            else
            {
                baseFeeVote.noVote ();
            }

            if (val.isFieldPresent (sfReserveBase))
            {
                baseReserveVote.addVote (val.getFieldU32 (sfReserveBase));
            }
            else
            {
                baseReserveVote.noVote ();
            }

            if (val.isFieldPresent (sfReserveIncrement))
            {
                incReserveVote.addVote (val.getFieldU32 (sfReserveIncrement));
            }
            else
            {
                incReserveVote.noVote ();
            }
        }
    }

    // choose our positions
    std::uint64_t const baseFee = baseFeeVote.getVotes ();
    std::uint32_t const baseReserve = baseReserveVote.getVotes ();
    std::uint32_t const incReserve = incReserveVote.getVotes ();

    // add transactions to our position
    if ((baseFee != lastClosedLedger->getBaseFee ()) ||
            (baseReserve != lastClosedLedger->getReserve (0)) ||
            (incReserve != lastClosedLedger->getReserveInc ()))
    {
        if (journal_.warning) journal_.warning <<
            "We are voting for a fee change: " << baseFee <<
            "/" << baseReserve <<
            "/" << incReserve;

        STTx trans (ttFEE);
        trans.setFieldAccount (sfAccount, AccountID ());
        trans.setFieldU64 (sfBaseFee, baseFee);
        trans.setFieldU32 (sfReferenceFeeUnits, 10);
        trans.setFieldU32 (sfReserveBase, baseReserve);
        trans.setFieldU32 (sfReserveIncrement, incReserve);

        uint256 txID = trans.getTransactionID ();

        if (journal_.warning)
            journal_.warning << "Vote: " << txID;

        Serializer s;
        trans.add (s);

        auto tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (journal_.warning) journal_.warning <<
                "Ledger already had fee change";
        }
    }
}

//------------------------------------------------------------------------------

FeeVote::Setup
setup_FeeVote (Section const& section)
{
    FeeVote::Setup setup;
    set (setup.reference_fee, "reference_fee", section);
    set (setup.account_reserve, "account_reserve", section);
    set (setup.owner_reserve, "owner_reserve", section);
    return setup;
}

std::unique_ptr<FeeVote>
make_FeeVote (FeeVote::Setup const& setup, beast::Journal journal)
{
    return std::make_unique<FeeVoteImpl> (setup, journal);
}

} // divvy
