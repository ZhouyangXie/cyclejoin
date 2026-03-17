#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <random>

#include "binder/query/reading_clause/bound_join_hint.h"
#include "planner/planner.h"
#include "query_graph.h"

namespace kuzu {

namespace binder {

namespace simple {
using Node = std::string;
using Nodes = std::set<Node>;
using Path = std::vector<Node>;
using Cycle = std::vector<Node>;

const size_t BuildPathLengthLimit = 3; // \rho_{max}

struct Graph {
    Nodes nodes;
    std::map<Node, Nodes> edges;

    Graph() = default;

    bool isEmpty() const { return nodes.size() == 0; }

    const Nodes& getNeighbors(const Node& src) const {
        auto it = edges.find(src);
        KU_ASSERT_UNCONDITIONAL(it != edges.end());
        return it->second;
    }

    bool isConnectedAndDifferent(const Graph other) const {
        bool is_connected = false;
        bool has_new_node = false;
        for (auto& v: other.nodes){
            if (!nodes.contains(v)) {
                has_new_node = true;
            }
            else {
                is_connected = true;
            }
        }
        return is_connected && has_new_node;
    }

    Graph merge(const Graph other) const {
        Graph merged = this->copy();
        for (const Node& u : other.nodes) {
            merged.addNodeMayExists(u);
        }
        for (const Node& u : other.nodes) {
            for (const Node& v : other.getNeighbors(u)) {
                merged.addEdge(u, v);
            }
        }
        return merged;
    }

    void mergeFrom(const Path path) {
        for(size_t k = 0; k < path.size(); k++){
            addNodeMayExists(path[k]);
        }
        for(size_t k = 1; k < path.size(); k++){
            addEdgeMayExists(path[k-1], path[k]);
        }
    }

    void addNodeMayExists(const Node& new_node) {
        if (!nodes.contains(new_node)) {
            nodes.insert(new_node);
            edges[new_node] = {};
        }
    }

    void addNode(const Node& new_node) {
        KU_ASSERT_UNCONDITIONAL(!nodes.contains(new_node));
        nodes.insert(new_node);
        edges[new_node] = {};
    };

    void addEdgeMayExists(const Node& src, const Node& dst) {
        if (!edges[src].contains(dst)) {
            edges[src].insert(dst);
            edges[dst].insert(src);
        }
    }

    void addEdge(const Node& src, const Node& dst) {
        KU_ASSERT_UNCONDITIONAL(nodes.contains(src));
        KU_ASSERT_UNCONDITIONAL(nodes.contains(dst));
        edges[src].insert(dst);
        edges[dst].insert(src);
    }

    void removeNode(const Node& node) {
        nodes.erase(node);
        auto& neighbors = edges[node];
        for (auto& neighbor : neighbors) {
            edges[neighbor].erase(node);
        }
        edges.erase(node);
    }

    void removeEdge(const Node& src, const Node& dst) {
        edges[src].erase(dst);
        edges[dst].erase(src);
    }

    void removeEdgesFrom(const Graph& other) {
        for (const Node& u : other.nodes) {
            if (this->nodes.contains(u)) {
                for (const Node& v : other.getNeighbors(u)) {
                    if (this->edges[u].contains(v)) {
                        removeEdge(u, v);
                    }
                }
            }
        }
    }

    size_t numOfNode() const { return nodes.size(); }

    size_t numOfEdge() const {
        size_t count = 0;
        for (auto [_, neighbors] : edges) {
            count += edges.size();
        }
        KU_ASSERT_UNCONDITIONAL(count % 2 == 0);
        return count / 2;
    }

    Graph copy() const {
        Graph copy;
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

    bool operator==(const Graph& other) {
        if (nodes.size() != other.nodes.size()) {
            return false;
        }
        for (auto& edge : edges) {
            auto it = other.edges.find(edge.first);
            if (it == other.edges.end() || it->second != edge.second) {
                return false;
            }
        }
        return true;
    }

    std::string toString() {
        std::string s;
        for (auto& node : nodes) {
            s += "Node:" + node;
            s += ",Nbr:{";
            for (auto& nbr : edges[node]) {
                s += nbr + ",";
            }
            s += "},";
        }
        return s;
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
                KU_ASSERT_UNCONDITIONAL(edges[node].size() == nodes.size() - 1);
            } else {
                KU_ASSERT_UNCONDITIONAL(edges[node].size() == 1);
            }
        }
        KU_ASSERT_UNCONDITIONAL(!center.empty());

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
        std::reverse(build_node_exps.begin(), build_node_exps.end());
        return {hint_tree_node, center_exp->getInternalID(), build_node_exps};
    }
};

Graph pathToGraph(const Path& path) {
    Graph graph;
    if (path.size() > 0) {
        graph.addNode(path[0]);
        for (size_t k = 0; k < path.size() - 1; k++) {
            graph.addNode(path[k + 1]);
            graph.addEdge(path[k], path[k + 1]);
        }
    }
    return graph;
}

void extendPath(Path& path, const Graph& graph, const Nodes& visited) {
    const Node& end = path.back();
    if (graph.getNeighbors(end).size() != 2) {
        if (visited.contains(end) || end == path[0]) {
            path.clear();
        }
        return;
    }

    Node next;
    for (const Node& nbr : graph.getNeighbors(end)) {
        if (nbr == path[path.size() - 2]) {
            continue;
        }
        next = nbr;
    }

    path.push_back(next);
    extendPath(path, graph, visited);
}

std::vector<Path> findBasicCycleIntersection(const Graph& graph_) {
    Graph graph = graph_.copy();
    do {
        Nodes to_remove;
        for (const Node& u : graph.nodes) {
            if (graph.getNeighbors(u).size() < 2) {
                to_remove.insert(u);
            }
        }
        if (to_remove.size() == 0) {
            break;
        }
        for (const Node& u : to_remove) {
            graph.removeNode(u);
        }
    } while (true);

    std::vector<Path> paths;
    Nodes visited;
    for (const Node& start : graph.nodes) {
        if (graph.getNeighbors(start).size() <= 2) {
            continue;
        }
        for (const Node& nbr : graph.getNeighbors(start)) {
            Path path = {start, nbr};
            extendPath(path, graph, visited);
            if (path.size() > 0) {
                paths.push_back(path);
            }
        }
        visited.insert(start);
    }

    return paths;
}

std::vector<Graph> getConnectedSubgraphs(const Graph& graph) {
    std::vector<Graph> subgraphs;
    Nodes visited;
    for (auto& start_node : graph.nodes) {
        if (visited.contains(start_node)) {
            continue;
        }
        Graph subgraph;
        subgraph.addNode(start_node);
        // find all connected nodes
        bool updated = false;
        do {
            updated = false;
            for (auto& src : subgraph.nodes) {
                for (auto& dst : graph.getNeighbors(src)) {
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
            for (auto& neighbor : graph.getNeighbors(node)) {
                if (subgraph.nodes.contains(neighbor)) {
                    subgraph.addEdge(node, neighbor);
                }
            }
        }
        subgraphs.push_back(subgraph);
    }

    return subgraphs;
}

Path findCyclePathIntersection(const Cycle& cycle_a, const Cycle& cycle_b) {
    size_t i = 0, j = 0;
    bool found = false;
    for (i = 0; i < cycle_a.size(); i++) {
        for (j = 0; j < cycle_b.size(); j++) {
            if (cycle_a[i] == cycle_b[j]) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        return {};
    }
    Path intersect = {cycle_a[i]};
    // search forward
    for (size_t step = 1; step < std::min(cycle_a.size(), cycle_b.size()); step++) {
        if (cycle_a[(i + step) % cycle_a.size()] == cycle_b[(j + step) % cycle_b.size()]) {
            intersect.push_back(cycle_a[(i + step) % cycle_a.size()]);
        } else {
            break;
        }
    }
    for (size_t step = 1; step < std::min(cycle_a.size(), cycle_b.size()); step++) {
        if (cycle_a[(i + cycle_a.size() - step) % cycle_a.size()] ==
            cycle_b[(j - step + cycle_b.size()) % cycle_b.size()]) {
            intersect.push_back(cycle_a[(i + cycle_a.size() - step) % cycle_a.size()]);
        } else {
            break;
        }
    }
    if (intersect.size() >= 2 && *intersect.begin() > intersect.back()) {
        std::reverse(intersect.begin(), intersect.end());
    }
    return intersect;
}

std::vector<std::vector<Path>> findCycleJoin(const Nodes& V_probe,
    const std::vector<Path>& basic_paths) {
    std::map<Node, std::map<Node, Path>> building_graphs;
    for (auto& path : basic_paths) {
        const Node &head = path[0], tail = path.back();
        // not via the probe side
        bool contains_V_probe = false;
        for (size_t i = 1; i < path.size() - 1; i++) {
            if (V_probe.contains(path[i])) {
                contains_V_probe = true;
                break;
            }
        }
        if (contains_V_probe) {
            continue;
        }

        if (V_probe.contains(head)) {
            if (V_probe.contains(tail)) {
                if (path.size() < 2 * BuildPathLengthLimit && path.size() > 2) {
                    // randomly choose a node to break, here we choose the middle
                    Path from_head, from_tail;
                    for (size_t i = 0; i <= path.size() / 2; i++) {
                        from_head.push_back(path[i]);
                    }
                    for (size_t i = path.size() - 1; i >= path.size() / 2; i--) {
                        from_tail.push_back(path[i]);
                    }
                    const Node& intersect = path[path.size() / 2];
                    if (!building_graphs.contains(head)) {
                        building_graphs[head] = {};
                    }
                    building_graphs[head][intersect] = std::move(from_head);
                    if (!building_graphs.contains(tail)) {
                        building_graphs[tail] = {};
                    }
                    building_graphs[tail][intersect] = std::move(from_tail);
                }
            } else {
                if (path.size() > BuildPathLengthLimit) {
                    continue;
                }
                if (!building_graphs.contains(head)) {
                    building_graphs[head] = {};
                }
                if (!building_graphs[head].contains(tail) ||
                    building_graphs[head][tail].size() > path.size()) {
                    building_graphs[head][tail] = path;
                }
            }
        } else if (V_probe.contains(tail)) {
            if (path.size() > BuildPathLengthLimit) {
                continue;
            }
            if (!building_graphs.contains(tail)) {
                building_graphs[tail] = {};
            }
            if (!building_graphs[tail].contains(head) ||
                building_graphs[tail][head].size() > path.size()) {
                building_graphs[tail][head] = path;
                // reverse the path to make the probing node at front
                std::reverse(building_graphs[tail][head].begin(),
                    building_graphs[tail][head].end());
            }
        }
    }

    bool removal = false;
    Nodes probe_remove_nodes;
    Nodes intersect_remove_nodes;
    do {
        removal = false;
        probe_remove_nodes.clear();
        intersect_remove_nodes.clear();
        std::map<Node, size_t> intersect_node_counts;

        for (auto& [v_probe, intersect_nodes] : building_graphs) {
            if (intersect_nodes.size() < 2) {
                probe_remove_nodes.insert(v_probe);
                removal = true;
            }
            for (auto& [v_intersect, _] : intersect_nodes) {
                if (intersect_node_counts.contains(v_intersect)) {
                    intersect_node_counts[v_intersect]++;
                } else {
                    intersect_node_counts[v_intersect] = 1;
                }
            }
        }

        for (const Node& remove_node : probe_remove_nodes) {
            building_graphs.erase(remove_node);
        }

        for (auto& [intersect_node, count] : intersect_node_counts) {
            if (count < 2) {
                intersect_remove_nodes.insert(intersect_node);
            }
        }

        for (const Node& remove_node : intersect_remove_nodes) {
            for (auto& [v_probe, intersect_nodes] : building_graphs) {
                if (intersect_nodes.contains(remove_node)) {
                    intersect_nodes.erase(remove_node);
                    removal = true;
                }
            }
        }

    } while (removal);

    std::vector<std::vector<Path>> trees;
    for (auto& [v_probe, intersect_nodes] : building_graphs) {
        trees.push_back({});
        for (auto& [_, path] : building_graphs[v_probe]) {
            trees.back().push_back(path);
        }
    }

    return trees;
}

bool compareCycleJoin(const Nodes& a, const Nodes& b, const std::vector<Path>& basic_paths) {
    auto trees_a = findCycleJoin(a, basic_paths);
    auto trees_b = findCycleJoin(b, basic_paths);

    if (trees_a.size() == 0) {
        return false;
    }
    if (trees_b.size() == 0) {
        return true;
    }

    size_t s_a = 0, s_b = 0;
    for (auto& paths : trees_a) {
        s_a += (paths.size() - 1);
    }
    for (auto& paths : trees_b) {
        s_b += (paths.size() - 1);
    }
    if (s_a > s_b && s_a > 0) {
        return true;
    }

    size_t t_a = 0, t_b = 0;
    for (auto& paths : trees_a) {
        for (auto& path : paths) {
            t_a += (path.size() - 2);
        }
    }
    for (auto& paths : trees_b) {
        for (auto& path : paths) {
            t_b += (path.size() - 2);
        }
    }
    if (t_a < t_b) {
        return true;
    }

    if (a.size() < b.size()) {
        return true;
    }

    return false;
}

std::pair<Graph, std::vector<std::vector<Path>>> findBestCycleJoin(const Graph& Q, bool shuffle_baisc_paths) {
    std::vector<Path> P_basic = findBasicCycleIntersection(Q);

    if (P_basic.size() == 0) {
        return {Graph(), {}};
    }

    if(shuffle_baisc_paths){
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(P_basic.begin(), P_basic.end(), g);
    }

    std::vector<Graph> P;
    for (auto& basic : P_basic) {
        P.push_back(pathToGraph(basic));
    }

    Graph G_probe;
    do {
        bool no_improvement = true;
        for (size_t i = 0; i < P.size(); i++) {
            if (compareCycleJoin(P[i].nodes, G_probe.nodes, P_basic)) {
                G_probe = P[i];
                no_improvement = false;
            }
        }
        if (no_improvement) {
            break;
        }
        std::vector<Graph> P_next;
        for (auto& basic : P_basic) {
            Graph G_basic = pathToGraph(basic);
            if (G_probe.isConnectedAndDifferent(G_basic)) {
                P_next.push_back(G_probe.merge(G_basic));
            }
        }
        P = std::move(P_next);
    } while (P.size() > 0);

    for (const Node& u : G_probe.nodes) {
        for (const Node& v : Q.getNeighbors(u)) {
            if (G_probe.nodes.contains(v)) {
                G_probe.addEdge(u, v);
            }
        }
    }

    auto paths = findCycleJoin(G_probe.nodes, P_basic);
    return {G_probe, paths};
}

Graph fromQueryGraph(const QueryGraph& graph) {
    Graph simple;
    for (size_t i = 0; i < graph.getNumQueryNodes(); i++) {
        simple.addNode(graph.getQueryNode(i)->getUniqueName());
    }
    for (size_t i = 0; i < graph.getNumQueryRels(); i++) {
        auto rel = graph.getQueryRel(i);
        simple.addEdge(rel->getSrcNodeName(), rel->getDstNodeName());
    }
    return simple;
}


std::shared_ptr<binder::BoundJoinHintNode> pathToHintTree(const Path & path, const QueryGraph& ref_graph){
    auto left_most_node = std::make_shared<binder::BoundJoinHintNode>();
    left_most_node->nodeOrRel = ref_graph.getQueryNode(path[1]);
    auto root_node = left_most_node;

    for(size_t i = 2; i < path.size(); i++){
        auto edge_node = std::make_shared<binder::BoundJoinHintNode>();
        edge_node->nodeOrRel = Graph::getRelExpressionFromRefGraph(path[i - 1], path[i], ref_graph);
        auto edge_join_node = std::make_shared<binder::BoundJoinHintNode>();
        edge_join_node->addChild(root_node);
        edge_join_node->addChild(edge_node);

        auto right_node = std::make_shared<binder::BoundJoinHintNode>();
        right_node->nodeOrRel = ref_graph.getQueryNode(path[i]);
        auto right_join_node = std::make_shared<binder::BoundJoinHintNode>();
        right_join_node->addChild(edge_join_node);
        right_join_node->addChild(right_node);

        root_node = right_join_node;
    }

    return root_node;
}

std::tuple<std::shared_ptr<binder::BoundJoinHintNode>, std::shared_ptr<Expression>,
    std::vector<std::shared_ptr<Expression>>>
pathsToHintTree(const std::vector<Path> & paths,  const QueryGraph& ref_graph) {
    Node center = paths[0][0];
    auto hint_tree_root = std::make_shared<binder::BoundJoinHintNode>();
    auto center_exp = ref_graph.getQueryNode(center);
    std::vector<std::shared_ptr<Expression>> intersect_node_exps;

    hint_tree_root->nodeOrRel = center_exp;

    // build the consecutive extend ops
    for (auto & path : paths) {
        const Node & neighbor = path[1];
        auto rel = Graph::getRelExpressionFromRefGraph(center, neighbor, ref_graph);
        auto leaf = std::make_shared<binder::BoundJoinHintNode>();
        leaf->nodeOrRel = rel;
        auto new_node = std::make_shared<binder::BoundJoinHintNode>();
        new_node->addChild(hint_tree_root);
        new_node->addChild(leaf);
        hint_tree_root = new_node;
        intersect_node_exps.push_back(ref_graph.getQueryNode(path.back())->getInternalID());
    }

    for (auto& path : paths) {
        auto new_node = std::make_shared<binder::BoundJoinHintNode>();
        new_node->addChild(hint_tree_root);
        new_node->addChild(pathToHintTree(path, ref_graph));
        hint_tree_root = new_node;
    }

    std::reverse(intersect_node_exps.begin(), intersect_node_exps.end());

    return {hint_tree_root, center_exp->getInternalID(), intersect_node_exps};
}

} // namespace simple
} // namespace binder
} // namespace kuzu
