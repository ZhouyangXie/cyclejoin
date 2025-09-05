#include <iostream>
#include "main/kuzu.h"
#include "planner/operator/logical_plan.h"

int main() {
    auto database = std::make_unique<kuzu::main::Database>("/home/ocea/demo_try_multivertex_wcoj/data/com-Youtube/kuzu.db");
    auto connection = std::make_unique<kuzu::main::Connection>(database.get());
    connection->query("CALL THREADS=1;");
    connection->query("CALL ENABLE_SEMI_MASK=false;");
    std::unique_ptr<kuzu::main::CachedPreparedStatement> plan = connection->getClientContext()->getCachedStatement("MATCH (u:Vertex)-[e:Edge]-(v:Vertex) WHERE ((u.id % 8) = 0) AND ((v.id % 8) = 0) HINT ((u JOIN e) JOIN v) RETURN u.id, v.id;");
    std::cout << plan->logicalPlan->toString() << std::endl;
    std::unique_ptr<kuzu::main::QueryResult> result = connection->getClientContext()->executeCachedStatement(std::move(plan));
    std::cout << result->toString() << std::endl;
}
