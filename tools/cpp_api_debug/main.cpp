#include <iostream>
#include "main/kuzu.h"
using namespace kuzu::main;

int main() {
    auto database = std::make_unique<Database>("/home/ocea/demo_try_multivertex_wcoj/data/my-sample/kuzu.db");
    auto connection = std::make_unique<Connection>(database.get());
    auto result = connection->query("MATCH (a:Vertex) RETURN *;");
    std::cout << result->toString();
}
