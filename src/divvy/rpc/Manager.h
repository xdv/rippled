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

#ifndef RIPPLE_RPC_MANAGER_H_INCLUDED
#define RIPPLE_RPC_MANAGER_H_INCLUDED

#include <divvy/rpc/Request.h>
#include <memory>

namespace divvy {
namespace RPC {

/** Processes RPC commands. */
class Manager
{
public:
    using handler_type = std::function <void (Request&)>;

    virtual ~Manager () = 0;

    /** Add a handler for the specified JSON-RPC command. */
    template <class Handler>
    void add (std::string const& method)
    {
        add (method, handler_type (
        [](Request& req)
        {
            Handler h;
            h (req);
        }));
    }

    virtual void add (std::string const& method, handler_type&& handler) = 0;

    /** Dispatch the JSON-RPC request.
        @return `true` if the command was found.
    */
    virtual bool dispatch (Request& req) = 0;
};

std::unique_ptr <Manager> make_Manager (beast::Journal journal);

} // RPC
} // divvy

#endif
