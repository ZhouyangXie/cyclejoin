#pragma once

#include <cstdint>

#include "common/api.h"

namespace kuzu {
namespace common {
enum class StatementType : uint8_t;
}

namespace main {

/**
 * @brief PreparedSummary stores the compiling time and query options of a query.
 */
struct PreparedSummary { // NOLINT(*-pro-type-member-init)
    double compilingTime = 0;
    common::StatementType statementType;
};

/**
 * @brief QuerySummary stores the execution time, plan, compiling time and query options of a query.
 */
class QuerySummary {

public:
    QuerySummary() = default;
    explicit QuerySummary(const PreparedSummary& preparedSummary)
        : preparedSummary{preparedSummary} {}
    /**
     * @return query compiling time in milliseconds.
     */
    KUZU_API double getCompilingTime() const;
    /**
     * @return query execution time in milliseconds.
     */
    KUZU_API double getExecutionTime() const;

    KUZU_API std::size_t getNumHashInsert() const;

    KUZU_API std::size_t getNumHashProbe() const;

    KUZU_API std::size_t getNumIntersect() const;

    void setExecutionTime(double time);

    void setNumHashInsert(std::size_t num);

    void setNumHashProbe(std::size_t num);

    void setNumIntersect(std::size_t num);

    void incrementCompilingTime(double increment);

    void incrementExecutionTime(double increment);

    /**
     * @return true if the query is executed with EXPLAIN.
     */
    bool isExplain() const;

    /**
     * @return the statement type of the query.
     */
    common::StatementType getStatementType() const;

private:
    double executionTime = 0;
    std::size_t numHashInsert = 0;
    std::size_t numHashProbe = 0;
    std::size_t numIntersect = 0;
    PreparedSummary preparedSummary;
};

} // namespace main
} // namespace kuzu
