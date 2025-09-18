#pragma once
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <algorithm>

#include "binder/query/reading_clause/bound_join_hint.h"
#include "planner/planner.h"
#include "query_graph.h"

namespace kuzu {

namespace binder {

struct QueryGraphSimple {
    using Node = std::string;
    using Nodes = std::set<Node>;
    using DenseSubgraph = std::pair<Nodes, Nodes>;

    QueryGraphSimple() = default;

    bool isEmpty() const { return nodes.size() == 0; }

    void addNode(Node new_node) {
        KU_ASSERT(!nodes.contains(new_node));
        nodes.insert(new_node);
        edges[new_node] = {};
    };

    void addEdge(Node src, Node dst) {
        KU_ASSERT(nodes.contains(src));
        KU_ASSERT(nodes.contains(dst));
        edges[src].insert(dst);
        edges[dst].insert(src);
    }

    void removeNode(Node node) {
        nodes.erase(node);
        auto& neighbors = edges[node];
        for (auto& neighbor : neighbors) {
            edges[neighbor].erase(node);
        }
        edges.erase(node);
    }

    void removeEdge(Node src, Node dst) {
        edges[src].erase(dst);
        edges[dst].erase(src);
    }

    size_t numOfNode() const { return nodes.size(); }

    size_t numOfEdge() const {
        size_t count = 0;
        for (auto [_, neighbors] : edges) {
            count += edges.size();
        }
        KU_ASSERT(count % 2 == 0);
        return count / 2;
    }

    QueryGraphSimple copy() const {
        QueryGraphSimple copy;
        for (auto& node : nodes) {
            copy.addNode(node);
        }
        for (auto& [src, dsts] : edges) {
            for (auto& dst : dsts) {
                copy.addEdge(src, dst);
            }
        }
        return copy;
    }

    std::vector<QueryGraphSimple> getConnectedParts() {
        std::vector<QueryGraphSimple> subgraphs;
        Nodes visited;

        for (auto& start_node : nodes) {
            if (visited.contains(start_node)) {
                continue;
            }
            QueryGraphSimple subgraph;
            subgraph.addNode(start_node);
            // find all connected nodes
            bool updated = false;
            do {
                updated = false;
                for (auto& src : subgraph.nodes) {
                    for (auto& dst : edges[src]) {
                        if (!subgraph.nodes.contains(dst)) {
                            updated = true;
                            subgraph.addNode(dst);
                            visited.insert(dst);
                        }
                    }
                }
            } while (updated);
            // find all edges
            for (auto& node : subgraph.nodes) {
                for (auto& neighbor : edges[node]) {
                    if (subgraph.nodes.contains(neighbor)) {
                        subgraph.addEdge(node, neighbor);
                    }
                }
            }
            subgraphs.push_back(std::move(subgraph));
        }

        return subgraphs;
    }

    DenseSubgraph findOneMinimalDense() {
        for (auto& u0 : nodes) {
            for (auto& u1 : nodes) {
                if (u0 >= u1) {
                    continue;
                }
                // at least two common neighbors
                Nodes common_neighbors;
                for (auto& neighbor : edges[u0]) {
                    if (neighbor == u1) {
                        continue;
                    }
                    if (edges[u1].contains(neighbor)) {
                        common_neighbors.insert(neighbor);
                    }
                }
                if (common_neighbors.size() < 2) {
                    continue;
                }
                // u0 and u1 are connected if the edges are removed
                auto new_graph = this->copy();
                for (auto& neighbor : common_neighbors) {
                    new_graph.removeEdge(u0, neighbor);
                    new_graph.removeEdge(u1, neighbor);
                }
                auto parts = new_graph.getConnectedParts();
                bool still_connected = false;
                for (auto& part : parts) {
                    if (part.nodes.contains(u0) && part.nodes.contains(u1)) {
                        still_connected = true;
                        break;
                    }
                }
                if (still_connected) {
                    return {{u0, u1}, common_neighbors};
                }
            }
        }
        return {{}, {}};
    }

    Nodes findCommonNeighbors(Nodes probes) {
        // find nodes that are connected by at least 2 probes
        // and each probe has at least two connections
        Nodes builds;
        for (auto& build : nodes) {
            if (probes.contains(build)) {
                continue;
            }
            size_t shared_by = 0;
            for (auto& probe : probes) {
                if (edges[probe].contains(build)) {
                    shared_by += 1;
                }
            }
            if (shared_by >= 2) {
                builds.insert(build);
            }
        }
        for (auto& probe : probes) {
            size_t shared_by = 0;
            for (auto& build : builds) {
                if (edges[probe].contains(build)) {
                    shared_by += 1;
                }
            }
            if (shared_by < 2) {
                return {};
            }
        }
        return builds;
    }

    std::tuple<QueryGraphSimple, std::vector<QueryGraphSimple>, std::vector<QueryGraphSimple>>
    findOneMaximalDense() {
        DenseSubgraph dense = findOneMinimalDense();
        if (dense.first.size() == 0) {
            return {{}, {}, {}};
        }
        bool expanded = false;
        do {
            expanded = false;
            for (auto& new_probe : nodes) {
                if (dense.first.contains(new_probe)) {
                    continue;
                }
                if (dense.second.contains(new_probe)) {
                    continue;
                }
                Nodes new_probes = dense.first;
                new_probes.insert(new_probe);
                auto new_builds = findCommonNeighbors(new_probes);
                if (new_builds.size() == 0) {
                    continue;
                }
                // if the probes are connected after removing the edges
                auto new_graph = this->copy();
                for (auto& probe : new_probes) {
                    for (auto& build : new_builds) {
                        if (new_graph.edges[probe].contains(build)) {
                            new_graph.removeEdge(probe, build);
                        }
                    }
                }
                auto parts = new_graph.getConnectedParts();
                bool still_connected = false;
                for (auto& part : parts) {
                    still_connected = true;
                    for (auto& new_probe : new_probes) {
                        if (!part.nodes.contains(new_probe)) {
                            still_connected = false;
                            break;
                        }
                    }
                    if (still_connected) {
                        break;
                    }
                }
                if (!still_connected) {
                    continue;
                }
                expanded = true;
                dense = {new_probes, new_builds};
                break;
            }
        } while (expanded);

        QueryGraphSimple rest_graph = this->copy();
        std::vector<QueryGraphSimple> build_graphs;
        for (auto& probe : dense.first) {
            QueryGraphSimple build_graph;
            build_graph.addNode(probe);
            for (auto& build : edges[probe]) {
                if (dense.second.contains(build)) {
                    build_graph.addNode(build);
                    build_graph.addEdge(probe, build);
                    rest_graph.removeEdge(probe, build);
                }
            }
            build_graphs.push_back(std::move(build_graph));
        }

        auto parts = rest_graph.getConnectedParts();
        std::vector<QueryGraphSimple> remaining_graphs;
        size_t probe_graph_idx = -1u;
        for (size_t i = 0; i < parts.size(); i++) {
            if (parts[i].nodes.contains(*dense.first.begin())) {
                for (auto& probe_node : dense.first) {
                    KU_ASSERT(parts[i].nodes.contains(probe_node));
                }
                probe_graph_idx = i;
            } else {
                remaining_graphs.push_back(std::move(parts[i]));
            }
        }
        KU_ASSERT(probe_graph_idx != -1u);

        return {std::move(parts[probe_graph_idx]), std::move(build_graphs),
            std::move(remaining_graphs)};
    }

    static QueryGraphSimple fromQueryGraph(const QueryGraph& graph) {
        QueryGraphSimple simple;
        for (size_t i = 0; i < graph.getNumQueryNodes(); i++) {
            simple.addNode(graph.getQueryNode(i)->getUniqueName());
        }
        for (size_t i = 0; i < graph.getNumQueryRels(); i++) {
            auto rel = graph.getQueryRel(i);
            simple.addEdge(rel->getSrcNodeName(), rel->getDstNodeName());
        }
        return simple;
    }

    static std::shared_ptr<RelExpression> getRelExpressionFromRefGraph(const Node& src,
        const Node& dst, const QueryGraph& ref_graph) {
        for (size_t i = 0; i < ref_graph.getNumQueryRels(); i++) {
            auto rel = ref_graph.getQueryRel(i);
            if ((rel->getSrcNodeName() == src && rel->getDstNodeName() == dst) ||
                (rel->getSrcNodeName() == dst && rel->getDstNodeName() == src)) {
                return rel;
            }
        }
        KU_UNREACHABLE;
    }

    std::tuple<QueryGraph, planner::QueryGraphPlanningInfo> toQueryGraphAndInfo(
        const QueryGraph& ref_graph, const planner::QueryGraphPlanningInfo& ref_info) {
        QueryGraph graph;
        for (auto& node : nodes) {
            graph.addQueryNode(ref_graph.getQueryNode(node));
        }
        for (auto& node : nodes) {
            for (auto& neighbor : edges[node]) {
                if (node > neighbor) {
                    graph.addQueryRel(getRelExpressionFromRefGraph(node, neighbor, ref_graph));
                }
            }
        }
        planner::QueryGraphPlanningInfo info = ref_info;
        info.predicates.clear();
        // filter relevant info
        for (auto predicate : ref_info.predicates) {
            if (graph.canProjectExpression(predicate)) {
                info.predicates.push_back(predicate);
            }
        }
        return {graph, info};
    }

    std::tuple<std::shared_ptr<binder::BoundJoinHintNode>, std::shared_ptr<Expression>,
        std::vector<std::shared_ptr<Expression>>>
    toHintTree(const QueryGraph& ref_graph) {
        // find the center of the start
        Node center;
        for (auto& node : nodes) {
            if (edges[node].size() > 1) {
                center = node;
                KU_ASSERT(edges[node].size() == nodes.size() - 1);
            } else {
                KU_ASSERT(edges[node].size() == 1);
            }
        }
        KU_ASSERT(!center.empty());

        auto hint_tree_node = std::make_shared<binder::BoundJoinHintNode>();
        auto center_exp = ref_graph.getQueryNode(center);
        std::vector<std::shared_ptr<Expression>> build_node_exps;
        hint_tree_node->nodeOrRel = center_exp;
        for (auto& neighbor : edges[center]) {
            auto rel = getRelExpressionFromRefGraph(center, neighbor, ref_graph);
            auto leaf = std::make_shared<binder::BoundJoinHintNode>();
            leaf->nodeOrRel = rel;
            auto new_node = std::make_shared<binder::BoundJoinHintNode>();
            new_node->addChild(hint_tree_node);
            new_node->addChild(leaf);
            hint_tree_node = new_node;
            build_node_exps.push_back(ref_graph.getQueryNode(neighbor)->getInternalID());
        }
        for (auto& neighbor : edges[center]) {
            auto node = ref_graph.getQueryNode(neighbor);
            auto leaf = std::make_shared<binder::BoundJoinHintNode>();
            leaf->nodeOrRel = node;
            auto new_node = std::make_shared<binder::BoundJoinHintNode>();
            new_node->addChild(hint_tree_node);
            new_node->addChild(leaf);
            hint_tree_node = new_node;
        }
        // TODO: make sure the order in build_nodes is the same as the key order in HT!
        std::reverse(build_node_exps.begin(), build_node_exps.end());
        return {hint_tree_node, center_exp->getInternalID(), build_node_exps};
    }

    Nodes nodes;
    std::map<Node, Nodes> edges;
};

} // namespace binder
} // namespace kuzu
