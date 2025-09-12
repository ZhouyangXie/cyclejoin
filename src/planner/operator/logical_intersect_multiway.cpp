#include "planner/operator/logical_intersect_multiway.h"

#include <memory>

namespace kuzu {
namespace planner {

std::string LogicalIntersectMultiway::getExpressionsForPrinting() const {
    std::string s = "";
    for (auto [probe, builds] : probeToBuildExpression) {
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
    for (auto [build_node, _] : buildToProbeExpression) {
        auto outGroupPos = schema->createGroup();
        schema->insertToGroupAndScope(build_node, outGroupPos);
        for (auto& probe_node : buildToProbeExpression[build_node]) {
            auto build_schema = children[probeToChildIdx[probe_node]]->getSchema();
            auto pos = build_schema->getGroupPos(build_node->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != build_node->getUniqueName()) {
                    schema->insertToGroupAndScope(expression, outGroupPos);
                }
            }
        }
    }
}

void LogicalIntersectMultiway::computeFlatSchema() {
    schema = children[0]->getSchema()->copy();
    for (auto [build_node, _] : buildToProbeExpression) {
        schema->insertToGroupAndScope(build_node, 0);
        for (auto& probe_node : buildToProbeExpression[build_node]) {
            auto build_schema = children[probeToChildIdx[probe_node]]->getSchema();
            auto pos = build_schema->getGroupPos(build_node->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != build_node->getUniqueName()) {
                    schema->insertToGroupAndScope(expression, 0);
                }
            }
        }
    }
}

std::unique_ptr<LogicalOperator> LogicalIntersectMultiway::copy() {
    binder::expression_map<std::shared_ptr<LogicalOperator>> probeToBuildChildren_copy;
    for (auto [k, v] : probeToBuildChildren) {
        probeToBuildChildren_copy[k] = v->copy();
    }
    return std::make_unique<LogicalIntersectMultiway>(probeToBuildExpression, children[0]->copy(),
        std::move(probeToBuildChildren), cardinality);
}

} // namespace planner
} // namespace kuzu