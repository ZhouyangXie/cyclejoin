#include "planner/operator/extend/logical_shared_extend.h"


using namespace kuzu::common;

namespace kuzu {
namespace planner {


std::string LogicalSharedExtend::getExpressionsForPrinting() const {
    return LogicalSharedExtendPrintInfo(boundNode, nbrNodes, rels, directions).toString();
}

void LogicalSharedExtend::computeFactorizedSchema() {
    copyChildSchema(0);
    const auto boundGroupPos = schema->getGroupPos(*boundNode->getInternalID());
    if (!schema->getGroup(boundGroupPos)->isFlat()) {
        schema->flattenGroup(boundGroupPos);
    }
    for(size_t i = 0; i < getNumberOfSharing(); i++){
        uint32_t nbrGroupPos = schema->createGroup();
        schema->insertToGroupAndScope(nbrNodes[i]->getInternalID(), nbrGroupPos);
        for (auto& property : properties[i]) {
            schema->insertToGroupAndScope(property, nbrGroupPos);
        }
        if (rels[i]->hasDirectionExpr()) {
            schema->insertToGroupAndScope(rels[i]->getDirectionExpr(), nbrGroupPos);
        }
    }
}

void LogicalSharedExtend::computeFlatSchema() {
    copyChildSchema(0);
    for(size_t i = 0; i < getNumberOfSharing(); i++){
        schema->insertToGroupAndScope(nbrNodes[i]->getInternalID(), 0);
        for (auto& property : properties[i]) {
            schema->insertToGroupAndScope(property, 0);
        }
        if (rels[i]->hasDirectionExpr()) {
            schema->insertToGroupAndScope(rels[i]->getDirectionExpr(), 0);
        }
    }
}

std::unique_ptr<LogicalOperator> LogicalSharedExtend::copy() {
    auto extend = std::make_unique<LogicalSharedExtend>(
        boundNode, nbrNodes, rels, directions, properties, children[0]->copy(), cardinality
    );
    for(size_t i = 0; i < getNumberOfSharing(); i++){
        extend->setPropertyPredicates(i, copyVector(propertyPredicates[i]));
        extend->scanNbrID[i] = scanNbrID[i];
    }
    return extend;
}


} // namespace planner
} // namespace kuzu
