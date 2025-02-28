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

#ifndef RIPPLE_TEST_JTX_ENV_H_INCLUDED
#define RIPPLE_TEST_JTX_ENV_H_INCLUDED

#include <divvy/test/jtx/Account.h>
#include <divvy/test/jtx/amount.h>
#include <divvy/test/jtx/JTx.h>
#include <divvy/test/jtx/require.h>
#include <divvy/test/jtx/tags.h>

#include <divvy/app/ledger/Ledger.h>
#include <divvy/json/json_value.h>
#include <divvy/json/to_string.h>
#include <divvy/protocol/Indexes.h>
#include <divvy/protocol/Issue.h>
#include <divvy/protocol/DivvyAddress.h>
#include <divvy/protocol/STAmount.h>
#include <divvy/protocol/STObject.h>
#include <divvy/protocol/STTx.h>
#include <beast/is_call_possible.h>
#include <beast/unit_test/suite.h>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <beast/cxx14/utility.h> // <utility>
#include <functional>
#include <tuple>
#include <utility>
#include <unordered_map>

namespace divvy {
namespace test {
namespace jtx {

namespace detail {

#ifdef _MSC_VER

// Workaround for C2797:
// list initialization inside member initializer
// list or non-static data member initializer is
// not implemented
//
template <std::size_t N>
struct nodivvy_helper
{
    std::array<Account, N> args;

    template <class T, class ...Args>
    std::array<Account, sizeof...(Args)>
    flatten (Args&& ...args)
    {
        return std::array<T,
            sizeof...(Args)> {
                std::forward<Args>(args)...};
    }

    template <class... Args>
    explicit
    nodivvy_helper(Args const&... args_)
        : args(flatten<Account>(args_...))
    {
    }
};

#else

template <std::size_t N>
struct nodivvy_helper
{
    std::array<Account, N> args;

    template <class... Args>
    explicit
    nodivvy_helper(Args const&... args_)
        : args { { args_... } }
    {
    }
};

#endif

} // detail

/** Designate accounts as no-divvy in Env::fund */
template <class... Args>
detail::nodivvy_helper<1 + sizeof...(Args)>
nodivvy (Account const& account,
    Args const&... args)
{
    return detail::nodivvy_helper<
        1 + sizeof...(Args)>(
            account, args...);
}

//------------------------------------------------------------------------------

/** A transaction testing environment. */
class Env
{
public:
    beast::unit_test::suite& test;

    /** The master account. */
    Account const master;

    /** The open ledger. */
    std::shared_ptr<Ledger> ledger;

public:
    Env (beast::unit_test::suite& test_);
    
    /** Turn on JSON tracing.
        With no arguments, trace all
    */
    void
    trace(int howMany = -1)
    {
        trace_ = howMany;
    }

    /** Turn off JSON tracing. */
    void
    notrace()
    {
        trace_ = 0;
    }

    /** Associate AccountID with account. */
    void
    memoize (Account const& account);

    /** Returns the Account given the AccountID. */
    /** @{ */
    Account const&
    lookup (AccountID const& id) const;

    Account const&
    lookup (std::string const& base58ID) const;
    /** @} */

    /** Returns the XDV balance on an account.
        Returns 0 if the account does not exist.
    */
    PrettyAmount
    balance (Account const& account) const;

    /** Returns the next sequence number on account.
        Exceptions:
            Throws if the account does not exist
    */
    std::uint32_t
    seq (Account const& account) const;

    /** Return the balance on an account.
        Returns 0 if the trust line does not exist.
    */
    // VFALCO NOTE This should return a unit-less amount
    PrettyAmount
    balance (Account const& account,
        Issue const& issue) const;

    /** Return an account root.
        @return empty if the account does not exist.
    */
    std::shared_ptr<SLE const>
    le (Account const& account) const;

    /** Return a ledger entry.
        @return empty if the ledger entry does not exist
    */
    // VFALCO NOTE Use Keylet here
    std::shared_ptr<SLE const>
    le (uint256 const& key) const;

    /** Create a JTx from parameters. */
    template <class JsonValue,
        class... FN>
    JTx
    jt (JsonValue&& jv, FN const&... fN)
    {
        JTx jt(std::forward<JsonValue>(jv));
        invoke(jt, fN...);
        autofill(jt);
        return jt;
    }

    /** Create JSON from parameters.
        This will apply funclets and autofill.
    */
    template <class JsonValue,
        class... FN>
    Json::Value
    json (JsonValue&&jv, FN const&... fN)
    {
        auto tj = jt(
            std::forward<JsonValue>(jv),
                fN...);
        return std::move(tj.jv);
    }

    /** Check a set of requirements.
        
        The requirements are formed
        from condition functors.
    */
    template <class... Args>
    void
    require (Args const&... args) const
    {
        jtx::required(args...)(*this);
    }

    /** Submit an existing JTx.
        This calls postconditions.
    */
    virtual
    void
    submit (JTx const& jt);

    /** Apply funclets and submit. */
    /** @{ */
    template <class JsonValue, class... FN>
    void
    apply (JsonValue&& jv, FN const&... fN)
    {
        submit(jt(std::forward<
            JsonValue>(jv), fN...));
    }

    template <class JsonValue,
        class... FN>
    void
    operator()(JsonValue&& jv,
        FN const&... fN)
    {
        apply(std::forward<
            JsonValue>(jv), fN...);
    }
    /** @} */

private:
    void
    fund (bool setDefaultDivvy,
        STAmount const& amount,
            Account const& account);

    // If you get an error here it means
    // you're calling fund with no accounts
    inline
    void
    fund (STAmount const&)
    {
    }

    void
    fund_arg (STAmount const& amount,
        Account const& account)
    {
        fund (true, amount, account);
    }

    template <std::size_t N>
    void
    fund_arg (STAmount const& amount,
        detail::nodivvy_helper<N> const& list)
    {
        for (auto const& account : list.args)
            fund (false, amount, account);
    }
public:

    /** Create a new account with some XDV.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings. The XDV is transferred from the master
        account.

        Preconditions:
            The account must not already exist

        Effects:
            The asfDefaultDivvy on the account is set,
            and the sequence number is incremented, unless
            the account is wrapped with a call to nodivvy.

            The account's XDV balance is set to amount.

            Generates a test that the balance is set.

        @param amount The amount of XDV to transfer to
                      each account.

        @param args A heterogeneous list of accounts to fund
                    or calls to nodivvy with lists of accounts
                    to fund.
    */
    template<class Arg, class... Args>
    void
    fund (STAmount const& amount,
        Arg const& arg, Args const&... args)
    {
        fund_arg (amount, arg);
        fund (amount, args...);
    }

    /** Establish trust lines.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings.

        Preconditions:
            The account must already exist

        Effects:
            A trust line is added for the account.
            The account's sequence number is incremented.
            The account is refunded for the transaction fee
                to set the trust line.

        The refund comes from the master account.
    */
    /** @{ */
    void
    trust (STAmount const& amount,
        Account const& account);

    template<class... Accounts>
    void
    trust (STAmount const& amount, Account const& to0,
        Account const& to1, Accounts const&... toN)
    {
        trust(amount, to0);
        trust(amount, to1, toN...);
    }
    /** @} */

protected:
    int trace_ = 0;

    void
    autofill_sig (JTx& jt);

    virtual
    void
    autofill (JTx& jt);

    /** Create a STTx from a JTx
        The framework requires that JSON is valid.
        On a parse error, the JSON is logged and
        an exception thrown.
        Throws:
            parse_error
    */
    // VFALCO NOTE This should be <STTx const>
    std::shared_ptr<STTx>
    st (JTx const& jt);

    inline
    void
    invoke (STTx& stx)
    {
    }

    template <class F>
    inline
    void
    maybe_invoke (STTx& stx, F const& f,
        std::false_type)
    {
    }

    template <class F>
    void
    maybe_invoke (STTx& stx, F const& f,
        std::true_type)
    {
        f(*this, stx);
    }

    // Invoke funclets on stx
    // Note: The STTx may not be modified
    template <class F, class... FN>
    void
    invoke (STTx& stx, F const& f,
        FN const&... fN)
    {
        maybe_invoke(stx, f,
            beast::is_call_possible<F,
                void(Env&, STTx const&)>());
        invoke(stx, fN...);
    }

    inline
    void
    invoke (JTx&)
    {
    }

    template <class F>
    inline
    void
    maybe_invoke (JTx& jt, F const& f,
        std::false_type)
    {
    }

    template <class F>
    void
    maybe_invoke (JTx& jt, F const& f,
        std::true_type)
    {
        f(*this, jt);
    }

    // Invoke funclets on jt
    template <class F, class... FN>
    void
    invoke (JTx& jt, F const& f,
        FN const&... fN)
    {
        maybe_invoke(jt, f,
            beast::is_call_possible<F,
                void(Env&, JTx&)>());
        invoke(jt, fN...);
    }

    // Map of account IDs to Account
    std::unordered_map<
        AccountID, Account> map_;
};

} // jtx
} // test
} // divvy

#endif
