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
#include <divvy/app/ledger/LedgerMaster.h>
#include <divvy/app/main/Application.h>
#include <divvy/app/main/LocalCredentials.h>
#include <divvy/app/misc/NetworkOPs.h>
#include <divvy/basics/SHA512Half.h>
#include <divvy/protocol/BuildInfo.h>
#include <divvy/overlay/impl/TMHello.h>
#include <beast/crypto/base64.h>
#include <beast/http/rfc2616.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/utility/static_initializer.h>
#include <boost/regex.hpp>
#include <algorithm>

// VFALCO Shouldn't we have to include the OpenSSL
// headers or something for SSL_get_finished?

namespace divvy {

/** Hashes the latest finished message from an SSL stream
    @param sslSession the session to get the message from.
    @param hash       the buffer into which the hash of the retrieved
                        message will be saved. The buffer MUST be at least
                        64 bytes long.
    @param getMessage a pointer to the function to call to retrieve the
                        finished message. This be either:
                        `SSL_get_finished` or
                        `SSL_get_peer_finished`.
    @return `true` if successful, `false` otherwise.
*/
static
bool
hashLastMessage (SSL const* ssl, unsigned char* hash,
    size_t (*get)(const SSL *, void *buf, size_t))
{
    enum
    {
        sslMinimumFinishedLength = 12
    };
    unsigned char buf[1024];
    std::memset(hash, 0, 64);
    size_t len = get (ssl, buf, sizeof (buf));
    if(len < sslMinimumFinishedLength)
        return false;
    SHA512 (buf, len, hash);
    return true;
}

std::pair<uint256, bool>
makeSharedValue (SSL* ssl, beast::Journal journal)
{
    std::pair<uint256, bool> result = { {}, false };

    unsigned char sha1[64];
    unsigned char sha2[64];

    if (!hashLastMessage(ssl, sha1, SSL_get_finished))
    {
        journal.error << "Cookie generation: local setup not complete";
        return result;
    }

    if (!hashLastMessage(ssl, sha2, SSL_get_peer_finished))
    {
        journal.error << "Cookie generation: peer setup not complete";
        return result;
    }

    // If both messages hash to the same value (i.e. match) something is
    // wrong. This would cause the resulting cookie to be 0.
    if (memcmp (sha1, sha2, sizeof (sha1)) == 0)
    {
        journal.error << "Cookie generation: identical finished messages";
        return result;
    }

    for (size_t i = 0; i < sizeof (sha1); ++i)
        sha1[i] ^= sha2[i];

    // Finally, derive the actual cookie for the values that
    // we have calculated.
    result.first = sha512Half(Slice(sha1, sizeof(sha1)));
    result.second = true;
    return result;
}

protocol::TMHello
buildHello (uint256 const& sharedValue, Application& app)
{
    protocol::TMHello h;

    Blob vchSig;
    app.getLocalCredentials ().getNodePrivate ().signNodePrivate (
        sharedValue, vchSig);

    h.set_protoversion (to_packed (BuildInfo::getCurrentProtocol()));
    h.set_protoversionmin (to_packed (BuildInfo::getMinimumProtocol()));
    h.set_fullversion (BuildInfo::getFullVersionString ());
    h.set_nettime (app.getOPs ().getNetworkTimeNC ());
    h.set_nodepublic (app.getLocalCredentials ().getNodePublic (
        ).humanNodePublic ());
    h.set_nodeproof (&vchSig[0], vchSig.size ());
    // h.set_ipv4port (portNumber); // ignored now
    h.set_testnet (false);

    // We always advertise ourselves as private in the HELLO message. This
    // suppresses the old peer advertising code and allows PeerFinder to
    // take over the functionality.
    h.set_nodeprivate (true);

    auto const closedLedger = app.getLedgerMaster().getClosedLedger();

    if (closedLedger && closedLedger->isClosed ())
    {
        uint256 hash = closedLedger->getHash ();
        h.set_ledgerclosed (hash.begin (), hash.size ());
        hash = closedLedger->getParentHash ();
        h.set_ledgerprevious (hash.begin (), hash.size ());
    }

    return h;
}

void
appendHello (beast::http::message& m,
    protocol::TMHello const& hello)
{
    auto& h = m.headers;

    //h.append ("Protocol-Versions",...

    h.append ("Public-Key", hello.nodepublic());

    h.append ("Session-Signature", beast::base64_encode (
        hello.nodeproof()));

    if (hello.has_nettime())
        h.append ("Network-Time", std::to_string (hello.nettime()));

    if (hello.has_ledgerindex())
        h.append ("Ledger", std::to_string (hello.ledgerindex()));

    if (hello.has_ledgerclosed())
        h.append ("Closed-Ledger", beast::base64_encode (
            hello.ledgerclosed()));

    if (hello.has_ledgerprevious())
        h.append ("Previous-Ledger", beast::base64_encode (
            hello.ledgerprevious()));
}

std::vector<ProtocolVersion>
parse_ProtocolVersions (std::string const& s)
{
    static beast::static_initializer<boost::regex> re (
        "^"                  // start of line
        "RTXP/"              // the string "RTXP/"
        "([1-9][0-9]*)"      // a number (non-zero and with no leading zeroes)
        "\\."                // a period
        "(0|[1-9][0-9]*)"    // a number (no leading zeroes unless exactly zero)
        "$"                  // The end of the string
        , boost::regex_constants::optimize);

    auto const list = beast::rfc2616::split_commas (s);
    std::vector<ProtocolVersion> result;
    for (auto const& s : list)
    {
        boost::smatch m;
        if (! boost::regex_match (s, m, *re))
            continue;
        int major;
        int minor;
        if (! beast::lexicalCastChecked (
                major, std::string (m[1])))
            continue;
        if (! beast::lexicalCastChecked (
                minor, std::string (m[2])))
            continue;
        result.push_back (std::make_pair (major, minor));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique (result.begin(), result.end()), result.end());
    return result;
}

std::pair<protocol::TMHello, bool>
parseHello (beast::http::message const& m, beast::Journal journal)
{
    auto const& h = m.headers;
    std::pair<protocol::TMHello, bool> result = { {}, false };
    protocol::TMHello& hello = result.first;

    // protocol version in TMHello is obsolete,
    // it is supplanted by the values in the headers.

    {
        // Required
        auto const iter = h.find ("Upgrade");
        if (iter == h.end())
            return result;
        auto const versions = parse_ProtocolVersions(iter->second);
        if (versions.empty())
            return result;
        hello.set_protoversion(
            (static_cast<std::uint32_t>(versions.back().first) << 16) |
            (static_cast<std::uint32_t>(versions.back().second)));
        hello.set_protoversionmin(
            (static_cast<std::uint32_t>(versions.front().first) << 16) |
            (static_cast<std::uint32_t>(versions.front().second)));
    }

    {
        // Required
        auto const iter = h.find ("Public-Key");
        if (iter == h.end())
            return result;
        DivvyAddress addr;
        addr.setNodePublic (iter->second);
        if (! addr.isValid())
            return result;
        hello.set_nodepublic (iter->second);
    }

    {
        // Required
        auto const iter = h.find ("Session-Signature");
        if (iter == h.end())
            return result;
        // TODO Security Review
        hello.set_nodeproof (beast::base64_decode (iter->second));
    }

    {
        auto const iter = h.find (m.request() ?
            "User-Agent" : "Server");
        if (iter != h.end())
            hello.set_fullversion (iter->second);
    }

    {
        auto const iter = h.find ("Network-Time");
        if (iter != h.end())
        {
            std::uint64_t nettime;
            if (! beast::lexicalCastChecked(nettime, iter->second))
                return result;
            hello.set_nettime (nettime);
        }
    }

    {
        auto const iter = h.find ("Ledger");
        if (iter != h.end())
        {
            LedgerIndex ledgerIndex;
            if (! beast::lexicalCastChecked(ledgerIndex, iter->second))
                return result;
            hello.set_ledgerindex (ledgerIndex);
        }
    }

    {
        auto const iter = h.find ("Closed-Ledger");
        if (iter != h.end())
            hello.set_ledgerclosed (beast::base64_decode (iter->second));
    }

    {
        auto const iter = h.find ("Previous-Ledger");
        if (iter != h.end())
            hello.set_ledgerprevious (beast::base64_decode (iter->second));
    }

    result.second = true;
    return result;
}

std::pair<DivvyAddress, bool>
verifyHello (protocol::TMHello const& h, uint256 const& sharedValue,
    beast::Journal journal, Application& app)
{
    std::pair<DivvyAddress, bool> result = { {}, false };
    std::uint32_t const ourTime = app.getOPs().getNetworkTimeNC();
    std::uint32_t const minTime = ourTime - clockToleranceDeltaSeconds;
    std::uint32_t const maxTime = ourTime + clockToleranceDeltaSeconds;

#ifdef BEAST_DEBUG
    if (h.has_nettime ())
    {
        std::int64_t to = ourTime;
        to -= h.nettime ();
        journal.debug <<
            "Connect: time offset " << to;
    }
#endif

    auto const protocol = BuildInfo::make_protocol(h.protoversion());

    if (h.has_nettime () &&
        ((h.nettime () < minTime) || (h.nettime () > maxTime)))
    {
        if (h.nettime () > maxTime)
        {
            journal.info <<
                "Clock for is off by +" << h.nettime() - ourTime;
        }
        else if (h.nettime () < minTime)
        {
            journal.info <<
                "Clock is off by -" << ourTime - h.nettime();
        }
    }
    else if (h.protoversionmin () > to_packed (
        BuildInfo::getCurrentProtocol()))
    {
        journal.info <<
            "Hello: Disconnect: Protocol mismatch [" <<
            "Peer expects " << to_string (protocol) <<
            " and we run " << to_string (BuildInfo::getCurrentProtocol()) << "]";
    }
    else if (! result.first.setNodePublic (h.nodepublic()))
    {
        journal.info <<
            "Hello: Disconnect: Bad node public key.";
    }
    else if (! result.first.verifyNodePublic (
        sharedValue, h.nodeproof (), ECDSA::not_strict))
    {
        // Unable to verify they have private key for claimed public key.
        journal.info <<
            "Hello: Disconnect: Failed to verify session.";
    }
    else
    {
        // Successful connection.
        result.second = true;
    }
    return result;
}

}
