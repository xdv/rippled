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
#include <divvy/basics/Log.h>
#include <divvy/basics/SHA512Half.h>
#include <divvy/protocol/Serializer.h>

namespace divvy {

int Serializer::addZeros (size_t uBytes)
{
    int ret = mData.size ();

    while (uBytes--)
        mData.push_back (0);

    return ret;
}

int Serializer::add16 (std::uint16_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 8));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

int Serializer::add32 (std::uint32_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 24));
    mData.push_back (static_cast<unsigned char> ((i >> 16) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 8) & 0xff));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

int Serializer::add64 (std::uint64_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 56));
    mData.push_back (static_cast<unsigned char> ((i >> 48) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 40) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 32) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 24) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 16) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 8) & 0xff));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

template <> int Serializer::addInteger(unsigned char i) { return add8(i); }
template <> int Serializer::addInteger(std::uint16_t i) { return add16(i); }
template <> int Serializer::addInteger(std::uint32_t i) { return add32(i); }
template <> int Serializer::addInteger(std::uint64_t i) { return add64(i); }

int Serializer::add128 (const uint128& i)
{
    int ret = mData.size ();
    mData.insert (mData.end (), i.begin (), i.end ());
    return ret;
}

int Serializer::add256 (uint256 const& i)
{
    int ret = mData.size ();
    mData.insert (mData.end (), i.begin (), i.end ());
    return ret;
}

int Serializer::addRaw (Blob const& vector)
{
    int ret = mData.size ();
    mData.insert (mData.end (), vector.begin (), vector.end ());
    return ret;
}

int Serializer::addRaw (const Serializer& s)
{
    int ret = mData.size ();
    mData.insert (mData.end (), s.begin (), s.end ());
    return ret;
}

int Serializer::addRaw (const void* ptr, int len)
{
    int ret = mData.size ();
    mData.insert (mData.end (), (const char*) ptr, ((const char*)ptr) + len);
    return ret;
}

bool Serializer::get256 (uint256& o, int offset) const
{
    if ((offset + (256 / 8)) > mData.size ()) return false;

    memcpy (o.begin (), & (mData.front ()) + offset, (256 / 8));
    return true;
}

int Serializer::addFieldID (int type, int name)
{
    int ret = mData.size ();
    assert ((type > 0) && (type < 256) && (name > 0) && (name < 256));

    if (type < 16)
    {
        if (name < 16) // common type, common name
            mData.push_back (static_cast<unsigned char> ((type << 4) | name));
        else
        {
            // common type, uncommon name
            mData.push_back (static_cast<unsigned char> (type << 4));
            mData.push_back (static_cast<unsigned char> (name));
        }
    }
    else if (name < 16)
    {
        // uncommon type, common name
        mData.push_back (static_cast<unsigned char> (name));
        mData.push_back (static_cast<unsigned char> (type));
    }
    else
    {
        // uncommon type, uncommon name
        mData.push_back (static_cast<unsigned char> (0));
        mData.push_back (static_cast<unsigned char> (type));
        mData.push_back (static_cast<unsigned char> (name));
    }

    return ret;
}

bool Serializer::getFieldID (int& type, int& name, int offset) const
{
    if (!get8 (type, offset))
    {
        WriteLog (lsWARNING, Serializer) << "gFID: unable to get type";
        return false;
    }

    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        if (!get8 (type, ++offset))
            return false;

        if ((type == 0) || (type < 16))
        {
            WriteLog (lsWARNING, Serializer) << "gFID: uncommon type out of range " << type;
            return false;
        }
    }

    if (name == 0)
    {
        // uncommon name
        if (!get8 (name, ++offset))
            return false;

        if ((name == 0) || (name < 16))
        {
            WriteLog (lsWARNING, Serializer) << "gFID: uncommon name out of range " << name;
            return false;
        }
    }

    return true;
}

int Serializer::add8 (unsigned char byte)
{
    int ret = mData.size ();
    mData.push_back (byte);
    return ret;
}

bool Serializer::get8 (int& byte, int offset) const
{
    if (offset >= mData.size ()) return false;

    byte = mData[offset];
    return true;
}

bool Serializer::chop (int bytes)
{
    if (bytes > mData.size ()) return false;

    mData.resize (mData.size () - bytes);
    return true;
}

bool Serializer::getRaw (Blob& o, int offset, int length) const
{
    if ((offset + length) > mData.size ()) return false;

    o.assign (mData.begin () + offset, mData.begin () + offset + length);
    return true;
}

Blob Serializer::getRaw (int offset, int length) const
{
    Blob o;

    if ((offset + length) > mData.size ()) return o;

    o.assign (mData.begin () + offset, mData.begin () + offset + length);
    return o;
}

uint256 Serializer::getSHA512Half () const
{
    return sha512Half(make_Slice(mData));
}

int Serializer::addVL (Blob const& vector)
{
    int ret = addEncoded (vector.size ());
    addRaw (vector);
    assert (mData.size () == (ret + vector.size () + encodeLengthLength (vector.size ())));
    return ret;
}

int Serializer::addVL (const void* ptr, int len)
{
    int ret = addEncoded (len);

    if (len)
        addRaw (ptr, len);

    return ret;
}

bool Serializer::getVL (Blob& objectVL, int offset, int& length) const
{
    int b1;

    if (!get8 (b1, offset++)) return false;

    int datLen, lenLen = decodeLengthLength (b1);

    try
    {
        if (lenLen == 1)
            datLen = decodeVLLength (b1);
        else if (lenLen == 2)
        {
            int b2;

            if (!get8 (b2, offset++)) return false;

            datLen = decodeVLLength (b1, b2);
        }
        else if (lenLen == 3)
        {
            int b2, b3;

            if (!get8 (b2, offset++)) return false;

            if (!get8 (b3, offset++)) return false;

            datLen = decodeVLLength (b1, b2, b3);
        }
        else return false;
    }
    catch (...)
    {
        return false;
    }

    length = lenLen + datLen;
    return getRaw (objectVL, offset, datLen);
}

bool Serializer::getVLLength (int& length, int offset) const
{
    int b1;

    if (!get8 (b1, offset++)) return false;

    int lenLen = decodeLengthLength (b1);

    try
    {
        if (lenLen == 1)
            length = decodeVLLength (b1);
        else if (lenLen == 2)
        {
            int b2;

            if (!get8 (b2, offset++)) return false;

            length = decodeVLLength (b1, b2);
        }
        else if (lenLen == 3)
        {
            int b2, b3;

            if (!get8 (b2, offset++)) return false;

            if (!get8 (b3, offset++)) return false;

            length = decodeVLLength (b1, b2, b3);
        }
        else return false;
    }
    catch (...)
    {
        return false;
    }

    return true;
}

int Serializer::addEncoded (int length)
{
    std::array<std::uint8_t, 4> bytes;
    int numBytes = 0;

    if (length <= 192)
    {
        bytes[0] = static_cast<unsigned char> (length);
        numBytes = 1;
    }
    else if (length <= 12480)
    {
        length -= 193;
        bytes[0] = 193 + static_cast<unsigned char> (length >> 8);
        bytes[1] = static_cast<unsigned char> (length & 0xff);
        numBytes = 2;
    }
    else if (length <= 918744)
    {
        length -= 12481;
        bytes[0] = 241 + static_cast<unsigned char> (length >> 16);
        bytes[1] = static_cast<unsigned char> ((length >> 8) & 0xff);
        bytes[2] = static_cast<unsigned char> (length & 0xff);
        numBytes = 3;
    }
    else throw std::overflow_error ("lenlen");

    return addRaw (&bytes[0], numBytes);
}

int Serializer::encodeLengthLength (int length)
{
    if (length < 0) throw std::overflow_error ("len<0");

    if (length <= 192) return 1;

    if (length <= 12480) return 2;

    if (length <= 918744) return 3;

    throw std::overflow_error ("len>918744");
}

int Serializer::decodeLengthLength (int b1)
{
    if (b1 < 0) throw std::overflow_error ("b1<0");

    if (b1 <= 192) return 1;

    if (b1 <= 240) return 2;

    if (b1 <= 254) return 3;

    throw std::overflow_error ("b1>254");
}

int Serializer::decodeVLLength (int b1)
{
    if (b1 < 0) throw std::overflow_error ("b1<0");

    if (b1 > 254) throw std::overflow_error ("b1>254");

    return b1;
}

int Serializer::decodeVLLength (int b1, int b2)
{
    if (b1 < 193) throw std::overflow_error ("b1<193");

    if (b1 > 240) throw std::overflow_error ("b1>240");

    return 193 + (b1 - 193) * 256 + b2;
}

int Serializer::decodeVLLength (int b1, int b2, int b3)
{
    if (b1 < 241) throw std::overflow_error ("b1<241");

    if (b1 > 254) throw std::overflow_error ("b1>254");

    return 12481 + (b1 - 241) * 65536 + b2 * 256 + b3;
}

//------------------------------------------------------------------------------

SerialIter::SerialIter (void const* data,
        std::size_t size) noexcept
    : p_ (reinterpret_cast<
        std::uint8_t const*>(data))
    , remain_ (size)
{
}

void
SerialIter::reset() noexcept
{
    p_ -= used_;
    remain_ += used_;
    used_ = 0;
}

unsigned char
SerialIter::get8()
{
    if (remain_ < 1)
        throw std::runtime_error(
            "invalid SerialIter get8");
    unsigned char t = *p_;
    ++p_;
    ++used_;
    --remain_;
    return t;
}

std::uint16_t
SerialIter::get16()
{
    if (remain_ < 2)
        throw std::runtime_error(
            "invalid SerialIter get16");
    auto t = p_;
    p_ += 2;
    used_ += 2;
    remain_ -= 2;
    return
        (std::uint64_t(t[0]) <<  8) +
         std::uint64_t(t[1]  );
}

std::uint32_t
SerialIter::get32()
{
    if (remain_ < 4)
        throw std::runtime_error(
            "invalid SerialIter get32");
    auto t = p_;
    p_ += 4;
    used_ += 4;
    remain_ -= 4;
    return
        (std::uint64_t(t[0]) << 24) +
        (std::uint64_t(t[1]) << 16) +
        (std::uint64_t(t[2]) <<  8) +
         std::uint64_t(t[3] );
}

std::uint64_t
SerialIter::get64 ()
{
    if (remain_ < 8)
        throw std::runtime_error(
            "invalid SerialIter get64");
    auto t = p_;
    p_ += 8;
    used_ += 8;
    remain_ -= 8;
    return
        (std::uint64_t(t[0]) << 56) +
        (std::uint64_t(t[1]) << 48) +
        (std::uint64_t(t[2]) << 40) +
        (std::uint64_t(t[3]) << 32) +
        (std::uint64_t(t[4]) << 24) +
        (std::uint64_t(t[5]) << 16) +
        (std::uint64_t(t[6]) <<  8) +
         std::uint64_t(t[7] );
}

void
SerialIter::getFieldID (int& type, int& name)
{
    type = get8();
    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        type = get8();
        if (type == 0 || type < 16)
            throw std::runtime_error(
                "gFID: uncommon type out of range " +
                    std::to_string(type));
    }

    if (name == 0)
    {
        // uncommon name
        name = get8();
        if (name == 0 || name < 16)
            throw std::runtime_error(
                "gFID: uncommon name out of range " +
                    std::to_string(name));
    }
}

// getRaw for blob or buffer
template<class T>
T SerialIter::getRawHelper (int size)
{
    static_assert(std::is_same<T, Blob>::value ||
                  std::is_same<T, Buffer>::value, "");
    if (remain_ < size)
        throw std::runtime_error(
            "invalid SerialIter getRaw");
    T result (size);
    memcpy(result.data (), p_, size);
    p_ += size;
    used_ += size;
    remain_ -= size;
    return result;
}

// VFALCO DEPRECATED Returns a copy
Blob
SerialIter::getRaw (int size)
{
    return getRawHelper<Blob> (size);
}

int SerialIter::getVLDataLength ()
{
    int b1 = get8();
    int datLen;
    int lenLen = Serializer::decodeLengthLength(b1);
    if (lenLen == 1)
    {
        datLen = Serializer::decodeVLLength (b1);
    }
    else if (lenLen == 2)
    {
        int b2 = get8();
        datLen = Serializer::decodeVLLength (b1, b2);
    }
    else
    {
        assert(lenLen == 3);
        int b2 = get8();
        int b3 = get8();
        datLen = Serializer::decodeVLLength (b1, b2, b3);
    }
    return datLen;
}

// VFALCO DEPRECATED Returns a copy
Blob
SerialIter::getVL()
{
    return getRaw(getVLDataLength ());
}

Buffer
SerialIter::getVLBuffer()
{
    return getRawHelper<Buffer> (getVLDataLength ());
}

} // divvy
