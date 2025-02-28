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
#include <divvy/protocol/InnerObjectFormats.h>

namespace divvy {

InnerObjectFormats::InnerObjectFormats ()
{
    add (sfSignerEntry.getJsonName ().c_str (), sfSignerEntry.getCode ())
        << SOElement (sfAccount,              SOE_REQUIRED)
        << SOElement (sfSignerWeight,         SOE_REQUIRED)
        ;

    add (sfSigningFor.getJsonName ().c_str (), sfSigningFor.getCode ())
        << SOElement (sfAccount,              SOE_REQUIRED)
        << SOElement (sfSigningAccounts,      SOE_REQUIRED)
        ;

    add (sfSigningAccount.getJsonName ().c_str (), sfSigningAccount.getCode ())
        << SOElement (sfAccount,              SOE_REQUIRED)
        << SOElement (sfSigningPubKey,        SOE_REQUIRED)
        << SOElement (sfMultiSignature,       SOE_REQUIRED)
        ;
}

void InnerObjectFormats::addCommonFields (Item& item)
{
}

InnerObjectFormats const&
InnerObjectFormats::getInstance ()
{
    static InnerObjectFormats instance;
    return instance;
}

SOTemplate const*
InnerObjectFormats::findSOTemplateBySField (SField const& sField) const
{
    SOTemplate const* ret = nullptr;
    auto itemPtr = findByType (sField.getCode ());
    if (itemPtr)
        ret = &(itemPtr->elements);

    return ret;
}

} // divvy
