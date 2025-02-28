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
#include <divvy/test/jtx/owners.h>

namespace divvy {
namespace test {
namespace jtx {

namespace detail {

std::uint32_t
owned_count_of(Ledger const& ledger,
    AccountID const& id,
        LedgerEntryType type)
{
    std::uint32_t count = 0;
    forEachItem(ledger, id, getApp().getSLECache(),
        [&count, type](std::shared_ptr<SLE const> const& sle)
        {
            if (sle->getType() == type)
                ++count;
        });
    return count;
}

void
owned_count_helper(Env const& env,
    AccountID const& id,
        LedgerEntryType type,
            std::uint32_t value)
{
    env.test.expect(owned_count_of(
        *env.ledger, id, type) == value);
}

} // detail

void
owners::operator()(Env const& env) const
{
    env.test.expect(env.le(
        account_)->getFieldU32(sfOwnerCount) ==
            value_);
}

} // jtx
} // test
} // divvy
