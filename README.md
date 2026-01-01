## CycleJoin

This is the implementation of paper "CycleJoin: An Efficient Join-based Algorithm for Multi-Cyclic Subgraph Matching" (under review) based on [kuzu](https://github.com/kuzudb/kuzu).

### Build

Consult the official [building instructions](https://kuzudb.github.io/docs/developer-guide/) of kuzu to build from source.

### Cypher Interface

To execute Cypher queries with CycleJoin, simply execute the following statement beforehand:

```
CALL ENABLE_MULTIWAY_INTERSECT=true;
```

To make a Cypher query by Python/C/Cpp, consult the [docs of kuzu](https://kuzudb.github.io/docs/).

### Investigate the CycleJoin Implementation

The algorithms of CycleJoin are implemented in the following source code files:

* Logical operator of CycleJoin: [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/planner/operator/logical_intersect_multiway.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/planner/operator/logical_intersect_multiway.cpp).

* Mapping from logical to physical operator: [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/map/map_intersect_multiway.cpp).

* Physical operator of CycleJoin building phase (building shared hash tables): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/intersect/intersect_multiway_build.h).

* Physical operator of CycleJoin probing phase (multiway intersection): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/intersect/intersect_multiway.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/operator/intersect/intersect_multiway.cpp).

* Physical operator of SharedScan (sharing the rel table scanning): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/scan/shared_scan_rel_table.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/operator/scan/shared_scan_rel_table.cpp).

* The CycleJoin-enabled query optimizer entrance: [.cpp:250](https://github.com/ZhouyangXie/cyclejoin/blob/32edf1caa59d8c994187b40d8cb6ae9aae7da1a0/src/planner/plan/plan_join_order.cpp#L250C13-L250C57).

* Finding basic paths, finding and comparing cycle-join graphs, finding external paths: [.hpp](https://github.com/ZhouyangXie/cyclejoin/blob/32edf1caa59d8c994187b40d8cb6ae9aae7da1a0/src/include/binder/query/query_graph_simple.hpp).
