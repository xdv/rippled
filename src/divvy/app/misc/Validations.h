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

#ifndef RIPPLE_APP_MISC_VALIDATIONS_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATIONS_H_INCLUDED

#include <divvy/protocol/STValidation.h>
#include <beast/cxx14/memory.h> // <memory>
#include <vector>

namespace divvy {

// VFALCO TODO rename and move these type aliases into the Validations interface

// nodes validating and highest node ID validating
using ValidationSet = hash_map<NodeID, STValidation::pointer>;

using ValidationCounter = std::pair<int, NodeID>;
using LedgerToValidationCounter = hash_map<uint256, ValidationCounter>;
using ValidationVector = std::vector<STValidation::pointer>;

class Validations
{
public:

    virtual ~Validations () { }

    virtual bool addValidation (STValidation::ref, std::string const& source) = 0;

    virtual ValidationSet getValidations (uint256 const& ledger) = 0;

    virtual void getValidationCount (
        uint256 const& ledger, bool currentOnly, int& trusted,
        int& untrusted) = 0;
    virtual void getValidationTypes (
        uint256 const& ledger, int& full, int& partial) = 0;

    virtual int getTrustedValidationCount (uint256 const& ledger) = 0;

    /** Returns fees reported by trusted validators in the given ledger. */
    virtual
    std::vector <std::uint64_t>
    fees (uint256 const& ledger, std::uint64_t base) = 0;

    virtual int getNodesAfter (uint256 const& ledger) = 0;
    virtual int getLoadRatio (bool overLoaded) = 0;

    // VFALCO TODO make a type alias for this ugly return value!
    virtual LedgerToValidationCounter getCurrentValidations (
        uint256 currentLedger, uint256 previousLedger) = 0;

    /** Return the times of all validations for a particular ledger hash. */
    virtual std::vector<std::uint32_t> getValidationTimes (
        uint256 const& ledger) = 0;

    virtual std::list <STValidation::pointer>
    getCurrentTrustedValidations () = 0;

    virtual void tune (int size, int age) = 0;

    virtual void flush () = 0;

    virtual void sweep () = 0;
};

std::unique_ptr <Validations> make_Validations ();

} // divvy

#endif
