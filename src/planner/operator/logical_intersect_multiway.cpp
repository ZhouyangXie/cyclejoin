#include "planner/operator/logical_intersect_multiway.h"

#include <memory>

namespace kuzu {
namespace planner {

std::string LogicalIntersectMultiway::getExpressionsForPrinting() const {
    std::string s = "";
    for (auto [probe, builds] : leftToRightExpression) {
        s += ("(" + probe->toString() + ")->{");
        for (auto build : builds) {
            s += (build->toString() + ",");
        }
        s += "}";
    }
    return s;
}

void LogicalIntersectMultiway::computeFactorizedSchema() {
    schema = children[0]->getSchema()->copy();
    for (auto [rightNode, leftNodes] : rightToLeftExpression) {
        auto outGroupPos = schema->createGroup();
        schema->insertToGroupAndScope(rightNode, outGroupPos);
        for (auto leftNode : leftNodes) {
            auto build_schema = leftToBuildChildren[leftNode]->getSchema();
            auto pos = build_schema->getGroupPos(rightNode->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != rightNode->getUniqueName()) {
                    schema->insertToGroupAndScope(expression, outGroupPos);
                }
            }
        }
    }
}

void LogicalIntersectMultiway::computeFlatSchema() {
    schema = children[0]->getSchema()->copy();
    for (auto [rightNode, leftNodes] : rightToLeftExpression) {
        schema->insertToGroupAndScope(rightNode, 0);
        for (auto& leftNode : leftNodes) {
            auto build_schema = leftToBuildChildren[leftNode]->getSchema();
            auto pos = build_schema->getGroupPos(rightNode->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != rightNode->getUniqueName()) {
                    schema->insertToGroupAndScope(expression, 0);
                }
            }
        }
    }
}

std::unique_ptr<LogicalOperator> LogicalIntersectMultiway::copy() {
    binder::expression_map<std::shared_ptr<LogicalOperator>> probeToBuildChildren_copy;
    for (auto [k, v] : leftToBuildChildren) {
        probeToBuildChildren_copy[k] = v->copy();
    }
    return std::make_unique<LogicalIntersectMultiway>(leftToRightExpression, children[0]->copy(),
        std::move(probeToBuildChildren_copy), cardinality);
}

} // namespace planner
} // namespace kuzu