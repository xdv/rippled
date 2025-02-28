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

#ifndef RIPPLE_TEST_JTX_UTILITY_H_INCLUDED
#define RIPPLE_TEST_JTX_UTILITY_H_INCLUDED

#include <divvy/test/jtx/Account.h>
#include <divvy/json/json_value.h>
#include <divvy/app/ledger/Ledger.h>
#include <divvy/protocol/STObject.h>
#include <stdexcept>

namespace divvy {
namespace test {
namespace jtx {

/** Thrown when parse fails. */
struct parse_error : std::logic_error
{
    template <class String>
    explicit
    parse_error (String const& s)
        : logic_error(s)
    {
    }
};

/** Convert JSON to STObject.
    This throws on failure, the JSON must be correct.
    @note Testing malformed JSON is beyond the scope of
          this set of unit test routines.
*/
STObject
parse (Json::Value const& jv);

/** Sign automatically.
    @note This only works on accounts with multi-signing off.
*/
void
sign (Json::Value& jv,
    Account const& account);

/** Set the fee automatically. */
void
fill_fee (Json::Value& jv,
    Ledger const& ledger);

/** Set the sequence number automatically. */
void
fill_seq (Json::Value& jv,
    Ledger const& ledger);

} // jtx
} // test
} // divvy

#endif
