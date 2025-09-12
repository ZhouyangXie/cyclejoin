#pragma once
#include <memory>
#include <unordered_map>

#include "binder/expression/expression.h"
#include "planner/operator/logical_operator.h"

namespace kuzu {
namespace planner {

class LogicalIntersectMultiway final : public LogicalOperator {
    static constexpr LogicalOperatorType type_ = LogicalOperatorType::INTERSECT_MULTIWAY;

public:
    LogicalIntersectMultiway(
        binder::expression_map<binder::expression_vector> probeToBuildExpression,
        std::shared_ptr<LogicalOperator> probeChild,
        binder::expression_map<std::shared_ptr<LogicalOperator>> probeToBuildChildren,
        common::cardinality_t cardinality = 0)
        : LogicalOperator{type_, std::move(probeChild)},
          probeToBuildExpression{probeToBuildExpression},
          probeToBuildChildren{probeToBuildChildren} {
        KU_ASSERT(probeToBuildChildren.size() == probeToBuildExpression.size());
        KU_ASSERT(probeToBuildExpression.size() >= 2);
        for (auto [probe_node, build_nodes] : probeToBuildExpression) {
            children.push_back(probeToBuildChildren[probe_node]);
            probeToChildIdx[probe_node] = children.size() - 1;
            KU_ASSERT(build_nodes.size() >= 2);
            // check that each build child has 1 + #probe groups
            auto build_schema = children[probeToChildIdx[probe_node]]->getSchema();
            KU_ASSERT(
                build_schema->getNumGroups() == 1 + probeToBuildExpression[probe_node].size());
            KU_ASSERT(build_schema->getGroupPos(probe_node->getUniqueName()) == 0);
            KU_ASSERT(build_schema->getGroup(probe_node->getUniqueName())->isFlat());
            for (size_t j = 0; j < build_nodes.size(); j++) {
                auto& build_node = build_nodes[j];
                KU_ASSERT(build_schema->getGroupPos(build_node->getUniqueName()) == 1 + j);
                KU_ASSERT(!build_schema->getGroup(build_node->getUniqueName())->isFlat());
                KU_ASSERT(!probeToBuildExpression.contains(build_node));
                if (buildToProbeExpression.contains(build_node)) {
                    buildToProbeExpression[build_node].insert(probe_node);
                } else {
                    buildToProbeExpression.insert({build_node, {probe_node}});
                }
            }
        }
        KU_ASSERT(buildToProbeExpression.size() >= 2);
        for (auto [build_node, probe_nodes] : buildToProbeExpression) {
            KU_ASSERT(probe_nodes.size() >= 2);
        }
        this->cardinality = cardinality;
    }

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;
    std::string getExpressionsForPrinting() const override;
    std::unique_ptr<LogicalOperator> copy() override;

private:
    binder::expression_map<binder::expression_vector> probeToBuildExpression;
    binder::expression_map<std::shared_ptr<LogicalOperator>> probeToBuildChildren;
    binder::expression_map<binder::expression_set> buildToProbeExpression;
    binder::expression_map<size_t> probeToChildIdx;
};

} // namespace planner
} // namespace kuzu
