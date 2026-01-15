#pragma once

#include "common/data_chunk/data_chunk_state.h"
#include "common/system_config.h"
#include "common/types/value/value.h"
#include "common/vector/value_vector.h"
#include "processor/operator/hash_join/hash_join_build.h"
#include "storage/buffer_manager/memory_manager.h"

namespace kuzu {
namespace processor {

// struct IntersectMultiwayBuildPrintInfo final : OPPrintInfo {
//     binder::expression_vector keys;
//     binder::expression_vector payloads;

//     IntersectMultiwayBuildPrintInfo(binder::expression_vector keys, binder::expression_vector payloads)
//         : keys{std::move(keys)}, payloads(std::move(payloads)) {}

//     std::string toString() const override {
//         std::string result = "Keys: ";
//         result += binder::ExpressionUtil::toString(keys);
//         if (!payloads.empty()) {
//             result += ", Payloads: ";
//             result += binder::ExpressionUtil::toString(payloads);
//         }
//         return result;
//     }

//     std::unique_ptr<OPPrintInfo> copy() const override {
//         return std::unique_ptr<IntersectMultiwayBuildPrintInfo>(new IntersectMultiwayBuildPrintInfo(*this));
//     }

// private:
//     IntersectMultiwayBuildPrintInfo(const IntersectMultiwayBuildPrintInfo& other)
//         : OPPrintInfo{other}, keys{other.keys}, payloads{other.payloads} {}
// };

class IntersectMultiwayBuild final : public HashJoinBuild {
    static constexpr PhysicalOperatorType type_ = PhysicalOperatorType::INTERSECT_MULTIWAY_BUILD;

public:
    IntersectMultiwayBuild(std::shared_ptr<HashJoinSharedState> sharedState, HashJoinBuildInfo info,
        std::unique_ptr<PhysicalOperator> child, uint32_t id,
        std::unique_ptr<OPPrintInfo> printInfo)
        : HashJoinBuild{type_, std::move(sharedState), std::move(info), std::move(child), id,
              std::move(printInfo)} {}

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override{
        HashJoinBuild::initLocalStateInternal(resultSet, context);

        auto mm = storage::MemoryManager::Get(*context->clientContext);
        for(size_t i = 0; i < keyVectors.size(); i++){
            cachedKeyVectors.emplace_back(std::make_shared<common::ValueVector>(keyVectors[i]->dataType.getLogicalTypeID(), mm));
            cachedKeyVectors.back()->setState(std::make_shared<common::DataChunkState>());
            if(keyVectors[i]->state->isFlat()){
                cachedKeyVectors.back()->state->setToFlat();
            }
            cachedKeyVectors.back()->getSelVectorPtr()->setSelSize(0);
        }

        cachedPayloadVectors.resize(payloadVectors.size());
        for(size_t i = 0; i < payloadVectors.size(); i++){
            cachedPayloadVectors[i].emplace_back(std::make_shared<common::ValueVector>(payloadVectors[i]->dataType.getLogicalTypeID(), mm));
            cachedPayloadVectors[i].back()->setState(std::make_shared<common::DataChunkState>());
            cachedPayloadVectors[i].back()->getSelVectorPtr()->setSelSize(0);
        }
    }

    void dumpCachedKeyPayloads(){
        // check that all payloads are non-empty
        bool skip_dumping = false;
        for(auto & keyVector: cachedKeyVectors){
            if(keyVector->getSelVectorPtr()->getSelSize() == 0){
                skip_dumping = true;
                break;
            }
        }
        for(auto & cache: cachedPayloadVectors){
            if(cache[0]->getSelVectorPtr()->getSelSize() == 0){
                skip_dumping = true;
                break;
            }
        }

        // dump the key/payloads
        if(!skip_dumping){
            std::vector<common::ValueVector*> append_keys(cachedKeyVectors.size(), nullptr);
            for(size_t i = 0; i < cachedKeyVectors.size(); i++){
                append_keys[i] = cachedKeyVectors[i].get();
            }
            KU_ASSERT(cachedKeyVectors[0]->state->getSelVector().getSelSize() == 1);
            std::vector<common::ValueVector*> append_payloads(cachedPayloadVectors.size(), nullptr);
            std::vector<size_t> indices(cachedPayloadVectors.size(), 0);
            bool has_unfinished = false;
            do{
                for(size_t i = 0; i < cachedPayloadVectors.size(); i++){
                    append_payloads[i] = cachedPayloadVectors[i][indices[i] == -1u? 0 : indices[i]].get();
                }
                auto numTuplesAppended = hashTable->appendVectors(append_keys, append_payloads, append_keys[0]->state.get());
                metrics->numOutputTuple.increase(numTuplesAppended);
                metrics->numHashInsert.increase(numTuplesAppended);
                has_unfinished = false;
                for(size_t i = 0; i < cachedPayloadVectors.size(); i++){
                    indices[i]++;
                    if(indices[i] >= cachedPayloadVectors[i].size() || cachedPayloadVectors[i][indices[i]]->getSelVectorPtr()->getSelSize() == 0 ){
                        indices[i] = -1u;
                        cachedPayloadVectors[i][0]->getSelVectorPtr()->setSelSize(0);
                    }
                    else{
                        has_unfinished = true;
                    }
                }
            } while(has_unfinished);
        }

        // clean up the cache
        for(auto vector: cachedKeyVectors){
            vector->getSelVectorPtr()->setSelSize(0);
        }
        for(auto & cache: cachedPayloadVectors){
            for(auto & vector: cache){
                vector->getSelVectorPtr()->setSelSize(0);
            }
        }
    }

    void cacheKeyPayloads(ExecutionContext* context, bool is_new_key) {
        auto mm = storage::MemoryManager::Get(*context->clientContext);

        if(is_new_key){
            for(size_t i = 0; i < keyVectors.size(); i++){
                auto src = keyVectors[i];
                auto dst = cachedKeyVectors[i].get();
                for(auto j: src->getSelVectorPtr()->getSelectedPositions()){
                    dst->copyFromVectorData(dst->getSelVectorPtr()->getSelSize(), src, j);
                    dst->getSelVectorPtr()->incrementSelSize(1);
                }
            }
        }

        for(size_t i = 0; i < payloadVectors.size(); i++){
            auto src = payloadVectors[i];
            for(auto j: src->getSelVectorPtr()->getSelectedPositions()){
                auto & cache = cachedPayloadVectors[i];
                common::ValueVector * dst = nullptr;
                for(auto & v: cache){
                    if(v->getSelVectorPtr()->getSelSize() < common::DEFAULT_VECTOR_CAPACITY){
                        dst = v.get();
                        break;
                    }
                }
                if(dst == nullptr){
                    cache.emplace_back(std::make_shared<common::ValueVector>(src->dataType.getLogicalTypeID(), mm));
                    cache.back()->setState(std::make_shared<common::DataChunkState>());
                    cache.back()->getSelVectorPtr()->setSelSize(0);
                    dst = cache.back().get();
                }
                dst->copyFromVectorData(dst->getSelVectorPtr()->getSelSize(), src, j);
                dst->getSelVectorPtr()->incrementSelSize(1);
            }
        }
    }

    void executeInternal(ExecutionContext* context) override {
        KU_ASSERT(resultSet->multiplicity == 1);
        while (children[0]->getNextTuple(context)) {
            bool is_new_key = true;
            if(cachedKeyVectors[0]->getSelVectorPtr()->getSelSize() > 0){
                if(*keyVectors[0]->getAsValue(keyVectors[0]->getSelVectorPtr()->getSelectedPositions()[0]) == *cachedKeyVectors[0]->getAsValue(0)){
                    is_new_key = false;
                }
            }
            if(is_new_key){  
                dumpCachedKeyPayloads();
            }
            cacheKeyPayloads(context, is_new_key);
        }
        dumpCachedKeyPayloads();
        // Merge with global hash table once local tuples are all appended.
        sharedState->mergeLocalHashTable(*hashTable);
    }

    std::unique_ptr<PhysicalOperator> copy() override {
        return make_unique<IntersectMultiwayBuild>(sharedState, info.copy(), children[0]->copy(), id,
            printInfo->copy());
    }

private:
    std::vector<std::shared_ptr<common::ValueVector>> cachedKeyVectors;
    std::vector<std::vector<std::shared_ptr<common::ValueVector>>> cachedPayloadVectors;
};

} // namespace processor
} // namespace kuzu
