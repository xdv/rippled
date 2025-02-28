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

#ifndef RIPPLE_TEST_JTX_JTX_H_INCLUDED
#define RIPPLE_TEST_JTX_JTX_H_INCLUDED

#include <divvy/test/jtx/requires.h>
#include <divvy/test/jtx/basic_prop.h>
#include <divvy/json/json_value.h>
#include <divvy/protocol/TER.h>
#include <functional>
#include <memory>
#include <vector>

namespace divvy {
namespace test {
namespace jtx {

class Env;

/** Execution context for applying a JSON transaction.
    This augments the transaction with various settings.
*/
struct JTx
{
    Json::Value jv;
    bool fill_fee = true;
    bool fill_seq = true;
    bool fill_sig = true;
    std::function<void(Env&, JTx&)> signer;
    requires_t requires;
    TER ter = tesSUCCESS;

    JTx() = default;

#if defined(_MSC_VER) && _MSC_VER <= 1800
    JTx(JTx&& src)
        : jv(std::move(src.jv))
        , fill_fee(std::move(src.fill_fee))
        , fill_seq(std::move(src.fill_seq))
        , fill_sig(std::move(src.fill_sig))
        , signer(std::move(src.signer))
        , requires(std::move(src.requires))
        , ter(std::move(src.ter))
        , props_(std::move(src.props_))
    {
    }

    JTx& operator=(JTx&& src) noexcept
    {
        jv = std::move(src.jv);
        fill_fee = std::move(src.fill_fee);
        fill_seq = std::move(src.fill_seq);
        fill_sig = std::move(src.fill_sig);
        signer = std::move(src.signer);
        requires = std::move(src.requires);
        ter = std::move(src.ter);
        props_ = std::move(src.props_);

        return *this;
    }
#endif

    JTx (Json::Value&& jv_)
        : jv(std::move(jv_))
    {
    }

    JTx (Json::Value const& jv_)
        : jv(jv_)
    {
    }

    template <class Key>
    Json::Value&
    operator[](Key const& key)
    {
        return jv[key];
    }

public:
    /** Return a property if it exists

        @return nullptr if the Prop does not exist
    */
    /** @{ */
    template <class Prop>
    Prop*
    get()
    {
        for (auto& prop : props_.list)
        {
            if (auto test = dynamic_cast<
                    prop_type<Prop>*>(
                        prop.get()))
                return &test->t;
        }
        return nullptr;
    }

    template <class Prop>
    Prop const*
    get() const
    {
        for (auto& prop : props_.list)
        {
            if (auto test = dynamic_cast<
                    prop_type<Prop> const*>(
                        prop.get()))
                return &test->t;
        }
        return nullptr;
    }
    /** @} */

    /** Set a property
        If the property already exists,
        it is replaced.
    */
    /** @{ */
    void
    set(std::unique_ptr<basic_prop> p)
    {
        for (auto& prop : props_.list)
        {
            if (prop->assignable(p.get()))
            {
                prop = std::move(p);
                return;
            }
        }
        props_.list.emplace_back(std::move(p));
    }

    template <class Prop, class... Args>
    void
    set(Args&&... args)
    {
        set(std::make_unique<
            prop_type<Prop>>(
                std::forward <Args> (
                    args)...));
    }
    /** @} */

private:
    struct prop_list
    {
        prop_list() = default;

        prop_list(prop_list const& src)
        {
            for (auto const& prop : src.list)
                list.emplace_back(prop->clone());
        }

        prop_list& operator=(prop_list const& src)
        {
            if (this != &src)
            {
                list.clear();
                for (auto const& prop : src.list)
                    list.emplace_back(prop->clone());
            }
            return *this;
        }

#if defined(_MSC_VER) && _MSC_VER <= 1800
        prop_list(prop_list&& src)
            : list(std::move(src.list))
        {
        }

        prop_list& operator=(prop_list&& src)
        {
            list = std::move(src.list);
            return *this;
        }
#else
        prop_list(prop_list&& src) = default;
        prop_list& operator=(prop_list&& src) = default;
#endif

        std::vector<std::unique_ptr<
            basic_prop>> list;
    };

    prop_list props_;
};

} // jtx
} // test
} // divvy

#endif
