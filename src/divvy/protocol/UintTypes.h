//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_UINTTYPES_H_INCLUDED
#define RIPPLE_PROTOCOL_UINTTYPES_H_INCLUDED

#include <divvy/basics/UnorderedContainers.h>
#include <divvy/basics/base_uint.h>

namespace divvy {
namespace detail {

class AccountIDTag {};
class CurrencyTag {};
class DirectoryTag {};
class NodeIDTag {};

} // detail

/** Directory is an index into the directory of offer books.
    The last 64 bits of this are the quality. */
using Directory = base_uint<256, detail::DirectoryTag>;

/** AccountID is a hash representing a specific account. */
using AccountID = base_uint<160, detail::AccountIDTag>;

/** Currency is a hash representing a specific currency. */
using Currency = base_uint<160, detail::CurrencyTag>;

/** NodeID is a 160-bit hash representing one node. */
using NodeID = base_uint<160, detail::NodeIDTag>;

using CurrencySet = hash_set<Currency>;

/** A special account that's used as the "issuer" for XDV. */
AccountID const& xdvAccount();

/** XDV currency. */
Currency const& xdvCurrency();

/** A placeholder for empty accounts. */
AccountID const& noAccount();

/** A placeholder for empty currencies. */
Currency const& noCurrency();

/** We deliberately disallow the currency that looks like "XDV" because too
    many people were using it instead of the correct XDV currency. */


Currency const& badCurrency();

inline bool isXDV(Currency const& c)
{
    return c == zero;
}

inline bool isXDV(AccountID const& c)
{
    return c == zero;
}

/** Returns a human-readable form of the account. */
std::string to_string(AccountID const&);

/** Returns "", "XDV", or three letter ISO code. */
std::string to_string(Currency const& c);

/** Tries to convert a string to a Currency, returns true on success. */
bool to_currency(Currency&, std::string const&);

/** Tries to convert a string to a Currency, returns noCurrency() on failure. */
Currency to_currency(std::string const&);

/** Tries to convert a string to an AccountID representing an issuer, returns true
 * on success. */
bool to_issuer(AccountID&, std::string const&);

inline std::ostream& operator<< (std::ostream& os, AccountID const& x)
{
    os << to_string (x);
    return os;
}

inline std::ostream& operator<< (std::ostream& os, Currency const& x)
{
    os << to_string (x);
    return os;
}

} // divvy

namespace std {

template <>
struct hash <divvy::AccountID> : divvy::AccountID::hasher
{
};

template <>
struct hash <divvy::Currency> : divvy::Currency::hasher
{
};

template <>
struct hash <divvy::NodeID> : divvy::NodeID::hasher
{
};

template <>
struct hash <divvy::Directory> : divvy::Directory::hasher
{
};

} // std

#endif
