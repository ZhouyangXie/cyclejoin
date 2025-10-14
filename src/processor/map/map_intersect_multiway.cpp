#include <memory>

#include "binder/expression/expression_util.h"
#include "planner/operator/logical_intersect_multiway.h"
#include "processor/operator/hash_join/hash_join_build.h"
#include "processor/operator/intersect/intersect_multiway.h"
#include "processor/operator/intersect/intersect_multiway_build.h"
#include "processor/plan_mapper.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::binder;
using namespace kuzu::planner;
using namespace kuzu::common;

namespace kuzu {
namespace processor {

std::unique_ptr<PhysicalOperator> PlanMapper::mapIntersectMultiway(
    const LogicalOperator* logicalOperator) {
    auto op = logicalOperator->constPtrCast<LogicalIntersectMultiway>();
    auto outSchema = op->getSchema();
    auto left_size = op->getNumLeftNode();
    auto right_size = op->getNumRightNode();

    std::vector<DataPos> keyDataPos(left_size);
    for (size_t i = 0; i < left_size; i++) {
        keyDataPos[i] = DataPos(outSchema->getExpressionPos(*(op->leftExpressions[i])));
    }
    std::vector<DataPos> outputDataPos(right_size);
    for (size_t j = 0; j < right_size; j++) {
        outputDataPos[j] = DataPos(outSchema->getExpressionPos(*(op->rightExpressions[j])));
    }

    std::vector<std::shared_ptr<HashJoinSharedState>> sharedStates;
    std::vector<std::unique_ptr<PhysicalOperator>> buildChildren;
    std::vector<std::vector<ft_col_offset_t>> keyOffsetInTuple(left_size);
    std::vector<std::vector<std::vector<DataPos>>> payloadsDataPos(left_size);
    std::vector<std::vector<std::vector<size_t>>> payloadsColRange(left_size);

    for (size_t i = 0; i < left_size; i++) {
        // build the hash tables, almost the same as Intersect, except the physical operator is
        // HashJoinBuild
        auto buildChild = op->getChild(i + 1);
        auto buildSchema = buildChild->getSchema();
        auto buildPrevOperator = mapOperator(buildChild.get());
        auto leftNode = op->leftExpressions[i];
        // compute the schema and cols of the HT
        binder::expression_vector payloadExpressions;
        binder::expression_map<size_t> payloadColumnIdx;
        size_t col_idx = 1;
        for (auto exp: op->leftToRightExpression.at(leftNode)) {
        // for (auto exp : buildSchema->getExpressionsInScope()) {
            // TODO: getExpressionsInScope is not ordered
            // payloadExpressions should be constructed in a certain order
            KU_ASSERT(buildSchema->isExpressionInScope(*exp));
            if (exp->getUniqueName() == leftNode->getUniqueName()) {
                continue;
            }
            payloadExpressions.push_back(exp);
            payloadColumnIdx[exp] = col_idx;
            col_idx++;
        }
        // because we currently do not support payloads in HT, check that only probeKey and intersectKey are present
        KU_ASSERT(payloadExpressions.size() == op->leftToRightExpression.at(leftNode).size());
        // init the HT
        binder::expression_vector keys = {leftNode};
        auto buildInfo = createHashBuildInfo(*buildSchema, keys, payloadExpressions);
        auto globalHashTable =
            std::make_unique<JoinHashTable>(*storage::MemoryManager::Get(*clientContext),
                ExpressionUtil::getDataTypes(keys), buildInfo.tableSchema.copy());
        auto sharedState = std::make_shared<HashJoinSharedState>(std::move(globalHashTable));
        sharedStates.push_back(sharedState);
        auto printInfo = std::make_unique<HashJoinBuildPrintInfo>(keys, payloadExpressions);
        auto build = std::make_unique<IntersectMultiwayBuild>(sharedState, std::move(buildInfo), std::move(buildPrevOperator), getOperatorID(),
            std::move(printInfo));
        build->setDescriptor(std::make_unique<ResultSetDescriptor>(buildSchema));
        buildChildren.push_back(std::move(build));
        // update the output schema related info
        keyOffsetInTuple[i].resize(right_size, -1u);
        for (size_t j = 0; j < right_size; j++) {
            if (!op->connectivity[i][j]) {
                continue;
            }
            keyOffsetInTuple[i][j] = sharedState->getHashTable()->getTableSchema()->getColOffset(
                payloadColumnIdx[op->rightExpressions[j]]);
        }
        // TODO: currently we do not support populating the payloads, so we can leave them empty for
        // now
        // TODO: it seems that the payloads can be organized other way around
        payloadsDataPos[i].resize(right_size);
        payloadsColRange[i].resize(right_size);
    }

    auto info = std::make_shared<IntersectMultiwayDataInfo>(keyDataPos, outputDataPos,
        op->connectivity, keyOffsetInTuple, payloadsDataPos, payloadsColRange);
    auto probeChild = mapOperator(op->getChild(0).get());
    auto printInfo = std::make_unique<IntersecMultiwaytPrintInfo>(op->leftExpressions,
        op->rightExpressions, op->connectivity);
    auto intersectMultiway = std::make_unique<IntersectMultiway>(info, sharedStates,
        std::move(probeChild), getOperatorID(), std::move(printInfo));
    for (auto& child : buildChildren) {
        intersectMultiway->addChild(std::move(child));
    }
    return intersectMultiway;
}

} // namespace processor
} // namespace kuzu
