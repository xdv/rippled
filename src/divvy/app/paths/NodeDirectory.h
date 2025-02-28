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

#ifndef RIPPLE_APP_PATHS_NODEDIRECTORY_H_INCLUDED
#define RIPPLE_APP_PATHS_NODEDIRECTORY_H_INCLUDED

#include <divvy/app/ledger/LedgerEntrySet.h>
#include <divvy/protocol/Indexes.h>

namespace divvy {

// VFALCO TODO de-inline these function definitions

class NodeDirectory {
  public:
    // Current directory - the last 64 bits of this are the quality.
    uint256 current;

    // Start of the next order book - one past the worst quality possible
    // for the current order book.
    uint256 next;

    // TODO(tom): directory.current and directory.next should be of type
    // Directory.

    bool advanceNeeded;  // Need to advance directory.
    bool restartNeeded;  // Need to restart directory.

    SLE::pointer ledgerEntry;

    void restart (bool multiQuality)
    {
        if (multiQuality)
            current = 0;             // Restart book searching.
        else
            restartNeeded  = true;   // Restart at same quality.
    }

    bool initialize (Book const& book, LedgerEntrySet& les)
    {
        if (current != zero)
            return false;

        current.copyFrom (getBookBase (book));
        next.copyFrom (getQualityNext (current));

        // TODO(tom): it seems impossible that any actual offers with
        // quality == 0 could occur - we should disallow them, and clear
        // directory.ledgerEntry without the database call in the next line.
        ledgerEntry = les.entryCache (ltDIR_NODE, current);

        // Advance, if didn't find it. Normal not to be unable to lookup
        // firstdirectory. Maybe even skip this lookup.
        advanceNeeded  = !ledgerEntry;
        restartNeeded  = false;

        // Associated vars are dirty, if found it.
        return bool(ledgerEntry);
    }

    enum Advance {NO_ADVANCE, NEW_QUALITY, END_ADVANCE};
    Advance advance(LedgerEntrySet& les)
    {
        if (!(advanceNeeded || restartNeeded))
            return NO_ADVANCE;

        // Get next quality.
        // The Merkel radix tree is ordered by key so we can go to the next
        // quality in O(1).
        if (advanceNeeded)
            current = les.getNextLedgerIndex (current, next);

        advanceNeeded  = false;
        restartNeeded  = false;

        if (current == zero)
            return END_ADVANCE;

        ledgerEntry = les.entryCache (ltDIR_NODE, current);
        return NEW_QUALITY;
    }
};

} // divvy

#endif
