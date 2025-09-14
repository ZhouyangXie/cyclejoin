#pragma once
#include <memory>

#include "binder/expression/expression.h"
#include "planner/operator/logical_operator.h"

namespace kuzu {
namespace planner {

class LogicalIntersectMultiway final : public LogicalOperator {
    static constexpr LogicalOperatorType type_ = LogicalOperatorType::INTERSECT_MULTIWAY;

public:
    LogicalIntersectMultiway(binder::expression_map<binder::expression_vector>
                                 leftToRightExpression, // note: the keys in the shared HT will be
                                                        // stored in the same order
        std::shared_ptr<LogicalOperator> probeChild,
        binder::expression_map<std::shared_ptr<LogicalOperator>> leftToBuildChildren,
        common::cardinality_t cardinality = 0)
        : LogicalOperator{type_, std::move(probeChild)},
          leftToRightExpression{std::move(leftToRightExpression)},
          leftToBuildChildren{std::move(leftToBuildChildren)} {
        KU_ASSERT(leftToRightExpression.size() == leftToBuildChildren.size());
        KU_ASSERT(leftToRightExpression.size() >= 2);
        for (auto [probe_node, build_nodes] : leftToRightExpression) {
            leftExpressions.push_back(probe_node);
            leftExpressionIdx[probe_node] = leftExpressions.size();
            KU_ASSERT(build_nodes.size() >= 2);
            connectivity.emplace_back(getNumRightNode(), false);

            auto build_child = leftToBuildChildren[probe_node];
            // check that each build child has 1 + #probe groups
            auto build_schema = build_child->getSchema();
            KU_ASSERT(build_schema->getNumGroups() == 1 + build_nodes.size());
            KU_ASSERT(build_schema->getGroupPos(probe_node->getUniqueName()) == 0);
            KU_ASSERT(build_schema->getGroup(probe_node->getUniqueName())->isFlat());
            for (size_t j = 0; j < build_nodes.size(); j++) {
                auto& build_node = build_nodes[j];
                KU_ASSERT(build_schema->getGroupPos(build_node->getUniqueName()) == 1 + j);
                KU_ASSERT(!build_schema->getGroup(build_node->getUniqueName())->isFlat());
                KU_ASSERT(!leftToRightExpression.contains(build_node));
                if (!rightExpressionIdx.contains(build_node)) {
                    rightExpressions.push_back(build_node);
                    rightExpressionIdx[build_node] = rightExpressions.size();
                }
                connectivity[leftExpressionIdx[probe_node]][rightExpressionIdx[build_node]] = true;
            }
            children.push_back(build_child);
        }
        for (size_t j = 0; j < getNumRightNode(); j++) {
            rightToLeftExpression[rightExpressions[j]] = {};
            for (size_t i = 0; i < getNumLeftNode(); i++) {
                if (connectivity[i][j]) {
                    rightToLeftExpression[rightExpressions[j]].push_back(leftExpressions[i]);
                }
            }
            KU_ASSERT(rightToLeftExpression[rightExpressions[j]].size() >= 2);
        }
        this->cardinality = cardinality;
    }

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;
    std::string getExpressionsForPrinting() const override;
    std::unique_ptr<LogicalOperator> copy() override;

    size_t getNumLeftNode() const { return leftExpressions.size(); }

    size_t getNumRightNode() const { return rightExpressions.size(); }

public:
    // initialized by constructor args
    binder::expression_map<binder::expression_vector> leftToRightExpression;
    binder::expression_map<std::shared_ptr<LogicalOperator>> leftToBuildChildren;

    // induced
    binder::expression_vector leftExpressions;
    binder::expression_map<size_t> leftExpressionIdx;
    binder::expression_vector rightExpressions;
    binder::expression_map<size_t> rightExpressionIdx;
    binder::expression_map<binder::expression_vector> rightToLeftExpression;
    std::vector<std::vector<bool>> connectivity;
};

} // namespace planner
} // namespace kuzu
