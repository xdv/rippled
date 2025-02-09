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

#ifndef RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED
#define RIPPLE_PROTOCOL_STINTEGER_H_INCLUDED

#include <divvy/protocol/STBase.h>

namespace divvy {

template <typename Integer>
class STInteger
    : public STBase
{
public:
    using value_type = Integer;

    explicit
    STInteger (Integer v) 
        : value_ (v)
    { }

    STInteger (SField const& n, Integer v = 0)
        : STBase (n), value_ (v)
    { }

    STInteger(SerialIter& sit, SField const& name);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    SerializedTypeID
    getSType () const override;

    Json::Value
    getJson (int) const override;

    std::string
    getText () const override;

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert (fName->fieldType == getSType ());
        s.addInteger (value_);
    }

    Integer
    getValue () const
    {
        return value_;
    }

    void
    setValue (Integer v)
    {
        value_ = v;
    }

    operator
    Integer () const
    {
        return value_;
    }

    virtual
    bool isDefault () const override
    {
        return value_ == 0;
    }

    bool
    isEquivalent (const STBase& t) const override
    {
        const STInteger* v = dynamic_cast<const STInteger*> (&t);
        return v && (value_ == v->value_);
    }

private:
    Integer value_;
};

using STUInt8  = STInteger<unsigned char>;
using STUInt16 = STInteger<std::uint16_t>;
using STUInt32 = STInteger<std::uint32_t>;
using STUInt64 = STInteger<std::uint64_t>;

} // divvy

#endif
