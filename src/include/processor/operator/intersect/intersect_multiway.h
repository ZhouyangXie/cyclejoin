#pragma once

#include <memory>

#include "processor/operator/hash_join/hash_join_build.h"
#include "processor/operator/physical_operator.h"

namespace kuzu {
namespace processor {

struct IntersecMultiwaytPrintInfo final : OPPrintInfo {
    std::vector<std::shared_ptr<binder::Expression>> left_nodes;
    std::vector<std::shared_ptr<binder::Expression>> right_nodes;
    std::vector<std::vector<bool>> connectivity;

    explicit IntersecMultiwaytPrintInfo(std::vector<std::shared_ptr<binder::Expression>> left_nodes,
        std::vector<std::shared_ptr<binder::Expression>> right_nodes,
        std::vector<std::vector<bool>> connectivity)
        : left_nodes{std::move(left_nodes)}, right_nodes{std::move(right_nodes)},
          connectivity{std::move(connectivity)} {}

    std::string toString() const override;
    std::unique_ptr<OPPrintInfo> copy() const override {
        return std::unique_ptr<IntersecMultiwaytPrintInfo>(new IntersecMultiwaytPrintInfo(*this));
    }

private:
    IntersecMultiwaytPrintInfo(const IntersecMultiwaytPrintInfo& other)
        : OPPrintInfo{other}, left_nodes{other.left_nodes}, right_nodes{other.right_nodes},
          connectivity{other.connectivity} {}
};

struct IntersectMultiwayDataInfo {
    // DataPos of each left (probe) side key
    std::vector<DataPos> keyDataPos;
    // DataPos of each right (build) side key
    std::vector<DataPos> outputDataPos;
    // Whether the i-th probe node is connected to the j-th build node
    std::vector<std::vector<bool>> connectivity;
    // Offset of each key in the HT tuples
    std::vector<std::vector<ft_col_offset_t>> keyOffsetInTuple;
    // DataPos of [i] the i-th left node [j] the j-th right node [k] the k-th expression.
    std::vector<std::vector<std::vector<DataPos>>> payloadsDataPos;
    // Column range of each payload in the corresponding hash table, same indexing as above
    std::vector<std::vector<std::vector<size_t>>> payloadsColRange;
    // Indices of the build nodes of each probe node
    std::vector<std::vector<size_t>> left2right_idx;
    // Indices of the probe nodes of each build node
    std::vector<std::vector<size_t>> right2left_idx;

    IntersectMultiwayDataInfo(std::vector<DataPos> keyDataPos, std::vector<DataPos> outputDataPos,
        std::vector<std::vector<bool>> connectivity,
        std::vector<std::vector<ft_col_offset_t>> keyOffsetInTuple,
        std::vector<std::vector<std::vector<DataPos>>> payloadsDataPos,
        std::vector<std::vector<std::vector<size_t>>> payloadsColRange)
        : keyDataPos{std::move(keyDataPos)}, outputDataPos{std::move(outputDataPos)},
          connectivity{std::move(connectivity)}, keyOffsetInTuple{std::move(keyOffsetInTuple)},
          payloadsDataPos{std::move(payloadsDataPos)},
          payloadsColRange{std::move(payloadsColRange)} {
        size_t numLeftNodes = this->keyDataPos.size();
        KU_ASSERT(numLeftNodes >= 2);
        size_t numRightNodes = this->outputDataPos.size();
        KU_ASSERT(numRightNodes >= 2);
        KU_ASSERT(this->connectivity.size() == numLeftNodes);
        KU_ASSERT(this->payloadsDataPos.size() == numLeftNodes);
        KU_ASSERT(this->payloadsColRange.size() == numLeftNodes);
        KU_ASSERT(this->keyOffsetInTuple.size() == numLeftNodes);
        for (size_t i = 0; i < numLeftNodes; i++) {
            auto& i_payloadsDataPos = this->payloadsDataPos[i];
            auto& i_payloadsColRange = this->payloadsColRange[i];
            auto& i_connectivity = this->connectivity[i];
            auto& i_keyOffsetInTuple = this->keyOffsetInTuple[i];
            KU_ASSERT(i_connectivity.size() == numRightNodes);
            KU_ASSERT(i_payloadsDataPos.size() == numRightNodes);
            KU_ASSERT(i_payloadsColRange.size() == numRightNodes);
            KU_ASSERT(i_keyOffsetInTuple.size() == numRightNodes);
            size_t i_non_empty_count = 0;
            left2right_idx.emplace_back();
            for (size_t j = 0; j < numRightNodes; j++) {
                KU_ASSERT(i_payloadsDataPos[j].size() == i_payloadsColRange[j].size());
                if (i_connectivity[j]) {
                    KU_ASSERT(i_keyOffsetInTuple[j] != -1u);
                    // TODO: currently we do not support populating the payloads
                    KU_ASSERT(i_payloadsDataPos[j].size() == 0);
                    for (auto& pos : i_payloadsDataPos[j]) {
                        KU_ASSERT(pos.isValid());
                    }
                    for (auto col : i_payloadsColRange[j]) {
                        // the first two cols in HT are probe keys and hashes.
                        KU_ASSERT(col > 2);
                    }
                    left2right_idx[i].push_back(j);
                    i_non_empty_count++;
                } else {
                    KU_ASSERT(i_keyOffsetInTuple[j] == -1u);
                    KU_ASSERT(i_payloadsDataPos.size() == 0);
                    KU_ASSERT(i_payloadsColRange.size() == 0);
                }
            }
            KU_ASSERT(i_non_empty_count >= 2);
        }
        for (size_t j = 0; j < numRightNodes; j++) {
            right2left_idx.emplace_back();
            size_t j_non_empty_count = 0;
            for (size_t i = 0; i < numLeftNodes; i++) {
                if (this->connectivity[i][j]) {
                    right2left_idx[j].push_back(i);
                    j_non_empty_count++;
                }
            }
            KU_ASSERT(j_non_empty_count >= 2);
        }
    }

    bool hasConnection(size_t i, size_t j) { return connectivity[i][j]; }
};

struct IntersectionLoopState;

class IntersectMultiway : public PhysicalOperator {
    static constexpr PhysicalOperatorType type_ = PhysicalOperatorType::INTERSECT_MULTIWAY;

public:
    IntersectMultiway(std::shared_ptr<IntersectMultiwayDataInfo> intersectDataInfo,
        std::vector<std::shared_ptr<HashJoinSharedState>> sharedHTs,
        std::unique_ptr<PhysicalOperator> probeChild, uint32_t id,
        std::unique_ptr<OPPrintInfo> printInfo)
        : PhysicalOperator{type_, std::move(probeChild), id, std::move(printInfo)},
          info{std::move(intersectDataInfo)}, sharedHTs{std::move(sharedHTs)} {
        KU_ASSERT(children[0]->getOperatorType() == PhysicalOperatorType::FLATTEN);
        KU_ASSERT(this->sharedHTs.size() == numLeftNodes());
        // TODO: check that the number of columns, flatness of each sharedHT is correct
        for (size_t i = 0; i < numLeftNodes(); i++) {
            auto ht_schema = this->sharedHTs[i]->getHashTable()->getTableSchema();
            // TODO: currently not considering other payload attributes
            KU_ASSERT(ht_schema->getNumColumns() == info->left2right_idx.size() + 3);
            KU_ASSERT(ht_schema->getColumn(0)->isFlat());
            for (size_t j = 0; j < info->left2right_idx.size(); j++) {
                KU_ASSERT(!ht_schema->getColumn(1 + j)->isFlat());
            }
            KU_ASSERT(ht_schema->getColumn(info->left2right_idx.size() + 1)->isFlat()); // hash
            KU_ASSERT(ht_schema->getColumn(info->left2right_idx.size() + 2)
                    ->isFlat()); // chaining pointer
        }
    }

    uint32_t numLeftNodes() { return info->keyDataPos.size(); }
    uint32_t numRightNodes() { return info->outputDataPos.size(); }

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;

    bool getNextTuplesInternal(ExecutionContext* context) override;

    std::unique_ptr<PhysicalOperator> copy() override {
        return std::make_unique<IntersectMultiway>(info, sharedHTs, children[0]->copy(), id,
            printInfo->copy());
    }

private:
    bool probeHTs();
    bool multiway_intersect_on_sorted_tuples(size_t rightSideNodeIdx);

private:
    std::shared_ptr<IntersectMultiwayDataInfo> info;
    // these hash tables have multiple payload groups
    std::vector<std::shared_ptr<HashJoinSharedState>> sharedHTs;

    // size() == numLeftNodes()
    std::vector<std::shared_ptr<common::ValueVector>> probeKeyVectors;
    // size() == numRightNodes()
    std::vector<std::shared_ptr<common::ValueVector>> outKeyVectors;
    // same sizes as intersectDataInfo.payloadsDataPos and intersectDataInfo.payloadsColRange
    std::vector<std::vector<std::vector<common::ValueVector*>>> payloadVectorsToScanInto;

    // pointer to the probed tuples in each hash table
    // the first 2 dimensions are of size numLeftNodes() x numRightNodes(), third dimension is the
    // number of probed tuples
    std::vector<std::vector<std::vector<common::overflow_value_t>>> probedIds;
    // sel vectors on each of probedIds
    std::vector<std::vector<std::vector<std::shared_ptr<common::SelectionVector>>>>
        intersectSelVectors;

    // this operator outputs the Cartesian product of intersections
    // this state stores the loop state
    friend IntersectionLoopState;
    std::shared_ptr<IntersectionLoopState> loopState;
};

struct IntersectionLoopState {
    //  cursor, size() == op->numRightNodes()
    std::vector<size_t> tuple_cursors;
    IntersectMultiway* op;
    bool finished;
    std::vector<size_t> smallesLeftSide;

    explicit IntersectionLoopState(IntersectMultiway* op) : op{op}, finished{true} {
        tuple_cursors.resize(op->numRightNodes(), 0);
        smallesLeftSide.resize(op->numRightNodes(), 0);
        // Choose the one with least number of tuples to iterate
        for (size_t j = 0; j < op->numRightNodes(); j++) {
            for (auto i : op->info->right2left_idx[j]) {
                if (op->intersectSelVectors[i][j].size() <
                    op->intersectSelVectors[smallesLeftSide[j]][j].size()) {
                    smallesLeftSide[j] = i;
                }
            }
        }
    }

    void reset();

    bool cursorFinished(size_t j) const {
        return cursorAt(j) >=
               op->probedIds[op->info->right2left_idx[j][smallesLeftSide[j]]][j].size();
    };

    bool hasFinished() const { return finished; }

    void gotoNext();

    size_t cursorAt(size_t j) const { return tuple_cursors[j]; }
};

} // namespace processor

} // namespace kuzu