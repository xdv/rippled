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

#ifndef RIPPLE_BASICS_UPTIMETIMER_H_INCLUDED
#define RIPPLE_BASICS_UPTIMETIMER_H_INCLUDED

#include <ctime>

namespace divvy {

/** Tracks program uptime.

    The timer can be switched to a manual system of updating, to reduce
    system calls. (?)
*/
// VFALCO TODO determine if the non-manual timing is actually needed
class UptimeTimer
{
private:
    UptimeTimer ();
    ~UptimeTimer ();

public:
    int getElapsedSeconds () const;

    void beginManualUpdates ();
    void endManualUpdates ();

    void incrementElapsedTime ();

    static UptimeTimer& getInstance ();

private:
    // VFALCO DEPRECATED, Use a memory barrier instead of forcing a cache line
    int volatile m_elapsedTime;

    time_t m_startTime;

    bool m_isUpdatingManually;
};

} // divvy

#endif
