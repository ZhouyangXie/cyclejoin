#include <iostream>
#include "main/kuzu.h"


int main() {
    auto database = std::make_unique<kuzu::main::Database>("/home/ocea/demo_try_multivertex_wcoj/data/com-Youtube-undirected/kuzu.db");
    auto connection = std::make_unique<kuzu::main::Connection>(database.get());

    connection->query("CALL THREADS=16;");


    std::string query = "MATCH "
    "(u0:Vertex)-[u0_u1:Edge]->(u1:Vertex), "
    "(u0:Vertex)-[u0_v0:Edge]->(v0:Vertex), "
    "(u0:Vertex)-[u0_v1:Edge]->(v1:Vertex), "
    "(u1:Vertex)-[u1_v0:Edge]->(v0:Vertex), "
    "(u1:Vertex)-[u1_v1:Edge]->(v1:Vertex), "
    "(v0:Vertex)-[v0_v1:Edge]->(v1:Vertex) "
    "WHERE ((u0.label % 16) = 0) AND ((u1.label % 16) = 1) AND ((v0.label % 16) = 0) AND ((v1.label % 16) = 1) "
    "RETURN COUNT(*);";

    std::string query_hint_wcoj = "MATCH "
    "(u0:Vertex)-[u0_u1:Edge]->(u1:Vertex), "
    "(u0:Vertex)-[u0_v0:Edge]->(v0:Vertex), "
    "(u0:Vertex)-[u0_v1:Edge]->(v1:Vertex), "
    "(u1:Vertex)-[u1_v0:Edge]->(v0:Vertex), "
    "(u1:Vertex)-[u1_v1:Edge]->(v1:Vertex), "
    "(v0:Vertex)-[v0_v1:Edge]->(v1:Vertex) "
    "WHERE ((u0.label % 16) = 0) AND ((u1.label % 16) = 1) AND ((v0.label % 16) = 0) AND ((v1.label % 16) = 1) "
    "HINT ((((((u0 JOIN u0_v1 JOIN v1) MULTI_JOIN u0_v0 MULTI_JOIN v0_v1) JOIN v0) MULTI_JOIN u0_u1 MULTI_JOIN u1_v1) JOIN u1) JOIN u1_v0 )"
    "RETURN COUNT(*);";

    // kuzu baseline
    {
        connection->query("CALL ENABLE_SEMI_MASK=true;");
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=false;");
        auto result = connection->query(query);
        std::cout << "kuzu baseline: " << result->getQuerySummary()->getExecutionTime() << std::endl;
    }
    // kuzu baseline no semi-mask
    {
        connection->query("CALL ENABLE_SEMI_MASK=false;");
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=false;");
        auto result = connection->query(query);
        std::cout << "kuzu baseline no semi-mask(" << result->toString() << "): " << result->getQuerySummary()->getExecutionTime() << std::endl;
    }
    // wcoj
    {
        connection->query("CALL ENABLE_SEMI_MASK=true;");
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=false;");
        auto result = connection->query(query_hint_wcoj);
        std::cout << "wcoj(" << result->toString() << "): " <<  result->getQuerySummary()->getExecutionTime() << std::endl;
    }
    // wcoj no semi-mask
    {
        connection->query("CALL ENABLE_SEMI_MASK=false;");
        connection->query("CALL ENABLE_MULTIWAY_INTERSECT=false;");
        auto result = connection->query(query_hint_wcoj);
        std::cout << "wcoj no semi-mask(" << result->toString() << "): " << result->getQuerySummary()->getExecutionTime() << std::endl;
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
