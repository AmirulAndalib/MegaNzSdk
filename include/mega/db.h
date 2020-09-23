/**
 * @file mega/db.h
 * @brief Database access interface
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_DB_H
#define MEGA_DB_H 1

#include "filesystem.h"

namespace mega {
// generic host transactional database access interface
class DBTableTransactionCommitter;

class NodeSerialized
{
public:
    bool mDecrypted = true;
    std::string mNode;
};

class MEGA_API DbTable
{
    static const int IDSPACING = 16;
    PrnGen &rng;

protected:
    bool mCheckAlwaysTransacted = false;
    DBTableTransactionCommitter* mTransactionCommitter = nullptr;
    friend class DBTableTransactionCommitter;
    void checkTransaction();
    // should be called by the subclass' destructor
    void resetCommitter();

public:
    typedef enum { NO_SHARES = 0x00, IN_SHARES = 0x01, OUT_SHARES = 0x02, PENDING_SHARES = 0x04, LINK = 0x08} sharesOrLink_t;

    // for a full sequential get: rewind to first record
    virtual void rewind() = 0;

    // get next record in sequence
    virtual bool next(uint32_t*, string*) = 0;
    bool next(uint32_t*, string*, SymmCipher*);

    // get specific record by key
    virtual bool get(uint32_t, string*) = 0;

    virtual bool getNode(handle nodehandle, NodeSerialized& nodeSerialized) = 0;
    virtual bool getNodes(std::vector<NodeSerialized>& nodes) = 0;
    virtual bool getNodesByFingerprint(const FileFingerprint& fingerprint, std::map<mega::handle, NodeSerialized>& nodes) = 0;
    virtual bool getNodesByOrigFingerprint(const std::string& fingerprint, std::map<mega::handle, NodeSerialized>& nodes) = 0;
    virtual bool getNodeByFingerprint(const FileFingerprint& fingerprint, NodeSerialized& node) = 0;
    virtual bool getNodesWithoutParent(std::vector<NodeSerialized>& nodes) = 0;
    virtual bool getNodesWithSharesOrLink(std::vector<NodeSerialized>& nodes, sharesOrLink_t shareType) = 0;
    virtual bool getChildrenFromNode(handle node, std::map<handle, NodeSerialized>& nodes) = 0;
    virtual bool getChildrenHandlesFromNode(handle node, std::vector<handle>& nodes) = 0;
    virtual bool getNodesByName(const std::string& name, std::map<mega::handle, NodeSerialized>& nodes) = 0;
    virtual NodeCounter getNodeCounter(handle node) = 0;
    virtual uint32_t getNumberOfChildrenFromNode(handle node) = 0;
    virtual bool isNodesOnDemandDb() = 0;
    virtual handle getFirstAncestor(handle node) = 0;
    virtual bool isNodeInDB(handle node) = 0;
    virtual bool isAncestor(handle node, handle ancestror) = 0;

    // update or add specific record
    virtual bool put(uint32_t, char*, unsigned) = 0;
    virtual bool put(Node* node) = 0;
    bool put(uint32_t, string*);
    bool put(uint32_t, Cacheable *, SymmCipher*);

    // delete specific record
    virtual bool del(uint32_t) = 0;
    virtual bool del(handle nodehandle) = 0;
    virtual bool removeNodes() = 0;

    // delete all records
    virtual void truncate() = 0;

    // begin transaction
    virtual void begin() = 0;

    // commit transaction
    virtual void commit() = 0;

    // abort transaction
    virtual void abort() = 0;

    // permanantly remove all database info
    virtual void remove() = 0;

    virtual std::string getVar(const std::string& name) = 0;
    virtual bool setVar(const std::string& name, const std::string& value) = 0;

    void checkCommitter(DBTableTransactionCommitter*);

    // autoincrement
    uint32_t nextid;

    DbTable(PrnGen &rng, bool alwaysTransacted);
    virtual ~DbTable() { }
    DBTableTransactionCommitter *getTransactionCommitter() const;
};

class MEGA_API DBTableTransactionCommitter
{
    DbTable* mTable;
    bool mStarted = false;

public:
    void beginOnce()
    {
        if (mTable && !mStarted)
        {
            mTable->begin();
            mStarted = true;
        }
    }

    void commitNow()
    {
        if (mTable)
        {
            if (mStarted)
            {
                mTable->commit();
                mStarted = false;
            }
        }
    }

    void reset()
    {
        mTable = nullptr;
    }

    ~DBTableTransactionCommitter()
    {
        if (mTable)
        {
            commitNow();
            mTable->mTransactionCommitter = nullptr;
        }
    }

    explicit DBTableTransactionCommitter(DbTable* t)
        : mTable(t)
    {
        if (mTable)
        {
            if (mTable->mTransactionCommitter)
            {
                mTable = nullptr;  // we are nested; this one does nothing.  This can occur during eg. putnodes response when the core sdk and the intermediate layer both do db work.
            }
            else
            {
                mTable->mTransactionCommitter = this;
            }
        }
    }

    MEGA_DISABLE_COPY_MOVE(DBTableTransactionCommitter)
};

struct MEGA_API DbAccess
{
    static const int LEGACY_DB_VERSION = 11;
    static const int DB_VERSION = LEGACY_DB_VERSION + 1;

    DbAccess();
    virtual DbTable* open(PrnGen &rng, FileSystemAccess*, string*, bool recycleLegacyDB, bool checkAlwaysTransacted) = 0;

    virtual ~DbAccess() { }

    int currentDbVersion;
};
} // namespace

#endif
