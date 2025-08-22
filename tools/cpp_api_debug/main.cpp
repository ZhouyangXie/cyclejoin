#include <iostream>
#include "main/kuzu.h"
#include "planner/operator/logical_plan.h"

int main() {
    auto database = std::make_unique<kuzu::main::Database>("/home/ocea/demo_try_multivertex_wcoj/data/my-sample/kuzu.db");
    auto connection = std::make_unique<kuzu::main::Connection>(database.get());
    // auto result = connection->query("MATCH (a:Vertex) RETURN *;");
    std::unique_ptr<kuzu::main::CachedPreparedStatement> left = connection->getClientContext()->getCachedStatement("MATCH (u:Vertex)-[e:Edge]-(v:Vertex) RETURN u.id, v.id;");

    // std::unique_ptr<kuzu::planner::LogicalPlan> left_plan = std::move(left->logicalPlan);
    std::cout << left->logicalPlan->toString() << std::endl;
    // kuzu::planner::Schema * left_schema = left_plan->getLastOperator()->getSchema();

    std::unique_ptr<kuzu::main::QueryResult> result = connection->getClientContext()->executeCachedStatement(std::move(left));
    std::cout << result->toString() << std::endl;
}
