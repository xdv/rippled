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
#include <divvy/shamap/SHAMapItem.h>
    
namespace divvy {

class SHAMap;

SHAMapItem::SHAMapItem (uint256 const& tag, Blob const& data)
    : mTag (tag)
    , mData (data.data(), data.size())
{
}

SHAMapItem::SHAMapItem (uint256 const& tag, const Serializer& data)
    : mTag (tag)
    , mData (data.data(), data.size())
{
}

// VFALCO This function appears not to be called
void SHAMapItem::dump (beast::Journal journal)
{
    if (journal.info) journal.info <<
        "SHAMapItem(" << mTag << ") " << mData.size () << "bytes";
}

} // divvy
