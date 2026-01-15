#include <iostream>
#include "main/kuzu.h"


int main() {
    auto database = std::make_unique<kuzu::main::Database>("./tools/subgraphmatching/kuzu.db");
    auto connection = std::make_unique<kuzu::main::Connection>(database.get());

    connection->query("CALL THREADS=1;");

    std::string query = "MATCH "
    "(u0:Vertex)-[u0_u1:Edge]->(u1:Vertex), "
    "(u0:Vertex)-[u0_v0:Edge]->(v0:Vertex), "
    "(u1:Vertex)-[u1_v0:Edge]->(v0:Vertex), "
    "(u0:Vertex)-[u0_v1:Edge]->(v1:Vertex), "
    "(u1:Vertex)-[u1_v2:Edge]->(v2:Vertex), "
    "(v1:Vertex)-[v1_v2:Edge]->(v2:Vertex) "
    "WHERE u0.label = 0 AND u1.label = 1 AND v0.label = 2 AND v1.label = 3 AND v2.label = 4 "
    "RETURN COUNT(*);";

    // kuzu baseline
    {
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=false;");
        auto result = connection->query(query);
        std::cout << "kuzu baseline(" << result->toString() << "): " << result->getQuerySummary()->getExecutionTime() << std::endl;
    }
    // multiway (no semi-mask)
    {
        connection->query("CALL ENABLE_SEMI_MASK=false;");
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=true;");
        auto result = connection->query(query);
        std::cout << "multiway(" << result->toString() << "): " << result->getQuerySummary()->getExecutionTime() << std::endl;
    }

    return 0;
}
