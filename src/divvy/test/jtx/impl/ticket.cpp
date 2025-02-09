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
#include <divvy/test/jtx/ticket.h>
#include <divvy/protocol/JsonFields.h>

namespace divvy {
namespace test {
namespace jtx {

namespace ticket {

namespace detail {

Json::Value
create (Account const& account,
    boost::optional<Account> const& target,
        boost::optional<std::uint32_t> const& expire)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "TicketCreate";
    if (expire)
        jv["Expiration"] = *expire;
    if (target)
        jv["Target"] = target->human();
    return jv;
}

} // detail

} // ticket

} // jtx
} // test
} // divvy
