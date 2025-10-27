#include "processor/operator/scan/shared_scan_rel_table.h"

#include "main/client_context.h"
#include "processor/execution_context.h"
#include "storage/local_storage/local_rel_table.h"

using namespace kuzu::common;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

std::string SharedScanRelTablePrintInfo::toString() const {
    std::string result = "";
    for(auto & info: infos){
        result += info.toString() + ",";
    }
    return result;
}

void SharedScanRelTable::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context){
    for(auto & opInfo: opInfos){
        outVectors.emplace_back();
        for (auto& pos : opInfo.outVectorsPos) {
            outVectors.back().push_back(resultSet->getValueVector(pos).get());
        }
    }
    auto clientContext = context->clientContext;
    for(size_t i = 0; i < getNumberOfSharing(); i++){
        auto boundNodeIDVector = resultSet->getValueVector(opInfos[i].nodeIDPos).get();
        auto nbrNodeIDVector = outVectors[i][0];
        scanStates.push_back(std::make_unique<RelTableScanState>(
            *MemoryManager::Get(*clientContext),
            boundNodeIDVector,
            outVectors[i],
            nbrNodeIDVector->state
        ));
        tableInfos[i].initScanState(*scanStates[i], outVectors[i], clientContext);
    }
    stateFinished.resize(scanStates.size(), true);
    flattenScanIndex.resize(getNumberOfSharing(), 0);
    flattenScanSize.resize(getNumberOfSharing(), 0);
}

bool SharedScanRelTable::getNextTuplesInternal(ExecutionContext* context){
    const auto transaction = context->clientContext->getTransaction();
    if(std::all_of(stateFinished.begin(), stateFinished.end(), [](auto x){return x;})){
        if (!children[0]->getNextTuple(context)) {
            return false;
        }
        for(size_t i = 0; i < getNumberOfSharing(); i++){
            tableInfos[i].table->initScanState(transaction, *scanStates[i]);
            stateFinished[i] = false;
        }
    }
    for(size_t i = 0; i < getNumberOfSharing(); i++){
        if(!stateFinished[i]){
            stateFinished[i] = true;
            if(flattenScans[i]){
                if((flattenScanIndex[i] + 1) < flattenScanSize[i]){
                    flattenScanIndex[i]++;
                    auto new_position = scanStates[i]->outState->getSelVectorUnsafe().getMutableBuffer()[flattenScanIndex[i]];
                    scanStates[i]->outState->getSelVectorUnsafe()[0] = new_position;
                    scanStates[i]->outState->getSelVectorUnsafe().setSelSize(1);
                    stateFinished[i] = false;
                } else {
                    // the current batch have been finished, scan for the next batch
                    while(tableInfos[i].table->scan(transaction, *scanStates[i])){
                        auto outputSize = scanStates[i]->outState->getSelSize();
                        if(outputSize > 0){
                            metrics->numOutputTuple.increase(outputSize);
                            stateFinished[i] = false;
                            break;
                        }
                    }
                    if(!stateFinished[i]){
                        flattenScanSize[i] = scanStates[i]->outState->getSelSize();
                        flattenScanIndex[i] = 0;
                        scanStates[i]->outState->getSelVectorUnsafe().setSelSize(1);
                    } else {
                        flattenScanSize[i] = 0;
                        flattenScanIndex[i] = 0;
                        scanStates[i]->outState->getSelVectorUnsafe().setSelSize(0);
                    }
                }
            } else {
                while(tableInfos[i].table->scan(transaction, *scanStates[i])){
                    auto outputSize = scanStates[i]->outState->getSelSize();
                    if(outputSize > 0){
                        metrics->numOutputTuple.increase(outputSize);
                        stateFinished[i] = false;
                        break;
                    }
                }
            }
            if(stateFinished[i]){
                scanStates[i]->outState->getSelVectorUnsafe().setSelSize(0);
            }
        } else {
            scanStates[i]->outState->getSelVectorUnsafe().setSelSize(0);
        }
    }
    return true;
}

}
}
