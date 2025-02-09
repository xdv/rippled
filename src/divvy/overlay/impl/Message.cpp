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
#include <divvy/overlay/Message.h>
#include <cstdint>

namespace divvy {

Message::Message (::google::protobuf::Message const& message, int type)
{
    unsigned const messageBytes = message.ByteSize ();

    assert (messageBytes != 0);

    mBuffer.resize (kHeaderBytes + messageBytes);

    encodeHeader (messageBytes, type);

    if (messageBytes != 0)
    {
        message.SerializeToArray (&mBuffer [Message::kHeaderBytes], messageBytes);
    }
}

bool Message::operator== (Message const& other) const
{
    return mBuffer == other.mBuffer;
}

unsigned Message::getLength (std::vector <uint8_t> const& buf)
{
    unsigned result;

    if (buf.size () >= Message::kHeaderBytes)
    {
        result = buf [0];
        result <<= 8;
        result |= buf [1];
        result <<= 8;
        result |= buf [2];
        result <<= 8;
        result |= buf [3];
    }
    else
    {
        result = 0;
    }

    return result;
}

int Message::getType (std::vector<uint8_t> const& buf)
{
    if (buf.size () < Message::kHeaderBytes)
        return 0;

    int ret = buf[4];
    ret <<= 8;
    ret |= buf[5];
    return ret;
}

void Message::encodeHeader (unsigned size, int type)
{
    assert (mBuffer.size () >= Message::kHeaderBytes);
    mBuffer[0] = static_cast<std::uint8_t> ((size >> 24) & 0xFF);
    mBuffer[1] = static_cast<std::uint8_t> ((size >> 16) & 0xFF);
    mBuffer[2] = static_cast<std::uint8_t> ((size >> 8) & 0xFF);
    mBuffer[3] = static_cast<std::uint8_t> (size & 0xFF);
    mBuffer[4] = static_cast<std::uint8_t> ((type >> 8) & 0xFF);
    mBuffer[5] = static_cast<std::uint8_t> (type & 0xFF);
}

}
