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

#ifndef RIPPLE_APP_BOOK_BOOKTIP_H_INCLUDED
#define RIPPLE_APP_BOOK_BOOKTIP_H_INCLUDED

#include <divvy/protocol/Quality.h>
#include <divvy/app/ledger/LedgerEntrySet.h>
#include <divvy/protocol/Indexes.h>
#include <beast/utility/noexcept.h>

#include <functional>

namespace divvy {

/** Iterates and consumes raw offers in an order book.
    Offers are presented from highest quality to lowest quality. This will
    return all offers present including missing, invalid, unfunded, etc.
*/
class BookTip
{
private:
    std::reference_wrapper <LedgerView> m_view;
    bool m_valid;
    uint256 m_book;
    uint256 m_end;
    uint256 m_dir;
    uint256 m_index;
    SLE::pointer m_entry;
    Quality m_quality;

    LedgerView&
    view() const noexcept
    {
        return m_view;
    }

public:
    /** Create the iterator. */
    BookTip (LedgerView& view, BookRef book);

    uint256 const&
    dir() const noexcept
    {
        return m_dir;
    }

    uint256 const&
    index() const noexcept
    {
        return m_index;
    }

    Quality const&
    quality() const noexcept
    {
        return m_quality;
    }

    SLE::pointer const&
    entry() const noexcept
    {
        return m_entry;
    }

    /** Erases the current offer and advance to the next offer.
        Complexity: Constant
        @return `true` if there is a next offer
    */
    bool
    step ();
};

}

#endif
