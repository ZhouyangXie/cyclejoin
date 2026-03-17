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

f_group_pos_set LogicalIntersectMultiway::getGroupsPosToFlattenOnProbeSide(){
    f_group_pos_set result;
    for (auto& keyNodeID : leftExpressions) {
        result.insert(children[0]->getSchema()->getGroupPos(*keyNodeID));
    }
    return result;
}

f_group_pos_set LogicalIntersectMultiway::getGroupsPosToFlattenOnBuildSide(uint32_t buildIdx){
    return {children[buildIdx + 1]->getSchema()->getGroupPos(*leftExpressions[buildIdx])};
}


void LogicalIntersectMultiway::computeFactorizedSchema() {
    schema = children[0]->getSchema()->copy();
    for (auto [rightNode, leftNodes] : rightToLeftExpression) {
        auto outGroupPos = schema->createGroup();
        schema->insertToGroupAndScopeMayRepeat(rightNode, outGroupPos);
        for (auto leftNode : leftNodes) {
            auto build_schema = leftToBuildChildren[leftNode]->getSchema();
            auto pos = build_schema->getGroupPos(rightNode->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != rightNode->getUniqueName()) {
                    schema->insertToGroupAndScopeMayRepeat(expression, outGroupPos);
                }
            }
        }
    }
    // insert all the intermediate expressions in the build schemas to the new schema
    for (auto leftNode: leftExpressions){
        auto build_schema = leftToBuildChildren[leftNode]->getSchema();
        for(auto group_pos: build_schema->getGroupsPosInScope()){
            auto group = build_schema->getGroup(group_pos);
            auto new_group_pos = schema->createGroup();
            auto new_group = schema->getGroup(new_group_pos);
            if(group->isFlat()){
                new_group->setFlat();
            }
            for(auto exp: group->getExpressions()){
                if(!schema->isExpressionInScope(*exp)){
                    schema->insertToGroupAndScope(exp, new_group_pos);
                }
            }
        }
    }
}

void LogicalIntersectMultiway::computeFlatSchema() {
    schema = children[0]->getSchema()->copy();
    for (auto [rightNode, leftNodes] : rightToLeftExpression) {
        schema->insertToGroupAndScopeMayRepeat(rightNode, 0);
        for (auto& leftNode : leftNodes) {
            auto build_schema = leftToBuildChildren[leftNode]->getSchema();
            auto pos = build_schema->getGroupPos(rightNode->getUniqueName());
            auto group = build_schema->getGroup(pos);
            KU_ASSERT(!group->isFlat());
            for (auto& expression : group->getExpressions()) {
                if (expression->getUniqueName() != rightNode->getUniqueName()) {
                    schema->insertToGroupAndScopeMayRepeat(expression, 0);
                }
            }
        }
    }
    // insert all the intermediate expressions in the build schemas to the new schema
    for (auto leftNode: leftExpressions){
        auto build_schema = leftToBuildChildren[leftNode]->getSchema();
        for(auto group_pos: build_schema->getGroupsPosInScope()){
            auto group = build_schema->getGroup(group_pos);
            for(auto exp: group->getExpressions()){
                if(!schema->isExpressionInScope(*exp)){
                    schema->insertToGroupAndScope(exp, 0);
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