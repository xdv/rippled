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
#include <divvy/app/tx/impl/SignerEntries.h>
#include <divvy/protocol/STObject.h>
#include <divvy/protocol/STArray.h>
#include <divvy/protocol/STAccount.h>
#include <cstdint>

namespace divvy {

SignerEntries::Decoded
SignerEntries::deserialize (
    STObject const& obj, beast::Journal journal, std::string const& annotation)
{
    Decoded s;
    auto& accountVec (s.vec);
    accountVec.reserve (maxEntries);

    if (!obj.isFieldPresent (sfSignerEntries))
    {
        if (journal.trace) journal.trace <<
            "Malformed " << annotation << ": Need signer entry array.";
        s.ter = temMALFORMED;
        return s;
    }

    STArray const& sEntries (obj.getFieldArray (sfSignerEntries));
    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        if (sEntry.getFName () != sfSignerEntry)
        {
            journal.trace <<
                "Malformed " << annotation << ": Expected SignerEntry.";
            s.ter = temMALFORMED;
            return s;
        }

        // Extract SignerEntry fields.
        AccountID const account = sEntry.getFieldAccount160 (sfAccount);
        std::uint16_t const weight = sEntry.getFieldU16 (sfSignerWeight);
        accountVec.emplace_back (account, weight);
    }

    s.ter = tesSUCCESS;
    return s;
}

} // divvy
