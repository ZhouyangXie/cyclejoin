#include "binder/binder.h"
#include "binder/expression/property_expression.h"
#include "binder/expression_binder.h"
#include "common/enums/extend_direction_util.h"
#include "main/client_context.h"
#include "planner/operator/extend/logical_shared_extend.h"
#include "processor/operator/scan/scan_rel_table.h"
#include "processor/operator/scan/shared_scan_rel_table.h"
#include "processor/plan_mapper.h"
#include "storage/storage_manager.h"


using namespace kuzu::binder;
using namespace kuzu::common;
using namespace kuzu::planner;
using namespace kuzu::storage;
using namespace kuzu::catalog;

namespace kuzu {
namespace processor {

static ScanRelTableInfo getRelTableScanInfo(const TableCatalogEntry& tableEntry,
    RelDataDirection direction, RelTable* relTable, bool shouldScanNbrID,
    const expression_vector& properties, const std::vector<ColumnPredicateSet>& columnPredicates,
    main::ClientContext* clientContext) {
    std::vector<ColumnPredicateSet> columnPredicateSets = copyVector(columnPredicates);
    if (!columnPredicateSets.empty()) {
        // Since we insert a nbr column. We need to pad an empty nbr column predicate set.
        columnPredicateSets.insert(columnPredicateSets.begin(), ColumnPredicateSet());
    }
    auto tableInfo = ScanRelTableInfo(relTable, std::move(columnPredicateSets), direction);
    // We always should scan nbrID from relTable. This is not a property in the schema label, so
    // cannot be bound to a column in the front-end.
    auto nbrColumnID = shouldScanNbrID ? NBR_ID_COLUMN_ID : INVALID_COLUMN_ID;
    tableInfo.addColumnInfo(nbrColumnID, ColumnCaster(LogicalType::INTERNAL_ID()));
    auto binder = Binder(clientContext);
    auto expressionBinder = ExpressionBinder(&binder, clientContext);
    for (auto& expr : properties) {
        auto& property = expr->constCast<PropertyExpression>();
        if (property.hasProperty(tableEntry.getTableID())) {
            auto propertyName = property.getPropertyName();
            auto& columnType = tableEntry.getProperty(propertyName).getType();
            auto columnCaster = ColumnCaster(columnType.copy());
            if (property.getDataType() != columnType) {
                auto columnExpr = std::make_shared<PropertyExpression>(property);
                columnExpr->dataType = columnType.copy();
                columnCaster.setCastExpr(
                    expressionBinder.forceCast(columnExpr, property.getDataType()));
            }
            tableInfo.addColumnInfo(tableEntry.getColumnID(propertyName), std::move(columnCaster));
        } else {
            tableInfo.addColumnInfo(INVALID_COLUMN_ID, ColumnCaster(LogicalType::ANY()));
        }
    }
    return tableInfo;
}

static bool scanSingleRelTable(const RelExpression& rel, const NodeExpression& boundNode,
    ExtendDirection extendDirection) {
    return !rel.isMultiLabeled() && !boundNode.isMultiLabeled() &&
           extendDirection != ExtendDirection::BOTH;
}

std::unique_ptr<PhysicalOperator> PlanMapper::mapSharedExtend(const LogicalOperator* logicalOperator) {
    auto extend = logicalOperator->constPtrCast<LogicalSharedExtend>();
    auto outFSchema = extend->getSchema();
    auto inFSchema = extend->getChild(0)->getSchema();
    auto prevOperator = mapOperator(logicalOperator->getChild(0).get());

    auto boundNode = extend->getBoundNode();
    auto inNodeIDPos = getDataPos(*boundNode->getInternalID(), *inFSchema);
    auto storageManager = StorageManager::Get(*clientContext);

    std::vector<ScanOpInfo> scanInfos;
    std::vector<ScanRelTableInfo> scanRelInfos;
    std::vector<ScanRelTablePrintInfo> printInfos;

    for(size_t i = 0; i < extend->getNumberOfSharing(); i++){
        auto nbrNode = extend->getNbrNode(i);
        auto rel = extend->getRel(i);
        auto extendDirection = extend->getDirection(i);
        std::vector<DataPos> outVectorsPos;
        auto outNodeIDPos = getDataPos(*nbrNode->getInternalID(), *outFSchema);
        outVectorsPos.push_back(outNodeIDPos);
        for (auto& expression : extend->getProperties(i)) {
            outVectorsPos.push_back(getDataPos(*expression, *outFSchema));
        }
        auto scanInfo = ScanOpInfo(inNodeIDPos, outVectorsPos);
        std::vector<std::string> tableNames;
        for (auto entry : rel->getEntries()) {
            tableNames.push_back(entry->getName());
        }
        KU_ASSERT(scanSingleRelTable(*rel, *boundNode, extendDirection));
        KU_ASSERT(rel->getNumEntries() == 1);
        auto entry = rel->getEntry(0)->ptrCast<RelGroupCatalogEntry>();
        auto relDataDirection = ExtendDirectionUtil::getRelDataDirection(extendDirection);
        auto entryInfo = entry->getSingleRelEntryInfo();
        auto relTable = storageManager->getTable(entryInfo.oid)->ptrCast<RelTable>();
        auto scanRelInfo = getRelTableScanInfo(*entry, relDataDirection, relTable, extend->shouldScanNbrID(i),
                extend->getProperties(i), extend->getPropertyPredicates(i), clientContext);
        auto printInfo = ScanRelTablePrintInfo(tableNames, extend->getProperties(i), boundNode, rel, nbrNode, extendDirection, rel->getVariableName());

        scanInfos.push_back(std::move(scanInfo));
        scanRelInfos.push_back(std::move(scanRelInfo));
        printInfos.push_back(std::move(printInfo));
    }
    return std::make_unique<SharedScanRelTable>(
        scanInfos, scanRelInfos,
        std::move(prevOperator), getOperatorID(),
        std::make_unique<SharedScanRelTablePrintInfo>(printInfos)
    );
}

} // namespace processor
} // namespace kuzu
