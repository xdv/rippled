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

#ifndef RIPPLE_OVERLAY_TMHELLO_H_INCLUDED
#define RIPPLE_OVERLAY_TMHELLO_H_INCLUDED

#include <divvy/app/main/Application.h>
#include <divvy/protocol/BuildInfo.h>
#include <divvy/protocol/DivvyAddress.h>
#include <divvy/protocol/UintTypes.h>
#include <beast/http/message.h>
#include <beast/utility/Journal.h>
#include <utility>

#include <boost/asio/ssl.hpp>

#include "divvy.pb.h"

namespace divvy {

enum
{
    // The clock drift we allow a remote peer to have
    clockToleranceDeltaSeconds = 20
};

/** Computes a shared value based on the SSL connection state.
    When there is no man in the middle, both sides will compute the same
    value. In the presence of an attacker, the computed values will be
    different.
    If the shared value generation fails, the link MUST be dropped.
    @return A pair. Second will be false if shared value generation failed.
*/
std::pair<uint256, bool>
makeSharedValue (SSL* ssl, beast::Journal journal);

/** Build a TMHello protocol message. */
protocol::TMHello
buildHello (uint256 const& sharedValue, Application& app);

/** Insert HTTP headers based on the TMHello protocol message. */
void
appendHello (beast::http::message& m, protocol::TMHello const& hello);

/** Parse HTTP headers into TMHello protocol message.
    @return A pair. Second will be false if the parsing failed.
*/
std::pair<protocol::TMHello, bool>
parseHello (beast::http::message const& m, beast::Journal journal);

/** Validate and store the public key in the TMHello.
    This includes signature verification on the shared value.
    @return A pair. Second will be false if the check failed.
*/
std::pair<DivvyAddress, bool>
verifyHello (protocol::TMHello const& h, uint256 const& sharedValue,
    beast::Journal journal, Application& app);

/** Parse a set of protocol versions.
    The returned list contains no duplicates and is sorted ascending.
    Any strings that are not parseable as RTXP protocol strings are
    excluded from the result set.
*/
std::vector<ProtocolVersion>
parse_ProtocolVersions (std::string const& s);

}

#endif
