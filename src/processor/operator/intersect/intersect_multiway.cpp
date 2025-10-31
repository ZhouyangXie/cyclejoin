#include "processor/operator/intersect/intersect_multiway.h"
#include <algorithm>
#include <memory>
#include <string>
#include "function/hash/hash_functions.h"
#include "processor/result/factorized_table.h"


using namespace kuzu::common;

namespace kuzu {
namespace processor {

std::string IntersecMultiwaytPrintInfo::toString() const{
    std::string s = "";
    for(size_t i = 0; i < left_nodes.size(); i++){
        s += ("(" + std::to_string(i) + ")->{");
        for(size_t j = 0; j < right_nodes.size(); j++){
            if(connectivity[i][j]){
                s += std::to_string(j) + ",";
            }
        }
        s += "},";
    }
    return s;
}

void IntersectionLoopState::reset(){
    std::fill(tuple_cursors.begin(), tuple_cursors.end(), 0);
    finished = false;
}

void IntersectionLoopState::gotoNext() {
    KU_ASSERT(!hasFinished());
    for(size_t j = 0; j < tuple_cursors.size(); j++){
        tuple_cursors[j]++;
        if(cursorFinished(j)){
            if(j < tuple_cursors.size() - 1){
                tuple_cursors[j] = 0;
            }
            else {
                finished = true;
            }
        }
        else{
            break;
        }
    }
}


void IntersectMultiway::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* /* context */) {
    probeKeyVectors.clear();
    for (size_t i = 0; i < numLeftNodes(); i++) {
        probeKeyVectors.push_back(resultSet->getValueVector(info->keyDataPos[i]));
    }
    outKeyVectors.clear();
    for (size_t j = 0; j < numRightNodes(); j++) {
        outKeyVectors.push_back(resultSet->getValueVector(info->outputDataPos[j]));
    }
    payloadVectorsToScanInto.clear();
    intersectSelVectors.clear();
    probedIds.clear();
    for (size_t i = 0; i < numLeftNodes(); i++) {
        payloadVectorsToScanInto.emplace_back(numRightNodes());
        intersectSelVectors.emplace_back(numRightNodes());
        probedIds.emplace_back(numRightNodes());
        for (size_t j = 0; j < numRightNodes(); j++) {
            if (info->hasConnection(i, j)) {
                for (auto& pos : info->payloadsDataPos[i][j]) {
                    // Currently we do not support populating the payloads
                    KU_UNREACHABLE;
                    payloadVectorsToScanInto[i][j].push_back(resultSet->getValueVector(pos).get());
                }
            }
        }
    }
    loopState = std::make_shared<IntersectionLoopState>(this);
    loopState->finished = true;

    card_prod.resize(numRightNodes());
    dynamic_order.resize(numRightNodes());
}


bool IntersectMultiway::probeHTs() {
    hash_t hashVal = 0;
    for (size_t i = 0; i < numLeftNodes(); i++) {
        KU_ASSERT(probeKeyVectors[i]->state->isFlat());
        for(size_t j = 0; j < numRightNodes(); j++){
            probedIds[i][j].clear();
        }
        if (sharedHTs[i] -> getHashTable() -> getNumEntries() == 0) {
            return false;
        }
        KU_ASSERT(probeKeyVectors[i]->state->getSelSize() == 1);
        auto key = probeKeyVectors[i] -> getValue<nodeID_t>(probeKeyVectors[i]->state->getSelVector()[0]);
        function::Hash::operation<nodeID_t>(key, false, hashVal);
        auto flatTuple = sharedHTs[i]->getHashTable()->getTupleForHash(hashVal);
        bool has_match = false;
        while(flatTuple){
            if (*(nodeID_t*)flatTuple == key) {
                has_match = true;
                for(auto j: info->left2right_idx[i]){
                    overflow_value_t * p_tuple = (overflow_value_t*)(flatTuple + info->keyOffsetInTuple[i][j]);
                    if(p_tuple->numElements == 0){
                        continue;
                    }
                    probedIds[i][j].push_back(*p_tuple);
                }
            }
            flatTuple = *sharedHTs[i]->getHashTable()->getPrevTuple(flatTuple);
        }
        // reverse the tuple vector because this is how they are ordered
        for(auto j: info->left2right_idx[i]){
            size_t num_tuples = probedIds[i][j].size();
            if(num_tuples == 0){
                return false;
            }
            for(auto k = 0u; k < num_tuples/2; k++ ){
                std::swap(probedIds[i][j][k], probedIds[i][j][num_tuples - 1 - k]);
            }
        }
        if(!has_match){
            return false;
        }
    }
    return true;
}


struct TupleCursor {
private:
    size_t tuple_idx;
    sel_t ele_idx;
    std::vector<overflow_value_t> & tuples;
    bool finished;

public:
    std::vector<std::shared_ptr<common::SelectionVector>> sels;

    explicit TupleCursor(std::vector<overflow_value_t> & tuples): tuples{tuples} {
        tuple_idx = 0;
        ele_idx = 0;
        finished = false;
        for(size_t i = 0; i < tuples.size(); i++){
            sels.push_back(std::make_shared<common::SelectionVector>(tuples[i].numElements));
            KU_ASSERT(tuples[i].numElements > 0);
            sels.back()->setToFiltered(0);
        }
    }

    bool hasFinished() const {
        return finished;
    }

    void gotoNext(){
        KU_ASSERT(!finished);
        if(ele_idx + 1 < tuples[tuple_idx].numElements){
            ele_idx++;
        } else {
            do {
                tuple_idx++;
                if(tuple_idx + 1 > tuples.size()){
                    finished = true;
                    break;
                }
            } while(tuples[tuple_idx].numElements == 0);
            ele_idx = 0;
        }
    }

    inline nodeID_t getEleOfCurrentTuple(sel_t idx){
        return ((nodeID_t*)(tuples[tuple_idx].value))[idx];
    }

    void goUtil(nodeID_t target){
        KU_ASSERT(!finished);
        KU_ASSERT(getCurrentID() < target);
        // find the correct tuple_idx
        do {
            auto end_ele = getEleOfCurrentTuple(tuples[tuple_idx].numElements - 1);
            if(end_ele < target){
                tuple_idx++;
                if(tuple_idx + 1 > tuples.size()){
                    finished = true;
                    return;
                }
            } else{
                break;
            }
        } while(true);
        // binary search
        sel_t start = 0;
        sel_t end = tuples[tuple_idx].numElements - 1;
        do{
            if(getEleOfCurrentTuple(start) >= target){
                ele_idx = start;
                break;
            }
            if(getEleOfCurrentTuple(end) == target){
                ele_idx = end;
                break;
            }
            if(start + 1 >= end){
                ele_idx = end;
                break;
            }
            auto mid = (start + end)/2;
            if(getEleOfCurrentTuple(mid) >= target){
                end = mid;
            } else{
                start = mid;
            }
        }while(true);
    }

    nodeID_t getCurrentID(){
        KU_ASSERT(!finished);
        return getEleOfCurrentTuple(ele_idx);
    }

    void selectCurrent(){
        KU_ASSERT(!finished);
        auto & sel = sels[tuple_idx];
        auto buf = sel->getMutableBuffer();
        buf[sel->getSelSize()] = ele_idx;
        sel->incrementSelSize();
    }

    void discardEmptyTuples(){
        KU_ASSERT(tuples.size() == sels.size());
        size_t last_empty = 0;
        for(size_t i = 0; i < tuples.size(); i++){
            if(sels[i]->getSelSize() > 0){
                if(i != last_empty){
                    std::swap(sels[i], sels[last_empty]);
                    std::swap(tuples[i], tuples[last_empty]);
                }
                last_empty++;
            }
        }
        sels.resize(last_empty);
        tuples.resize(last_empty);
    }
};


bool IntersectMultiway::multiway_intersect_on_sorted_tuples(size_t rightSideNodeIdx){
    size_t num_probes = info->right2left_idx[rightSideNodeIdx].size();
    std::vector<TupleCursor> cursors;
    for(size_t i = 0; i < num_probes; i++){
        cursors.emplace_back(probedIds[info->right2left_idx[rightSideNodeIdx][i]][rightSideNodeIdx]);
        KU_ASSERT(cursors.back().sels.size() > 0);
    }
    nodeID_t target = cursors[0].getCurrentID();
    bool emptyIntersect = true;
    bool finished = false;

    while(true){
        // forward each cursors to target
        bool target_changed = false;
        for(auto & cursor: cursors){
            do {
                nodeID_t cur_id = cursor.getCurrentID();
                if (cur_id > target){
                    // the current target is missed, set to new target
                    target = cur_id;
                    target_changed = true;
                    break;
                }
                else if (cur_id == target) {
                    // found a possibly intersected ID
                    break;
                }
                else {
                    cursor.goUtil(target);
                    if(cursor.hasFinished()){
                        // a set is exhausted, finish the whole loop
                        finished = true;
                        break;
                    }
                }
            } while(true);
            if(target_changed || finished){
                break;
            }
        }
        // if a set is exhausted, no more intersection to be found
        if(finished){
            break;
        }
        if(!target_changed){
            emptyIntersect = false;
            for(auto & cursor: cursors){
                cursor.selectCurrent();
                cursor.gotoNext();
                if(cursor.hasFinished()){
                    finished = true;
                } else {
                    // update the target to current max ID 
                    auto current = cursor.getCurrentID();
                    if(current > target){
                        target = current;
                    }
                }
            }
        }
        if(finished){
            break;
        }
    }

    // gather the selection vectors of all cursors and return
    for(size_t i = 0; i < num_probes; i++){
        // do not include tuples that does not have an intersected ID
        cursors[i].discardEmptyTuples();
        auto leftSideNodeIdx = info->right2left_idx[rightSideNodeIdx][i];
        intersectSelVectors[leftSideNodeIdx][rightSideNodeIdx] = std::move(cursors[i].sels);
        for(auto & sel: intersectSelVectors[leftSideNodeIdx][rightSideNodeIdx]){
            KU_ASSERT(sel != nullptr);
        }
        KU_ASSERT(probedIds[leftSideNodeIdx][rightSideNodeIdx].size() == intersectSelVectors[leftSideNodeIdx][rightSideNodeIdx].size());
    }
    return emptyIntersect;
}


bool IntersectMultiway::getNextTuplesInternal(ExecutionContext* context){
    // if the last tuple's intersection result has been finished
    while(loopState->hasFinished()){
        // get the next tuple from child until none of the probed tuples is empty
        do {
            if (!children[0]->getNextTuple(context)) {
                return false;
            }
        } while(!probeHTs());

        // dynamic ordering: compute cardinality product
        std::fill(card_prod.begin(), card_prod.end(), 1);
        for(size_t j = 0; j < numRightNodes(); j++){
            for(auto i: info->right2left_idx[j]){
                uint64_t sum = 0;
                for(auto & tuple: probedIds[i][j]){
                    sum += tuple.numElements;
                }
                card_prod[j] *= sum;
            }
        }
        // dynamic ordering: decide intersecting order by sorting the cardinality product
        std::iota(dynamic_order.begin(), dynamic_order.end(), 0);
        std::sort(dynamic_order.begin(), dynamic_order.end(), [this](int i, int j){return card_prod[i] < card_prod[j];});

        // compute all the intersections and save them in intersectSelVectors
        bool has_empty = false;
        for(size_t j = 0; j < numRightNodes(); j++){
            if(multiway_intersect_on_sorted_tuples(dynamic_order[j])){
                has_empty = true;
                break;
            }
        }
        if(has_empty){
            continue;
        } else{
            loopState->reset();
            break;
        }
    }

    size_t num_output_tuples = 1;
    for(size_t j = 0; j < numRightNodes(); j++){
        // Move the intersection keys in probedIds[loopState->smallesLeftSide[j]][j][] to the output value vector
        auto tuple_to_move = probedIds[loopState->smallesLeftSide[j]][j][loopState->cursorAt(j)];
        auto sel = intersectSelVectors[loopState->smallesLeftSide[j]][j][loopState->cursorAt(j)];
        KU_ASSERT(sel != nullptr);
        memcpy(
            outKeyVectors[j]->getData(),
            tuple_to_move.value,
            tuple_to_move.numElements * sizeof(nodeID_t)
        );
        // copy sel vector
        outKeyVectors[j]->getSelVectorPtr()->setRange(0, sel->getSelSize());
        memcpy(
            outKeyVectors[j]->getSelVectorPtr()->getMutableBuffer().data(),
            sel->getMutableBuffer().data(),
            sel->getSelSize() * sizeof(sel_t)
        );
        num_output_tuples *= sel->getSelSize();
    }

    // move forward the cursor
    loopState->gotoNext();
    metrics->numOutputTuple.increase(num_output_tuples);
    return true;
}

std::string nodeIdArrayToString(overflow_value_t tuple) {
    if(tuple.value == nullptr){
        return "(uninitialized)";
    }
    else{
        std::string s = "(size=" + std::to_string(tuple.numElements) + "){";
        for(size_t i = 0; i < tuple.numElements; i++){
            auto node_id = ((nodeID_t*)(tuple.value + i * sizeof(nodeID_t)));
            s += "(" + std::to_string(node_id->tableID) + "," + std::to_string(node_id->offset) + ")" + ",";
        }
        s += "}";
        return s;
    }
}

} // namespace processor
} // namespace kuzu
