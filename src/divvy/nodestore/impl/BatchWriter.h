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

#ifndef RIPPLE_NODESTORE_BATCHWRITER_H_INCLUDED
#define RIPPLE_NODESTORE_BATCHWRITER_H_INCLUDED

#include <divvy/nodestore/Scheduler.h>
#include <divvy/nodestore/Task.h>
#include <divvy/nodestore/Types.h>
#include <condition_variable>
#include <mutex>

namespace divvy {
namespace NodeStore {

/** Batch-writing assist logic.

    The batch writes are performed with a scheduled task. Use of the
    class it not required. A backend can implement its own write batching,
    or skip write batching if doing so yields a performance benefit.

    @see Scheduler
*/
class BatchWriter : private Task
{
public:
    /** This callback does the actual writing. */
    struct Callback
    {
        virtual void writeBatch (Batch const& batch) = 0;
    };

    /** Create a batch writer. */
    BatchWriter (Callback& callback, Scheduler& scheduler);

    /** Destroy a batch writer.

        Anything pending in the batch is written out before this returns.
    */
    ~BatchWriter ();

    /** Store the object.

        This will add to the batch and initiate a scheduled task to
        write the batch out.
    */
    void store (std::shared_ptr<NodeObject> const& object);

    /** Get an estimate of the amount of writing I/O pending. */
    int getWriteLoad ();

private:
    void performScheduledTask ();
    void writeBatch ();
    void waitForWriting ();

private:
    using LockType = std::recursive_mutex;
    using CondvarType = std::condition_variable_any;

    Callback& m_callback;
    Scheduler& m_scheduler;
    LockType mWriteMutex;
    CondvarType mWriteCondition;
    int mWriteLoad;
    bool mWritePending;
    Batch mWriteSet;
};

}
}

#endif
