/**
 * @file mega/nodemanager.h
 * @brief Client access engine core logic
 *
 * (c) 2013-2023 by Mega Limited, Auckland, New Zealand
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

#include <thread>
#ifndef NODEMANAGER_H
#define NODEMANAGER_H 1

#include <map>
#include <set>
#include "node.h"
#include "types.h"

namespace mega {

class DBTableNodes;
struct FileFingerprint;
class FingerprintContainer;
class MegaClient;
class NodeSerialized;

class NodeSearchFilter
{
public:
    NodeSearchFilter(ShareType_t st = NO_SHARES) : mShareType(st) {}

    template<class T>
    void copyFrom(const T& f, ShareType_t shareType = NO_SHARES)
    {
        mNameFilter = f.byName() ? f.byName() : std::string(); // get it as const char*
        mMimeCategory = static_cast<MimeType_t>(f.byCategory()); // get it as int
        mExcludeSensitive = f.bySensitivity();
        mLocationHandle = f.byLocationHandle();
        mShareType = shareType;
        mCreationLowerLimit = f.byCreationTimeLowerLimit();
        mCreationUpperLimit = f.byCreationTimeUpperLimit();
    }

    const std::string& byName() const { return mNameFilter; }
    MimeType_t byCategory() const { return mMimeCategory; }
    bool bySensitivity() const { return mExcludeSensitive; }

    // recursive look-ups (searchNodes): represents 'ancestor';
    // non-recursive look-ups (getChildren): represents 'parent'.
    handle byLocationHandle() const { return mLocationHandle; }

    // recursive look-ups (searchNodes): type of share where search is restricted to;
    // non-recursive look-ups (getChildren): ignored.
    ShareType_t byShareType() const { return mShareType; }

    int64_t byCreationTimeLowerLimit() const { return mCreationLowerLimit; }
    int64_t byCreationTimeUpperLimit() const { return mCreationUpperLimit; }

private:
    std::string mNameFilter;
    MimeType_t mMimeCategory = MIME_TYPE_UNKNOWN;
    bool mExcludeSensitive = false;
    handle mLocationHandle = UNDEF;
    ShareType_t mShareType = NO_SHARES;
    int64_t mCreationLowerLimit = 0;
    int64_t mCreationUpperLimit = 0;
};

/**
 * @brief The NodeManager class
 *
 * This class encapsulates the access to nodes. It hides the details to
 * access to the Node object: in case it's not loaded in RAM, it will
 * load it from the "nodes" DB table.
 *
 * The same DB file is used for the "statecache" and the "nodes" table, and
 * both tables need to follow the same domain for transactions: a commit is
 * triggered by the reception of a sequence-number in the actionpacket (scsn).
 */
class MEGA_API NodeManager
{
public:
    NodeManager(MegaClient& client);

    // set interface to access to "nodes" table
    void setTable(DBTableNodes *table);

    // set interface to access to "nodes" table to nullptr, it's called just after sctable.reset()
    void reset();

    // Take node ownership
    typedef map<NodeHandle,  set<Node*>> MissingParentNodes;
    bool addNode(Node* node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes);
    bool updateNode(Node* node);
    // removeNode() --> it's done through notifypurge()

    // if node is not available in memory, it's loaded from DB
    Node *getNodeByHandle(NodeHandle handle);

    // read children from DB and load them in memory
    node_list getChildren(const Node *parent, CancelToken cancelToken = CancelToken());

    node_vector getChildren(const NodeSearchFilter& filter, CancelToken cancelFlag);

    // read children from type (folder or file) from DB and load them in memory
    node_vector getChildrenFromType(const NodeHandle &parent, nodetype_t type, CancelToken cancelToken);

    // get up to "maxcount" nodes, not older than "since", ordered by creation time
    // Note: nodes are read from DB and loaded in memory
    node_vector getRecentNodes(unsigned maxcount, m_time_t since);

    // Search nodes containing 'searchString' in its name
    // Returned nodes are children of 'nodeHandle' (at any level)
    // If 'nodeHandle' is UNDEF, search includes the whole account
    // If a cancelFlag is passed, it must be kept alive until this method returns
    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector search(NodeHandle ancestorHandle, const char* searchString, bool recursive, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag);

    node_vector searchNodes(const NodeSearchFilter& filter, CancelToken cancelFlag);

    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getInSharesWithName(const char *searchString, CancelToken cancelFlag);
    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getOutSharesWithName(const char *searchString, CancelToken cancelFlag);
    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getPublicLinksWithName(const char *searchString, CancelToken cancelFlag);


    node_vector getNodesByFingerprint(FileFingerprint& fingerprint);
    node_vector getNodesByOrigFingerprint(const std::string& fingerprint, Node *parent);
    Node *getNodeByFingerprint(FileFingerprint &fingerprint);

    // Return a first level child node whose name matches with 'name'
    // Valid values for nodeType: FILENODE, FOLDERNODE
    // Note: if not found among children loaded in RAM (and not all children are loaded), it will search in DB
    // Hint: ensure all children are loaded if this method is called for all children of a folder
    Node* childNodeByNameType(const Node *parent, const std::string& name, nodetype_t nodeType);

    // Returns ROOTNODE, INCOMINGNODE, RUBBISHNODE (In case of logged into folder link returns only ROOTNODE)
    // Load from DB if it's necessary
    node_vector getRootNodes();

    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getNodesWithInShares(); // both, top-level and nested ones
    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getNodesWithOutShares();

    node_vector getNodesWithPendingOutShares();

    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getNodesWithLinks();
    /** @deprecated Use searchNodes(const NodeSearchFilter...) instead */
    node_vector getNodesByMimeType(MimeType_t mimeType, NodeHandle ancestorHandle, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag);

    std::vector<NodeHandle> getFavouritesNodeHandles(NodeHandle node, uint32_t count);
    size_t getNumberOfChildrenFromNode(NodeHandle parentHandle);

    // Returns the number of children nodes of specific node type with a query to DB
    // Valid types are FILENODE and FOLDERNODE
    size_t getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType);

    // true if 'node' is a child node of 'ancestor', false otherwise.
    bool isAncestor(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag);

    // Clean 'changed' flag from all nodes
    void removeChanges();

    // Remove all nodes from all caches
    void cleanNodes();

    // Use blob received as parameter to generate a node
    // Used to generate nodes from old cache
    Node* getNodeFromBlob(const string* nodeSerialized);

    // attempt to apply received keys to decrypt node's keys
    void applyKeys(uint32_t appliedKeys);

    // add node to the notification queue
    void notifyNode(Node* node, node_vector* nodesToReport = nullptr);

    // for consistently notifying when updating node counters
    void setNodeCounter(Node* n, const NodeCounter &counter, bool notify, node_vector* nodesToReport);

    // process notified/changed nodes from 'mNodeNotify': dump changes to DB
    void notifyPurge();

    size_t nodeNotifySize() const;

    // Returns if cache has been loaded
    bool hasCacheLoaded();

    // Load rootnodes (ROOTNODE, INCOMING, RUBBISH), its first-level children
    // and root of incoming shares. Return true if success, false if error
    bool loadNodes();

    // Returns total of nodes in the account (cloud+inbox+rubbish AND inshares), including versions
    uint64_t getNodeCount();

    // return the counter for all root nodes (cloud+inbox+rubbish)
    NodeCounter getCounterOfRootNodes();

    // update the counter of 'n' when its parent is updated (from 'oldParent' to 'n.parent')
    void updateCounter(Node &n, Node *oldParent);

    // true if 'h' is a rootnode: cloud, inbox or rubbish bin
    bool isRootNode(NodeHandle h) const;

    // Set values to mClient.rootnodes for ROOTNODE, INBOX and RUBBISH
    bool setrootnode(Node* node);

    // Add fingerprint to mFingerprint. If node isn't going to keep in RAM
    // node isn't added
    FingerprintPosition insertFingerprint(Node* node);
    // Remove fingerprint from mFingerprint
    void removeFingerprint(Node* node);
    FingerprintPosition invalidFingerprintPos();

    // Node has received last updates and it's ready to store in DB
    void saveNodeInDb(Node *node);

    // write all nodes into DB (used for migration from legacy to NOD DB schema)
    void dumpNodes();

    // This method only can be used in Megacli for testing purposes
    uint64_t getNumberNodesInRam() const;

    // Add new relationship between parent and child
    void addChild(NodeHandle parent, NodeHandle child, Node *node);
    // remove relationship between parent and child
    void removeChild(Node *parent, NodeHandle child);

    // Returns the number of versions for a node (including the current version)
    int getNumVersions(NodeHandle nodeHandle);

    NodeHandle getRootNodeFiles() const;
    NodeHandle getRootNodeVault() const;
    NodeHandle getRootNodeRubbish() const;
    void setRootNodeFiles(NodeHandle h);
    void setRootNodeVault(NodeHandle h);
    void setRootNodeRubbish(NodeHandle h);

    // In case of orphans send an event
    void checkOrphanNodes(MissingParentNodes& nodesWithMissingParent);

    // This method is called when initial fetch nodes is finished
    // Initialize node counters and create indexes at DB
    void initCompleted();

    // true when the filesystem has been initialized
    bool ready();

private:
    MegaClient& mClient;

#if defined(DEBUG)
    using MutexType = CheckableMutex<std::recursive_mutex>;
#else // DEBUG
    using MutexType = std::recursive_mutex;
#endif // ! DEBUG

    using LockGuard = std::lock_guard<MutexType>;

    mutable MutexType mMutex;

    // interface to handle accesses to "nodes" table
    DBTableNodes* mTable = nullptr;

    // root nodes (files, vault, rubbish)
    struct Rootnodes
    {
        NodeHandle files;
        NodeHandle vault;
        NodeHandle rubbish;

        // returns true if the 'h' provided matches any of the rootnodes.
        // (when logged into folder links, the handle of the folder is set to 'files')
        bool isRootNode(NodeHandle h) const { return (h == files || h == vault || h == rubbish); }
    } rootnodes;

    class FingerprintContainer : public fingerprint_set
    {
    public:
        bool allFingerprintsAreLoaded(const FileFingerprint *fingerprint) const;
        void setAllFingerprintLoaded(const FileFingerprint *fingerprint);
        void clear();

    private:
        // it stores all FileFingerprint that have been looked up in DB, so it
        // avoid the DB query for future lookups (includes non-existing (yet) fingerprints)
        std::set<FileFingerprint, FileFingerprintCmp> mAllFingerprintsLoaded;
    };

    // Stores nodes that have been loaded in RAM from DB (not necessarily all of them)
    std::map<NodeHandle, NodeManagerNode> mNodes;

    uint64_t mNodesInRam = 0;

    // nodes that have changed and are pending to notify to app and dump to DB
    node_vector mNodeNotify;

    Node* getNodeInRAM(NodeHandle handle);
    void saveNodeInRAM(Node* node, bool isRootnode, MissingParentNodes& missingParentNodes);    // takes ownership

    /** @deprecated */
    node_vector getNodesWithSharesOrLink_internal(ShareType_t shareType);

    enum OperationType
    {
        INCREASE = 0,
        DECREASE,
    };

    // Update a node counter for 'origin' and its subtree (recursively)
    // If operationType is INCREASE, nc is added, in other case is decreased (ie. upon deletion)
    void updateTreeCounter(Node* origin, NodeCounter nc, OperationType operation, node_vector* nodesToReport);

    // returns nullptr if there are unserialization errors. Also triggers a full reload (fetchnodes)
    Node* getNodeFromNodeSerialized(const NodeSerialized& nodeSerialized);

    // reads from DB and loads the node in memory
    Node* unserializeNode(const string*, bool fromOldCache);

    // returns the counter for the specified node, calculating it recursively and accessing to DB if it's neccesary
    NodeCounter calculateNodeCounter(const NodeHandle &nodehandle, nodetype_t parentType, Node *node, bool isInRubbish);

    // Container storing FileFingerprint* (Node* in practice) ordered by fingerprint
    FingerprintContainer mFingerPrints;

    // Return a node from Data base, node shouldn't be in RAM previously
    Node* getNodeFromDataBase(NodeHandle handle);

    // Returns root nodes without nested in-shares
    node_vector getRootNodesAndInshares();

    // Process unserialized nodes read from DB
    // Avoid loading nodes whose ancestor is not ancestorHandle. If ancestorHandle is undef load all nodes
    // If a valid cancelFlag is passed and takes true value, this method returns without complete operation
    // If a valid object is passed, it must be kept alive until this method returns.
    node_vector processUnserializedNodes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& nodesFromTable, NodeHandle ancestorHandle = NodeHandle(), CancelToken cancelFlag = CancelToken());

    node_vector searchNodes_internal(const NodeSearchFilter& filter, CancelToken cancelFlag);
    node_vector processUnserializedNodes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& nodesFromTable, const NodeSearchFilter& filter, CancelToken cancelFlag = CancelToken());
    node_vector getChildren_internal(const NodeSearchFilter& filter, CancelToken cancelFlag);
    node_vector processUnserializedChildren(const std::vector<std::pair<NodeHandle, NodeSerialized>>& childrenFromTable, const NodeSearchFilter& filter, CancelToken cancelFlag = CancelToken());

    // node temporary in memory, which will be removed upon write to DB
    unique_ptr<Node> mNodeToWriteInDb;

    // Stores (or updates) the node in the DB. It also tries to decrypt it for the last time before storing it.
    void putNodeInDb(Node* node) const;

    // true when the NodeManager has been inicialized and contains a valid filesystem
    bool mInitialized = false;

    // These are all the "internal" versions of the public interfaces.
    // This is to avoid confusion where public functions used to call other public functions
    // but that introudces confusion about where the mutex gets locked.
    // Now the public interfaces lock the mutex once, and call these internal functions
    // which have all the original code in them.
    // Internal functions only call other internal fucntions, and that keeps things simple
    // We would use a non-recursive mutex for more precise control, and to make sure we can unlock
    // it when we need to make callbacks to the app.
    // It's quite a verbose approach, but at least simple, easy to understand, and easy to get right.
    void setTable_internal(DBTableNodes *table);
    void reset_internal();
    bool addNode_internal(Node* node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes);
    bool updateNode_internal(Node* node);
    Node *getNodeByHandle_internal(NodeHandle handle);
    node_list getChildren_internal(const Node *parent, CancelToken cancelToken = CancelToken());
    node_vector getChildrenFromType_internal(const NodeHandle& parent, nodetype_t type, CancelToken cancelToken);
    node_vector getRecentNodes_internal(unsigned maxcount, m_time_t since);

    /** @deprecated */
    node_vector search_internal(NodeHandle ancestorHandle, const char* searchString, bool recursive, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag);
    /** @deprecated */
    node_vector getInSharesWithName_internal(const char *searchString, CancelToken cancelFlag);
    /** @deprecated */
    node_vector getOutSharesWithName_internal(const char *searchString, CancelToken cancelFlag);
    /** @deprecated */
    node_vector getPublicLinksWithName_internal(const char *searchString, CancelToken cancelFlag);

    node_vector getNodesByFingerprint_internal(FileFingerprint& fingerprint);
    node_vector getNodesByOrigFingerprint_internal(const std::string& fingerprint, Node *parent);
    Node *getNodeByFingerprint_internal(FileFingerprint &fingerprint);
    Node* childNodeByNameType_internal(const Node *parent, const std::string& name, nodetype_t nodeType);
    node_vector getRootNodes_internal();

    /** @deprecated */
    node_vector getNodesWithInShares_internal(); // both, top-level and nested ones
    /** @deprecated */
    node_vector getNodesByMimeType_internal(MimeType_t mimeType, NodeHandle ancestorHandle, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelFlag);

    std::vector<NodeHandle> getFavouritesNodeHandles_internal(NodeHandle node, uint32_t count);
    size_t getNumberOfChildrenFromNode_internal(NodeHandle parentHandle);
    size_t getNumberOfChildrenByType_internal(NodeHandle parentHandle, nodetype_t nodeType);
    bool isAncestor_internal(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag);
    void removeChanges_internal();
    void cleanNodes_internal();
    Node* getNodeFromBlob_internal(const string* nodeSerialized);
    void applyKeys_internal(uint32_t appliedKeys);
    void notifyNode_internal(Node* node, node_vector* nodesToReport);
    bool loadNodes_internal();
    uint64_t getNodeCount_internal();
    NodeCounter getCounterOfRootNodes_internal();
    void updateCounter_internal(Node &n, Node *oldParent);
    bool setrootnode_internal(Node* node);
    FingerprintPosition insertFingerprint_internal(Node* node);
    void removeFingerprint_internal(Node* node);
    void saveNodeInDb_internal(Node *node);
    void dumpNodes_internal();
    void addChild_internal(NodeHandle parent, NodeHandle child, Node *node);
    void removeChild_internal(Node *parent, NodeHandle child);
    void setRootNodeFiles_internal(NodeHandle h);
    void setRootNodeVault_internal(NodeHandle h);
    void setRootNodeRubbish_internal(NodeHandle h);
    void initCompleted_internal();
};

} // namespace

#endif
