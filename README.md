## CycleJoin

This is the implementation of paper "CycleJoin: An Efficient Join-based Algorithm for Multi-Cyclic Subgraph Matching" (under review) based on [kuzu](https://github.com/kuzudb/kuzu).

### Build

Consult the official [building instructions](https://kuzudb.github.io/docs/developer-guide/) of kuzu to build from source.

### Cypher Interface

To execute Cypher queries with CycleJoin in any kuzu interface, simply execute the following statement beforehand:

```
CALL ENABLE_MULTIWAY_INTERSECT=true;
```

Other interfaces are the same as kuzu. See [the docs of kuzu](https://kuzudb.github.io/docs/).

### Subgraph Matching

A simple example to run and evaluate subgraph matching can be found at `tools/subgraphmatching/run.py`. To run it, [build the Python interface](https://kuzudb.github.io/docs/developer-guide/#python) in the build step.

After building the Python interface, add the absolute path of `tools/python_api/build` to `PYTHONPATH` so that you can `import kuzu`. Then change to directory `tools/subgraphmatching` and run `run.py` as trial.

`run.py` run the queries evaluated in the paper on a demo data graph. The format of the data graph is specified at the docstring of function `convert_dataset`. Else, you can load the graph and make the query in other ways enabled by kuzu, but making sure the ID is incremental and continuous.

### Investigate the CycleJoin Implementation

The algorithms of CycleJoin (see pseudocode in the paper) are implemented in the following source code files:

* Logical operator of CycleJoin: [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/planner/operator/logical_intersect_multiway.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/planner/operator/logical_intersect_multiway.cpp).

* Mapping from logical to physical operator: [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/map/map_intersect_multiway.cpp).

* Physical operator of CycleJoin building phase (building shared hash tables, lines 1 to 8 of Algorithm 2): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/intersect/intersect_multiway_build.h).

* Physical operator of CycleJoin probing phase (multiway intersection, lines 9 to 18 of Algorithm 2): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/intersect/intersect_multiway.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/operator/intersect/intersect_multiway.cpp).

* Physical operator of SharedScan (sharing the rel table scanning): [.h](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/processor/operator/scan/shared_scan_rel_table.h), [.cpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/processor/operator/scan/shared_scan_rel_table.cpp).

* The CycleJoin-enabled query optimizer entrance (Algorithm 3): [.cpp:250](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/planner/plan/plan_join_order.cpp#L250C13-L250C57).

* Finding basic paths, finding and comparing cycle-join graphs, finding external paths (Algorithm 4 and 5): [.hpp](https://github.com/ZhouyangXie/cyclejoin/blob/dev-multiwaywcoj/src/include/binder/query/query_graph_simple.hpp).
