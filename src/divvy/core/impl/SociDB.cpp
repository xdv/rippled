//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <divvy/core/ConfigSections.h>
#include <divvy/core/SociDB.h>
#include <divvy/core/Config.h>
#include <beast/cxx14/memory.h>  // <memory>
#include <backends/sqlite3/soci-sqlite3.h>
#include <boost/filesystem.hpp>

namespace divvy {

static auto checkpointPageCount = 1000;

namespace detail {

std::pair<std::string, soci::backend_factory const&>
getSociSqliteInit (std::string const& name,
                   std::string const& dir,
                   std::string const& ext)
{
    if (dir.empty () || name.empty ())
    {
        throw std::runtime_error (
            "Sqlite databases must specify a dir and a name. Name: " +
            name + " Dir: " + dir);
    }
    boost::filesystem::path file (dir);
    if (is_directory (file))
        file /= name + ext;
    return std::make_pair (file.string (), std::ref (soci::sqlite3));
}

std::pair<std::string, soci::backend_factory const&>
getSociInit (BasicConfig const& config,
             std::string const& dbName)
{
    auto const& section = config.section ("sqdb");
    auto const backendName = get(section, "backend", "sqlite");

    if (backendName != "sqlite")
        throw std::runtime_error ("Unsupported soci backend: " + backendName);

    auto const path = config.legacy ("database_path");
    auto const ext = dbName == "validators" || dbName == "peerfinder"
                ? ".sqlite" : ".db";
    return detail::getSociSqliteInit(dbName, path, ext);
}

} // detail

SociConfig::SociConfig (
    std::pair<std::string, soci::backend_factory const&> init)
    : connectionString_ (std::move (init.first)),
      backendFactory_ (init.second)
{
}

SociConfig::SociConfig (BasicConfig const& config, std::string const& dbName)
    : SociConfig (detail::getSociInit (config, dbName))
{
}

std::string SociConfig::connectionString () const
{
    return connectionString_;
}

void SociConfig::open (soci::session& s) const
{
    s.open (backendFactory_, connectionString ());
}

void open  (soci::session& s,
            BasicConfig const& config,
            std::string const& dbName)
{
    SociConfig (config, dbName).open(s);
}

void open (soci::session& s,
           std::string const& beName,
           std::string const& connectionString)
{
    if (beName == "sqlite")
        s.open(soci::sqlite3, connectionString);
    else
        throw std::runtime_error ("Unsupported soci backend: " + beName);
}

static
sqlite_api::sqlite3* getConnection (soci::session& s)
{
    sqlite_api::sqlite3* result = nullptr;
    auto be = s.get_backend ();
    if (auto b = dynamic_cast<soci::sqlite3_session_backend*> (be))
        result = b->conn_;

    if (! result)
        throw std::logic_error ("Didn't get a database connection.");

    return result;
}

size_t getKBUsedAll (soci::session& s)
{
    if (! getConnection (s))
        throw std::logic_error ("No connection found.");
    return static_cast <size_t> (sqlite_api::sqlite3_memory_used () / 1024);
}

size_t getKBUsedDB (soci::session& s)
{
    // This function will have to be customized when other backends are added
    if (auto conn = getConnection (s))
    {
        int cur = 0, hiw = 0;
        sqlite_api::sqlite3_db_status (
            conn, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
        return cur / 1024;
    }
    throw std::logic_error ("");
}

void convert (soci::blob& from, std::vector<std::uint8_t>& to)
{
    to.resize (from.get_len ());
    if (to.empty ())
        return;
    from.read (0, reinterpret_cast<char*>(&to[0]), from.get_len ());
}

void convert (soci::blob& from, std::string& to)
{
    std::vector<std::uint8_t> tmp;
    convert (from, tmp);
    to.assign (tmp.begin (), tmp.end());

}

void convert (std::vector<std::uint8_t> const& from, soci::blob& to)
{
    if (!from.empty ())
        to.write (0, reinterpret_cast<char const*>(&from[0]), from.size ());
    else
        to.trim (0);
}

void convert (std::string const& from, soci::blob& to)
{
    if (!from.empty ())
        to.write (0, from.data (), from.size ());
    else
        to.trim (0);
}

namespace {

/** Run a thread to checkpoint the write ahead log (wal) for
    the given soci::session every 1000 pages. This is only implemented
    for sqlite databases.

    Note: According to: https://www.sqlite.org/wal.html#ckpt this
    is the default behavior of sqlite. We may be able to remove this
    class.
*/
class WALCheckpointer : public Checkpointer
{
public:
    WALCheckpointer (sqlite_api::sqlite3& conn, JobQueue& q)
            : conn_ (conn), jobQueue_ (q)
    {
        sqlite_api::sqlite3_wal_hook (&conn_, &sqliteWALHook, this);
    }

    ~WALCheckpointer () override = default;

private:
    sqlite_api::sqlite3& conn_;
    std::mutex mutex_;
    JobQueue& jobQueue_;

    bool running_ = false;

    static
    int sqliteWALHook (
        void* cp, sqlite_api::sqlite3*, const char* dbName, int walSize)
    {
        if (walSize >= checkpointPageCount)
        {
            if (auto checkpointer = reinterpret_cast <WALCheckpointer*> (cp))
                checkpointer->scheduleCheckpoint();
            else
                throw std::logic_error ("Didn't get a WALCheckpointer");
        }
        return SQLITE_OK;
    }

    void scheduleCheckpoint ()
    {
        {
            std::lock_guard <std::mutex> lock (mutex_);
            if (running_)
                return;
            running_ = true;
        }

        jobQueue_.addJob (
            jtWAL, "WAL", std::bind (&WALCheckpointer::checkpoint, this));
    }

    void checkpoint ()
    {
        int log = 0, ckpt = 0;
        int ret = sqlite3_wal_checkpoint_v2 (
            &conn_, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

        auto fname = sqlite3_db_filename (&conn_, "main");
        if (ret != SQLITE_OK)
        {
            WriteLog ((ret == SQLITE_LOCKED) ? lsTRACE : lsWARNING,
                      WALCheckpointer)
                << "WAL(" << fname << "): error " << ret;
        }
        else
        {
            WriteLog (lsTRACE, WALCheckpointer)
                << "WAL(" << fname << "): frames="
                << log << ", written=" << ckpt;
        }

        std::lock_guard <std::mutex> lock (mutex_);
        running_ = false;
    }
};

} // namespace

std::unique_ptr <Checkpointer> makeCheckpointer (
    soci::session& session, JobQueue& queue)
{
    if (auto conn = getConnection (session))
        return std::make_unique <WALCheckpointer> (*conn, queue);
    return {};
}

}
