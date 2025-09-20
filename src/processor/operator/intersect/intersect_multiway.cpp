#include "processor/operator/intersect/intersect_multiway.h"
#include <algorithm>
#include <memory>
#include <string>
#include "function/hash/hash_functions.h"
#include "processor/result/factorized_table.h"


using namespace kuzu::common;

namespace kuzu {
namespace processor {

std::string  IntersecMultiwaytPrintInfo::toString() const{
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
                    // TODO: currently we do not support populating the payloads
                    KU_UNREACHABLE;
                    payloadVectorsToScanInto[i][j].push_back(resultSet->getValueVector(pos).get());
                }
            }
        }
    }
    loopState = std::make_shared<IntersectionLoopState>(this);
    loopState->finished = true;
}


bool IntersectMultiway::probeHTs() {
    hash_t hashVal = 0;
    for (size_t i = 0; i < numLeftNodes(); i++) {
        KU_ASSERT(probeKeyVectors[i]->state->isFlat());
        probedIds[i].clear();
        if (sharedHTs[i] -> getHashTable() -> getNumEntries() == 0) {
            continue;
        }
        // TODO: remove this check
        KU_ASSERT(probeKeyVectors[i]->state->getSelSize() == 1);
        auto key = probeKeyVectors[i] -> getValue<nodeID_t>(probeKeyVectors[i]->state->getSelVector()[0]);
        function::Hash::operation<nodeID_t>(key, false, hashVal);
        auto flatTuple = sharedHTs[i]->getHashTable()->getTupleForHash(hashVal);
        bool has_match = false;
        while(flatTuple){
            if (*(nodeID_t*)flatTuple == key) {
                has_match = true;
                for(auto j: info->left2right_idx[i]){
                    // TODO: check that all intersect keys are store in unflat columns
                    probedIds[i][j].push_back(*(overflow_value_t*)(flatTuple + info->keyOffsetInTuple[i][j]));
                }
            }
            flatTuple = *sharedHTs[i]->getHashTable()->getPrevTuple(flatTuple);
        }
        if(!has_match){
            return false;
        }
    }
    return true;
}


struct TupleCursor {
    size_t tuple_idx;
    sel_t ele_idx;
    std::vector<overflow_value_t> & tuples;
    std::vector<std::shared_ptr<common::SelectionVector>> sels;
    bool finished;

    explicit TupleCursor(std::vector<overflow_value_t> & tuples): tuples{tuples} {
        tuple_idx = 0;
        ele_idx = 0;
        finished = false;
        for(size_t i = 0; i < tuples.size(); i++){
            sels.push_back(std::make_shared<common::SelectionVector>(tuples[i].numElements));
            sels.back()->setToFiltered(0);
        }
    }

    bool hasFinished() const {
        return finished;
    }

    // TODO: implement a `toUtil(nodeID_t)` method to find an ID by exponential search.

    void gotoNext(){
        KU_ASSERT(!finished);
        if(ele_idx < tuples[tuple_idx].numElements - 1){
            ele_idx++;
        } else if (tuple_idx < tuples.size() - 1){
            tuple_idx++;
            ele_idx = 0;
        } else {
            finished = true;
        }
    }

    nodeID_t getCurrentID(){
        KU_ASSERT(!finished);
        return ((nodeID_t*)(tuples[tuple_idx].value))[ele_idx];
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
                    cursor.gotoNext();
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
        // compute all the intersections and save them in intersectSelVectors
        // TODO: dynamic intersection order
        bool has_empty = false;
        for(size_t j = 0; j < numRightNodes(); j++){
            if(multiway_intersect_on_sorted_tuples(j)){
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
        outKeyVectors[j]->state->setSelVector(sel);

        // TODO: it might be more efficient to move as many intersection IDs to outKeyVectors as possible, like Intersect
        // TODO: Populate the payloads
    }

    // move forward the cursor
    loopState->gotoNext();
    return true;
}


} // namespace processor
} // namespace kuzu
