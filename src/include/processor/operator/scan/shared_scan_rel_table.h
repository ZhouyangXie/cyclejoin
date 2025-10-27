#pragma once

#include "processor/operator/scan/scan_rel_table.h"
#include "processor/operator/scan/scan_table.h"
#include "storage/table/rel_table.h"
#include "processor/operator/scan/scan_rel_table.h"


namespace kuzu {
namespace storage {
class MemoryManager;
}
namespace processor {

struct SharedScanRelTablePrintInfo final : OPPrintInfo {
    std::vector<ScanRelTablePrintInfo> infos;

    explicit SharedScanRelTablePrintInfo(std::vector<ScanRelTablePrintInfo> infos):  infos{std::move(infos)} {}

    std::string toString() const override;

    std::unique_ptr<OPPrintInfo> copy() const override {
        return std::unique_ptr<SharedScanRelTablePrintInfo>(new SharedScanRelTablePrintInfo(*this));
    }

    SharedScanRelTablePrintInfo(const SharedScanRelTablePrintInfo& other): infos{other.infos} {}
};


class SharedScanRelTable final : public PhysicalOperator {
    static constexpr PhysicalOperatorType type_ = PhysicalOperatorType::SHARED_SCAN_REL_TABLE;

public:
    SharedScanRelTable(
        std::vector<ScanOpInfo> opInfos,
        std::vector<ScanRelTableInfo> tableInfos,
        std::vector<bool> flattenScans,
        std::unique_ptr<PhysicalOperator> child, physical_op_id id,
        std::unique_ptr<OPPrintInfo> printInfo
    ): PhysicalOperator{type_, std::move(child), id, std::move(printInfo)},
    tableInfos{std::move(tableInfos)}, opInfos{std::move(opInfos)} {
        KU_ASSERT(this->tableInfos.size() == this->opInfos.size());
        this->flattenScans = flattenScans;
    }

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;

    bool getNextTuplesInternal(ExecutionContext* context) override;

    size_t getNumberOfSharing() const { return tableInfos.size();}

    std::unique_ptr<PhysicalOperator> copy() override {
        return std::make_unique<SharedScanRelTable>(opInfos, tableInfos, flattenScans, children[0]->copy(), id, printInfo->copy());
    }

private:
    std::vector<ScanRelTableInfo> tableInfos;
    std::vector<std::unique_ptr<storage::RelTableScanState>> scanStates;
    std::vector<bool> stateFinished;
    std::vector<bool> flattenScans;
    std::vector<common::sel_t> flattenScanIndex;
    std::vector<common::sel_t> flattenScanSize;

    std::vector<ScanOpInfo> opInfos;
    std::vector<std::vector<common::ValueVector*>> outVectors;
};

} // namespace processor
} // namespace kuzu
