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
#include <divvy/app/main/NodeStoreScheduler.h>

namespace divvy {

NodeStoreScheduler::NodeStoreScheduler (Stoppable& parent)
    : Stoppable ("NodeStoreScheduler", parent)
    , m_jobQueue (nullptr)
    , m_taskCount (0)
{
}

void NodeStoreScheduler::setJobQueue (JobQueue& jobQueue)
{
    m_jobQueue = &jobQueue;
}

void NodeStoreScheduler::onStop ()
{
}

void NodeStoreScheduler::onChildrenStopped ()
{
    bassert (m_taskCount == 0);
    stopped ();
}

void NodeStoreScheduler::scheduleTask (NodeStore::Task& task)
{
    ++m_taskCount;
    m_jobQueue->addJob (
        jtWRITE,
        "NodeObject::store",
        std::bind (&NodeStoreScheduler::doTask,
            this, std::ref(task), std::placeholders::_1));
}

void NodeStoreScheduler::doTask (NodeStore::Task& task, Job&)
{
    task.performScheduledTask ();
    if ((--m_taskCount == 0) && isStopping())
        stopped();
}

void NodeStoreScheduler::onFetch (NodeStore::FetchReport const& report)
{
    if (report.wentToDisk)
        m_jobQueue->addLoadEvents (
            report.isAsync ? jtNS_ASYNC_READ : jtNS_SYNC_READ,
                1, report.elapsed);
}

void NodeStoreScheduler::onBatchWrite (NodeStore::BatchWriteReport const& report)
{
    m_jobQueue->addLoadEvents (jtNS_WRITE,
        report.writeCount, report.elapsed);
}

} // divvy
