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
#include <divvy/shamap/SHAMap.h>
#include <divvy/nodestore/Database.h>
#include <beast/unit_test/suite.h>

namespace divvy {

// VFALCO TODO tidy up this global

static const uint256 uZero;

static bool visitLeavesHelper (
    std::function <void (std::shared_ptr<SHAMapItem> const&)> const& function,
    SHAMapAbstractNode& node)
{
    // Adapt visitNodes to visitLeaves
    if (!node.isInner ())
        function (static_cast<SHAMapTreeNode&>(node).peekItem ());

    return false;
}

void
SHAMap::visitLeaves(
    std::function<void(std::shared_ptr<SHAMapItem> const& item)> const& leafFunction) const
{
    visitNodes (std::bind (visitLeavesHelper,
            std::cref (leafFunction), std::placeholders::_1));
}

void SHAMap::visitNodes(std::function<bool (SHAMapAbstractNode&)> const& function) const
{
    // Visit every node in a SHAMap
    assert (root_->isValid ());

    if (!root_)
        return;

    function (*root_);

    if (!root_->isInner ())
        return;

    using StackEntry = std::pair <int, std::shared_ptr<SHAMapInnerNode>>;
    std::stack <StackEntry, std::vector <StackEntry>> stack;

    auto node = std::static_pointer_cast<SHAMapInnerNode>(root_);
    int pos = 0;

    while (1)
    {
        while (pos < 16)
        {
            uint256 childHash;
            if (!node->isEmptyBranch (pos))
            {
                std::shared_ptr<SHAMapAbstractNode> child = descendNoStore (node, pos);
                if (function (*child))
                    return;

                if (child->isLeaf ())
                    ++pos;
                else
                {
                    // If there are no more children, don't push this node
                    while ((pos != 15) && (node->isEmptyBranch (pos + 1)))
                           ++pos;

                    if (pos != 15)
                    {
                        // save next position to resume at
                        stack.push (std::make_pair(pos + 1, std::move (node)));
                    }

                    // descend to the child's first position
                    node = std::static_pointer_cast<SHAMapInnerNode>(child);
                    pos = 0;
                }
            }
            else
            {
                ++pos; // move to next position
            }
        }

        if (stack.empty ())
            break;

        std::tie(pos, node) = stack.top ();
        stack.pop ();
    }
}

/** Get a list of node IDs and hashes for nodes that are part of this SHAMap
    but not available locally.  The filter can hold alternate sources of
    nodes that are not permanently stored locally
*/
void
SHAMap::getMissingNodes(std::vector<SHAMapNodeID>& nodeIDs, std::vector<uint256>& hashes,
                        int max, SHAMapSyncFilter* filter)
{
    assert (root_->isValid ());
    assert (root_->getNodeHash().isNonZero ());

    std::uint32_t generation = f_.fullbelow().getGeneration();

    if (!root_->isInner ())
    {
        if (generation == 0)
            clearSynching();
        else if (journal_.warning) journal_.warning <<
            "synching empty tree";
        return;
    }

    if (std::static_pointer_cast<SHAMapInnerNode>(root_)->isFullBelow (generation))
    {
        clearSynching ();
        return;
    }

    int const maxDefer = f_.db().getDesiredAsyncReadCount ();

    // Track the missing hashes we have found so far
    std::set <uint256> missingHashes;


    while (1)
    {
        std::vector <std::tuple <SHAMapInnerNode*, int, SHAMapNodeID>> deferredReads;
        deferredReads.reserve (maxDefer + 16);

        using StackEntry = std::tuple<SHAMapInnerNode*, SHAMapNodeID, int, int, bool>;
        std::stack <StackEntry, std::vector<StackEntry>> stack;

        // Traverse the map without blocking

        auto node = static_cast<SHAMapInnerNode*>(root_.get());
        SHAMapNodeID nodeID;

        // The firstChild value is selected randomly so if multiple threads
        // are traversing the map, each thread will start at a different
        // (randomly selected) inner node.  This increases the likelihood
        // that the two threads will produce different request sets (which is
        // more efficient than sending identical requests).
        int firstChild = rand() % 256;
        int currentChild = 0;
        bool fullBelow = true;

        do
        {
            while (currentChild < 16)
            {
                int branch = (firstChild + currentChild++) % 16;
                if (!node->isEmptyBranch (branch))
                {
                    uint256 const& childHash = node->getChildHash (branch);

                    if (missingHashes.count (childHash) != 0)
                    {
                        fullBelow = false;
                    }
                    else if (! backed_ || ! f_.fullbelow().touch_if_exists (childHash))
                    {
                        SHAMapNodeID childID = nodeID.getChildNodeID (branch);
                        bool pending = false;
                        auto d = descendAsync (node, branch, childID, filter, pending);

                        if (!d)
                        {
                            if (!pending)
                            { // node is not in the database
                                nodeIDs.push_back (childID);
                                hashes.push_back (childHash);

                                if (--max <= 0)
                                    return;
                            }
                            else
                            {
                                // read is deferred
                                deferredReads.emplace_back (node, branch, childID);
                            }

                            fullBelow = false; // This node is not known full below
                        }
                        else if (d->isInner() &&
                                 !static_cast<SHAMapInnerNode*>(d)->isFullBelow(generation))
                        {
                            stack.push (std::make_tuple (node, nodeID,
                                          firstChild, currentChild, fullBelow));

                            // Switch to processing the child node
                            node = static_cast<SHAMapInnerNode*>(d);
                            nodeID = childID;
                            firstChild = rand() % 256;
                            currentChild = 0;
                            fullBelow = true;
                        }
                    }
                }
            }

            // We are done with this inner node (and thus all of its children)

            if (fullBelow)
            { // No partial node encountered below this node
                node->setFullBelowGen (generation);
                if (backed_)
                    f_.fullbelow().insert (node->getNodeHash ());
            }

            if (stack.empty ())
                node = nullptr; // Finished processing the last node, we are done
            else
            { // Pick up where we left off (above this node)
                bool was;
                std::tie(node, nodeID, firstChild, currentChild, was) = stack.top ();
                fullBelow = was && fullBelow; // was and still is
                stack.pop ();
            }

        }
        while ((node != nullptr) && (deferredReads.size () <= maxDefer));

        // If we didn't defer any reads, we're done
        if (deferredReads.empty ())
            break;

        auto const before = std::chrono::steady_clock::now();
        f_.db().waitReads();
        auto const after = std::chrono::steady_clock::now();

        auto const elapsed = std::chrono::duration_cast
            <std::chrono::milliseconds> (after - before);
        auto const count = deferredReads.size ();

        // Process all deferred reads
        int hits = 0;
        for (auto const& node : deferredReads)
        {
            auto parent = std::get<0>(node);
            auto branch = std::get<1>(node);
            auto const& nodeID = std::get<2>(node);
            auto const& nodeHash = parent->getChildHash (branch);

            auto nodePtr = fetchNodeNT(nodeID, nodeHash, filter);
            if (nodePtr)
            {
                ++hits;
                if (backed_)
                    canonicalize (nodeHash, nodePtr);
                nodePtr = parent->canonicalizeChild (branch, std::move(nodePtr));
            }
            else if ((max > 0) && (missingHashes.insert (nodeHash).second))
            {
                nodeIDs.push_back (nodeID);
                hashes.push_back (nodeHash);

                --max;
            }
        }

        auto const process_time = std::chrono::duration_cast
            <std::chrono::milliseconds> (std::chrono::steady_clock::now() - after);

        if ((count > 50) || (elapsed.count() > 50))
            journal_.debug << "getMissingNodes reads " <<
                count << " nodes (" << hits << " hits) in "
                << elapsed.count() << " + " << process_time.count()  << " ms";

        if (max <= 0)
            return;

    }

    if (nodeIDs.empty ())
        clearSynching ();
}

std::vector<uint256> SHAMap::getNeededHashes (int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> nodeHashes;
    nodeHashes.reserve(max);

    std::vector<SHAMapNodeID> nodeIDs;
    nodeIDs.reserve(max);

    getMissingNodes(nodeIDs, nodeHashes, max, filter);
    return nodeHashes;
}

bool SHAMap::getNodeFat (SHAMapNodeID wanted,
    std::vector<SHAMapNodeID>& nodeIDs,
        std::vector<Blob>& rawNodes, bool fatLeaves,
            std::uint32_t depth) const
{
    // Gets a node and some of its children
    // to a specified depth

    auto node = root_.get();
    SHAMapNodeID nodeID;

    while (node && node->isInner () && (nodeID.getDepth() < wanted.getDepth()))
    {
        int branch = nodeID.selectBranch (wanted.getNodeID());
        auto inner = static_cast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch (branch))
            return false;

        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID (branch);
    }

    if (!node || (nodeID != wanted))
    {
        if (journal_.warning) journal_.warning <<
            "peer requested node that is not in the map: " << wanted;
        return false;
    }

    if (node->isInner() && static_cast<SHAMapInnerNode*>(node)->isEmpty())
    {
        if (journal_.warning) journal_.warning <<
            "peer requests empty node";
        return false;
    }

    std::stack<std::tuple <SHAMapAbstractNode*, SHAMapNodeID, int>> stack;
    stack.emplace (node, nodeID, depth);

    while (! stack.empty ())
    {
        std::tie (node, nodeID, depth) = stack.top ();
        stack.pop ();

        // Add this node to the reply
        Serializer s;
        node->addRaw (s, snfWIRE);
        nodeIDs.push_back (nodeID);
        rawNodes.push_back (std::move (s.peekData()));

        if (node->isInner())
        {
            // We descend inner nodes with only a single child
            // without decrementing the depth
            auto inner = static_cast<SHAMapInnerNode*>(node);
            int bc = inner->getBranchCount();
            if ((depth > 0) || (bc == 1))
            {
                // We need to process this node's children
                for (int i = 0; i < 16; ++i)
                {
                    if (! inner->isEmptyBranch (i))
                    {
                        auto childID = nodeID.getChildNodeID (i);
                        auto childNode = descendThrow (inner, i);

                        if (childNode->isInner () &&
                            ((depth > 1) || (bc == 1)))
                        {
                            // If there's more than one child, reduce the depth
                            // If only one child, follow the chain
                            stack.emplace (childNode, childID,
                                (bc > 1) ? (depth - 1) : depth);
                        }
                        else if (childNode->isInner() || fatLeaves)
                        {
                            // Just include this node
                            Serializer s;
                            childNode->addRaw (s, snfWIRE);
                            nodeIDs.push_back (childID);
                            rawNodes.push_back (std::move (s.peekData ()));
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool SHAMap::getRootNode (Serializer& s, SHANodeFormat format) const
{
    root_->addRaw (s, format);
    return true;
}

SHAMapAddNode SHAMap::addRootNode (Blob const& rootNode,
    SHANodeFormat format, SHAMapSyncFilter* filter)
{
    // we already have a root_ node
    if (root_->getNodeHash ().isNonZero ())
    {
        if (journal_.trace) journal_.trace <<
            "got root node, already have one";
        return SHAMapAddNode::duplicate ();
    }

    assert (seq_ >= 1);
    auto node = SHAMapAbstractNode::make(rootNode, 0, format, uZero, false);
    if (!node || !node->isValid ())
        return SHAMapAddNode::invalid ();

#ifdef BEAST_DEBUG
    node->dump (SHAMapNodeID (), journal_);
#endif

    if (backed_)
        canonicalize (node->getNodeHash (), node);

    root_ = node;

    if (root_->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root_->addRaw (s, snfPREFIX);
        filter->gotNode (false, SHAMapNodeID{}, root_->getNodeHash (),
                         s.modData (), root_->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode SHAMap::addRootNode (uint256 const& hash, Blob const& rootNode, SHANodeFormat format,
                                   SHAMapSyncFilter* filter)
{
    // we already have a root_ node
    if (root_->getNodeHash ().isNonZero ())
    {
        if (journal_.trace) journal_.trace <<
            "got root node, already have one";
        assert (root_->getNodeHash () == hash);
        return SHAMapAddNode::duplicate ();
    }

    assert (seq_ >= 1);
    auto node = SHAMapAbstractNode::make(rootNode, 0, format, uZero, false);
    if (!node || !node->isValid() || node->getNodeHash () != hash)
        return SHAMapAddNode::invalid ();

    if (backed_)
        canonicalize (hash, node);

    root_ = node;

    if (root_->isLeaf())
        clearSynching ();

    if (filter)
    {
        Serializer s;
        root_->addRaw (s, snfPREFIX);
        filter->gotNode (false, SHAMapNodeID{}, root_->getNodeHash (), s.modData (),
                         root_->getType ());
    }

    return SHAMapAddNode::useful ();
}

SHAMapAddNode
SHAMap::addKnownNode (const SHAMapNodeID& node, Blob const& rawNode,
                      SHAMapSyncFilter* filter)
{
    // return value: true=okay, false=error
    assert (!node.isRoot ());

    if (!isSynching ())
    {
        if (journal_.trace) journal_.trace <<
            "AddKnownNode while not synching";
        return SHAMapAddNode::duplicate ();
    }

    std::uint32_t generation = f_.fullbelow().getGeneration();
    SHAMapNodeID iNodeID;
    auto iNode = root_.get();

    while (iNode->isInner () &&
           !static_cast<SHAMapInnerNode*>(iNode)->isFullBelow(generation) &&
           (iNodeID.getDepth () < node.getDepth ()))
    {
        int branch = iNodeID.selectBranch (node.getNodeID ());
        assert (branch >= 0);
        auto inner = static_cast<SHAMapInnerNode*>(iNode);
        if (inner->isEmptyBranch (branch))
        {
            if (journal_.warning) journal_.warning <<
                "Add known node for empty branch" << node;
            return SHAMapAddNode::invalid ();
        }

        uint256 childHash = inner->getChildHash (branch);
        if (f_.fullbelow().touch_if_exists (childHash))
            return SHAMapAddNode::duplicate ();

        auto prevNode = inner;
        std::tie(iNode, iNodeID) = descend(inner, iNodeID, branch, filter);

        if (!iNode)
        {
            if (iNodeID != node)
            {
                // Either this node is broken or we didn't request it (yet)
                if (journal_.warning) journal_.warning <<
                    "unable to hook node " << node;
                if (journal_.info) journal_.info <<
                    " stuck at " << iNodeID;
                if (journal_.info) journal_.info <<
                    "got depth=" << node.getDepth () <<
                        ", walked to= " << iNodeID.getDepth ();
                return SHAMapAddNode::invalid ();
            }

            auto newNode = SHAMapAbstractNode::make(rawNode, 0, snfWIRE, uZero, false);

            if (!newNode || !newNode->isValid() || childHash != newNode->getNodeHash ())
            {
                if (journal_.warning) journal_.warning <<
                    "Corrupt node received";
                return SHAMapAddNode::invalid ();
            }

            if (!newNode->isInBounds (iNodeID))
            {
                // Map is provably invalid
                state_ = SHAMapState::Invalid;
                return SHAMapAddNode::useful ();
            }

            if (backed_)
                canonicalize (childHash, newNode);

            newNode = prevNode->canonicalizeChild (branch, std::move(newNode));

            if (filter)
            {
                Serializer s;
                newNode->addRaw (s, snfPREFIX);
                filter->gotNode (false, node, childHash,
                                 s.modData (), newNode->getType ());
            }

            return SHAMapAddNode::useful ();
        }
    }

    if (journal_.trace) journal_.trace <<
        "got node, already had it (late)";
    return SHAMapAddNode::duplicate ();
}

bool SHAMap::deepCompare (SHAMap& other) const
{
    // Intended for debug/test only
    std::stack <std::pair <SHAMapAbstractNode*, SHAMapAbstractNode*> > stack;

    stack.push ({root_.get(), other.root_.get()});

    while (!stack.empty ())
    {
        SHAMapAbstractNode* node;
        SHAMapAbstractNode* otherNode;
        std::tie(node, otherNode) = stack.top ();
        stack.pop ();

        if (!node || !otherNode)
        {
            if (journal_.info) journal_.info <<
                "unable to fetch node";
            return false;
        }
        else if (otherNode->getNodeHash () != node->getNodeHash ())
        {
            if (journal_.warning) journal_.warning <<
                "node hash mismatch";
            return false;
        }

        if (node->isLeaf ())
        {
            if (!otherNode->isLeaf ())
                 return false;
            auto& nodePeek = static_cast<SHAMapTreeNode*>(node)->peekItem();
            auto& otherNodePeek = static_cast<SHAMapTreeNode*>(otherNode)->peekItem();
            if (nodePeek->getTag() != otherNodePeek->getTag())
                return false;
            if (nodePeek->peekData() != otherNodePeek->peekData())
                return false;
        }
        else if (node->isInner ())
        {
            if (!otherNode->isInner ())
                return false;
            auto node_inner = static_cast<SHAMapInnerNode*>(node);
            auto other_inner = static_cast<SHAMapInnerNode*>(otherNode);
            for (int i = 0; i < 16; ++i)
            {
                if (node_inner->isEmptyBranch (i))
                {
                    if (!other_inner->isEmptyBranch (i))
                        return false;
                }
                else
                {
                    if (other_inner->isEmptyBranch (i))
                       return false;

                    auto next = descend(node_inner, i);
                    auto otherNext = other.descend(other_inner, i);
                    if (!next || !otherNext)
                    {
                        if (journal_.warning) journal_.warning <<
                            "unable to fetch inner node";
                        return false;
                    }
                    stack.push ({next, otherNext});
                }
            }
        }
    }

    return true;
}

/** Does this map have this inner node?
*/
bool
SHAMap::hasInnerNode (SHAMapNodeID const& targetNodeID,
                      uint256 const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    while (node->isInner () && (nodeID.getDepth () < targetNodeID.getDepth ()))
    {
        int branch = nodeID.selectBranch (targetNodeID.getNodeID ());
        auto inner = static_cast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch (branch))
            return false;

        node = descendThrow (inner, branch);
        nodeID = nodeID.getChildNodeID (branch);
    }

    return (node->isInner()) && (node->getNodeHash() == targetNodeHash);
}

/** Does this map have this leaf node?
*/
bool
SHAMap::hasLeafNode (uint256 const& tag, uint256 const& targetNodeHash) const
{
    auto node = root_.get();
    SHAMapNodeID nodeID;

    if (!node->isInner()) // only one leaf node in the tree
        return node->getNodeHash() == targetNodeHash;

    do
    {
        int branch = nodeID.selectBranch (tag);
        auto inner = static_cast<SHAMapInnerNode*>(node);
        if (inner->isEmptyBranch (branch))
            return false;   // Dead end, node must not be here

        if (inner->getChildHash (branch) == targetNodeHash) // Matching leaf, no need to retrieve it
            return true;

        node = descendThrow(inner, branch);
        nodeID = nodeID.getChildNodeID (branch);
    }
    while (node->isInner());

    return false; // If this was a matching leaf, we would have caught it already
}

/**
@param have A pointer to the map that the recipient already has (if any).
@param includeLeaves True if leaf nodes should be included.
@param max The maximum number of nodes to return.
@param func The functor to call for each node added to the FetchPack.

Note: a caller should set includeLeaves to false for transaction trees.
There's no point in including the leaves of transaction trees.
*/
void SHAMap::getFetchPack (SHAMap* have, bool includeLeaves, int max,
                           std::function<void (uint256 const&, const Blob&)> func) const
{
    visitDifferences (have,
        [includeLeaves, &max, &func] (SHAMapAbstractNode& smn) -> bool
        {
            if (includeLeaves || smn.isInner ())
            {
                Serializer s;
                smn.addRaw (s, snfPREFIX);
                func (smn.getNodeHash(), s.peekData());

                if (--max <= 0)
                    return false;
            }
            return true;
        });
}

void
SHAMap::visitDifferences(SHAMap* have,
                         std::function<bool (SHAMapAbstractNode&)> func) const
{
    // Visit every node in this SHAMap that is not present
    // in the specified SHAMap

    if (root_->getNodeHash ().isZero ())
        return;

    if (have && (root_->getNodeHash () == have->root_->getNodeHash ()))
        return;

    if (root_->isLeaf ())
    {
        auto leaf = std::static_pointer_cast<SHAMapTreeNode>(root_);
        if (!have || !have->hasLeafNode(leaf->peekItem()->getTag(), leaf->getNodeHash()))
            func (*root_);

        return;
    }
    // contains unexplored non-matching inner node entries
    using StackEntry = std::pair <SHAMapInnerNode*, SHAMapNodeID>;
    std::stack <StackEntry, std::vector<StackEntry>> stack;

    stack.push ({static_cast<SHAMapInnerNode*>(root_.get()), SHAMapNodeID{}});

    while (!stack.empty())
    {
        SHAMapInnerNode* node;
        SHAMapNodeID nodeID;
        std::tie (node, nodeID) = stack.top ();
        stack.pop ();

        // 1) Add this node to the pack
        if (!func (*node))
            return;

        // 2) push non-matching child inner nodes
        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                uint256 const& childHash = node->getChildHash (i);
                SHAMapNodeID childID = nodeID.getChildNodeID (i);
                auto next = descendThrow(node, i);

                if (next->isInner ())
                {
                    if (!have || !have->hasInnerNode(childID, childHash))
                        stack.push ({static_cast<SHAMapInnerNode*>(next), childID});
                }
                else if (!have || !have->hasLeafNode(
                         static_cast<SHAMapTreeNode*>(next)->peekItem()->getTag(),
                         childHash))
                {
                    if (! func (*next))
                        return;
                }
            }
        }
    }
}

} // divvy
