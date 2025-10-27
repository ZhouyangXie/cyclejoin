#pragma once

#include "binder/expression/rel_expression.h"
#include "planner/operator/logical_operator.h"
#include "storage/predicate/column_predicate.h"

namespace kuzu {
namespace planner {


struct LogicalSharedExtendPrintInfo : OPPrintInfo {
    std::shared_ptr<binder::NodeExpression> boundNode;
    std::vector<std::shared_ptr<binder::NodeExpression>> nbrNodes;
    std::vector<std::shared_ptr<binder::RelExpression>> rels;
    std::vector<common::ExtendDirection> directions;

    LogicalSharedExtendPrintInfo(
        std::shared_ptr<binder::NodeExpression> boundNode,
        std::vector<std::shared_ptr<binder::NodeExpression>> nbrNodes,
        std::vector<std::shared_ptr<binder::RelExpression>> rels,
        std::vector<common::ExtendDirection> directions
    ): boundNode{std::move(boundNode)},
        nbrNodes{std::move(nbrNodes)},
        rels{std::move(rels)},
        directions{std::move(directions)} {}

    std::string toString() const override {
        std::string result = "";
        for(size_t i = 0; i < nbrNodes.size(); i++){
            switch (directions[i]) {
                case common::ExtendDirection::FWD: {
                    result += "(" + boundNode->toString() + ")-[" + rels[i]->toString() + "]->(" +
                        nbrNodes[i]->toString() + ")";
                } break;
                case common::ExtendDirection::BWD: {
                    result += "(" + nbrNodes[i]->toString() + ")-[" + rels[i]->toString() + "]->(" +
                        boundNode->toString() + ")";
                } break;
                case common::ExtendDirection::BOTH: {
                    result += "(" + boundNode->toString() + ")-[" + rels[i]->toString() + "]-(" +
                        nbrNodes[i]->toString() + ")";
                } break;
                default: {
                    KU_UNREACHABLE;
                }
            }
            result += ",";
        }
        return result;
    }
};


class LogicalSharedExtend final : public LogicalOperator {
    static constexpr LogicalOperatorType type_ = LogicalOperatorType::SHARED_EXTEND;

public:
    LogicalSharedExtend(
        std::shared_ptr<binder::NodeExpression> boundNode,
        std::vector<std::shared_ptr<binder::NodeExpression>> nbrNodes,
        std::vector<std::shared_ptr<binder::RelExpression>> rels,
        std::vector<common::ExtendDirection> directions,
        std::vector<binder::expression_vector> properties,
        std::vector<bool> flatScan,
        std::shared_ptr<LogicalOperator> child,
        common::cardinality_t cardinality = 0
    ):
    LogicalOperator{type_, std::move(child)},
    boundNode{std::move(boundNode)},
    nbrNodes{std::move(nbrNodes)},
    rels{std::move(rels)},
    directions{std::move(directions)},
    properties{std::move(properties)},
    flatScan{std::move(flatScan)}
    {
        KU_ASSERT(getNumberOfSharing() == this->rels.size());
        KU_ASSERT(getNumberOfSharing() == this->directions.size());
        KU_ASSERT(getNumberOfSharing() == this->properties.size());
        KU_ASSERT(getNumberOfSharing() == this->flatScan.size());
        this->cardinality = cardinality;
        scanNbrID.resize(getNumberOfSharing(), true);
        properties.resize(getNumberOfSharing());
        propertyPredicates.resize(getNumberOfSharing());
    }

    f_group_pos_set getGroupsPosToFlatten() { return f_group_pos_set{}; }
    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    size_t getNumberOfSharing() const {return nbrNodes.size();}
    std::shared_ptr<binder::NodeExpression> getBoundNode() const { return boundNode; }
    std::shared_ptr<binder::NodeExpression> getNbrNode(size_t i) const { return nbrNodes[i]; }
    std::shared_ptr<binder::RelExpression> getRel(size_t i) const { return rels[i]; }
    bool isRecursive(size_t i) const { return rels[i]->isRecursive(); }
    common::ExtendDirection getDirection(size_t i) const { return directions[i]; }
    binder::expression_vector getProperties(size_t i) const { return properties[i]; }
    void setPropertyPredicates(size_t i, std::vector<storage::ColumnPredicateSet> predicates) {
        propertyPredicates[i] = std::move(predicates);
    }
    const std::vector<storage::ColumnPredicateSet>& getPropertyPredicates(size_t i) const {
        return propertyPredicates[i];
    }
    void setScanNbrID(size_t i, bool scanNbrID_) { scanNbrID[i] = scanNbrID_; }
    bool shouldScanNbrID(size_t i) const { return scanNbrID[i]; }


    std::string getExpressionsForPrinting() const override;
    std::unique_ptr<LogicalOperator> copy() override;

    std::unique_ptr<OPPrintInfo> getPrintInfo() const override {
        return std::make_unique<LogicalSharedExtendPrintInfo>(boundNode, nbrNodes, rels, directions);
    }

    std::vector<bool> getFlatScan() const { return this->flatScan;}

private:
    std::shared_ptr<binder::NodeExpression> boundNode;
    std::vector<std::shared_ptr<binder::NodeExpression>> nbrNodes;
    std::vector<std::shared_ptr<binder::RelExpression>> rels;
    std::vector<common::ExtendDirection> directions;
    std::vector<binder::expression_vector> properties;
    std::vector<bool> flatScan;

    std::vector<bool> scanNbrID;
    std::vector<std::vector<storage::ColumnPredicateSet>> propertyPredicates;
};

} // namespace planner
} // namespace kuzu
