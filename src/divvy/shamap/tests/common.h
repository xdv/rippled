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

#ifndef RIPPLE_SHAMAP_TESTS_COMMON_H_INCLUDED
#define RIPPLE_SHAMAP_TESTS_COMMON_H_INCLUDED

#include <BeastConfig.h>
#include <divvy/shamap/Family.h>
#include <divvy/shamap/FullBelowCache.h>
#include <divvy/shamap/TreeNodeCache.h>
#include <divvy/shamap/SHAMap.h>
#include <divvy/basics/StringUtilities.h>
#include <divvy/nodestore/DummyScheduler.h>
#include <divvy/nodestore/Manager.h>
#include <beast/utility/Journal.h>
#include <beast/chrono/manual_clock.h>

namespace divvy {
namespace shamap {
namespace tests {

class TestFamily : public shamap::Family
{
private:
    beast::manual_clock <
        std::chrono::steady_clock> clock_;
    NodeStore::DummyScheduler scheduler_;
    TreeNodeCache treecache_;
    FullBelowCache fullbelow_;
    std::unique_ptr<NodeStore::Database> db_;

public:
    explicit
    TestFamily (beast::Journal j)
        : treecache_ ("TreeNodeCache", 65536, 60, clock_, j)
        , fullbelow_ ("full_below", clock_)
    {
        Section testSection;
        testSection.set("type", "memory");
        testSection.set("Path", "SHAMap_test");
        db_ = NodeStore::Manager::instance ().make_Database (
            "test", scheduler_, j, 1, testSection);
    }

    beast::manual_clock <std::chrono::steady_clock>
    clock()
    {
        return clock_;
    }

    FullBelowCache&
    fullbelow() override
    {
        return fullbelow_;
    }

    FullBelowCache const&
    fullbelow() const override
    {
        return fullbelow_;
    }

    TreeNodeCache&
    treecache() override
    {
        return treecache_;
    }

    TreeNodeCache const&
    treecache() const override
    {
        return treecache_;
    }

    NodeStore::Database&
    db() override
    {
        return *db_;
    }

    NodeStore::Database const&
    db() const override
    {
        return *db_;
    }

    void
    missing_node (std::uint32_t refNum) override
    {
        throw std::runtime_error("missing node");
    }
};

} // tests
} // shamap
} // divvy

#endif
