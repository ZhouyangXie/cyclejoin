from random import randint
from os.path import isfile, basename
from networkx import Graph, DiGraph


import kuzu


def translate_query_graph_to_cypher(query:  DiGraph) -> str:
    """Translate a query graph to Cypher query.

    Args:
        query (DiGraph): The query graph with label.
            Each labelled node/edge should have a `label`(int) attribute and optionally with a `n_labels`(int) attribute.
            The where clause will condition that the label (mod `n_labels`) equals the `label`.
            The name of the vertices should be unique and convertible to str.

    Returns:
        str: the Cypher query
    """
    assert query.number_of_nodes() > 0, "The query graph must not be empty."
    # MATCH e0, e1, e2, ..., v0, v1, v2, ...
    patterns = ", ".join([
        f"({v_src}:Vertex)-[{v_src}_{v_dst}:Edge]->({v_dst}:Vertex)"
        for (v_src, v_dst) in query.edges.keys()
    ] + [
        f"({v}:Vertex)" for v in query.nodes
    ])
    match_clause = f"MATCH {patterns}\n"
    # WHERE v0.label=label0 AND v1.label=label1 AND ...
    vertex_conditions = [f"{v}.label={v_attrs["label"]}" for v, v_attrs in query.nodes.items() if  "label" in v_attrs]
    edge_conditions = [f"{e[0]}_{e[1]}.label={e_attrs["label"]}" for e, e_attrs in query.edges.items() if  "label" in e_attrs]
    where_clause = " AND ".join(vertex_conditions + edge_conditions)
    where_clause = "" if len(where_clause) == 0 else f"WHERE {where_clause}\n"

    return  match_clause + where_clause + " RETURN COUNT(*);"


def convert_dataset(node_table_path: str, rel_table_path: str, database_path: str) -> None:
    """
    Args:
        node_table_path (str): path to a 2-column int64-valued headerless CSV storing the nodes of the data graph.
            The 1st column is the node ID, the ID is incremental (from 0) and continuous.
            The 2nd column is the node label.
            The rows are ordered by the first column.
        rel_table_path (str): path to a 3-column int64-valued headerless CSV storing the edges of the data graph.
            The 1st column is the node ID of the edge source, and the 2nd is the node ID of the edge destination.
            The 3rd column is the edge label.
            The rows are ordered primarily by the 1st column and secondarily by the 2nd column.
        database_path (str): the path to store the formatted kuzudb database file. The basename must be `kuzu.db`.
    """
    assert isfile(node_table_path)
    assert isfile(rel_table_path)
    assert basename(database_path) == "kuzu.db", f"The kuzudb file should be path/to/kuzu.db. Got {database_path}"
    if isfile(database_path):
        return

    # use 1 thread to avoid disturbing the ID order.
    db = kuzu.Database(database_path, max_num_threads=1)
    conn = kuzu.Connection(db)
    conn.execute("CALL THREADS=1;")
    conn.execute("CREATE NODE TABLE Vertex(id INT64 PRIMARY KEY, label INT64)")
    conn.execute("CREATE REL TABLE Edge(FROM Vertex TO Vertex, label INT64)")
    conn.execute(f'COPY Vertex FROM "{node_table_path}"')
    conn.execute(f'COPY Edge FROM "{rel_table_path}"')
    conn.close()


def run_query(query_graph: Graph, connection: kuzu.Connection, use_cycle_join: bool) -> float:
    """
    Args:
        query_graph (Graph): the query graph (see the docstring of `translate_query_graph_to_cypher`)
        connection (kuzu.Connection): database connection to the data graph
        use_cycle_join (bool): whether use CycleJoin

    Returns:
        float: execution time in millisecond (not including the parsing and query optimization time)
        int: the number of matching results
    """
    if use_cycle_join:
        connection.execute("CALL ENABLE_MULTIWAY_INTERSECT=true;")
        connection.execute("CALL ENABLE_SEMI_MASK=false;")

    cypher = translate_query_graph_to_cypher(query_graph)
    response = connection.execute(cypher)
    elapse = float(response.get_execution_time())

    # restor the default config
    if use_cycle_join:
        connection.execute("CALL ENABLE_MULTIWAY_INTERSECT=false;")
        connection.execute("CALL ENABLE_SEMI_MASK=true;")

    return elapse


def edge_list_to_graph_with_random_label(edge_list: list[tuple[str, str]], label_max: int) -> Graph:
    """
    Convert an edge list to a query graph, randomly label the vertices (a random int from 0 to `label_max` - 1)

    Args:
        edge_list (list[tuple[str, str]]): list of edges, no self-loop, should be connected
        label_max (int): the random label range

    Returns:
        Graph: the query graph
    """
    assert label_max >= 1
    nodes: set[str] = set()
    for src, dst in edge_list:
        nodes.add(src)
        nodes.add(dst)
    graph = DiGraph()
    for node in nodes:
        graph.add_node(node, label=randint(0, label_max - 1))
    for src, dst in edge_list:
        assert src != dst
        graph.add_edge(src, dst)

    return graph


def run_example():
    # load the data graph into kuzu database
    database_path = "./kuzu.db"
    # this data graph has vertex label 0, 1, ..., 7
    n_vertex_labels = 8
    # convert the demo data graph (from https://snap.stanford.edu/data/ego-Facebook.html)
    convert_dataset("./nodes.csv", "./edges.csv", database_path)

    # connect the database
    db = kuzu.Database(database_path)
    conn = kuzu.Connection(db)

    # the evaluated query graphs in Figure 5 in the paper.
    query_graphs: dict[str, list[tuple[str, str]]] = {
        "Q1": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v0"),
            ("u1", "v1"),
        ],
        "Q2": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("v0", "v1")
        ],
        "Q3": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v2"),
            ("u1", "v1"),
            ("u1", "v2"),
            ("v0", "v1"),
        ],
        "Q4": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("u1", "r0"),
        ],
        "Q5": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("v0", "r0"),
        ],
        "Q6": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("v0", "r0"),
            ("u1", "r0"),
        ],
        "Q7" : [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u0", "v2"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("u1", "v2"),
        ],
        "Q8": [
            ("u0", "u1"),
            ("u1", "u2"),
            ("u0", "v0"),
            ("u1", "v0"),
            ("u2", "v0"),
            ("u0", "v1"),
            ("u1", "v1"),
            ("u2", "v1"),
        ],
        "Q9" : [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u0", "v2"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("u1", "v2"),
            ("v0", "v1"),
            ("v1", "v2"),
        ],
        "Q10": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u0", "v2"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("u1", "v2"),
            ("v0", "v2"),
            ("v0", "v1"),
            ("v1", "v2"),
        ],
        "Q11": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u0", "v2"),
            ("u0", "v3"),
            ("u1", "v0"),
            ("u1", "v1"),
            ("u1", "v2"),
            ("u1", "v3"),
        ],
        "Q12": [
            ("u0", "u1"),
            ("u1", "u2"),
            ("u0", "v0"),
            ("u1", "v0"),
            ("u0", "v1"),
            ("u2", "v1"),
            ("u1", "v2"),
            ("u2", "v2"),
        ],
        "Q13": [
            ("u0", "u1"),
            ("u0", "v0"),
            ("u0", "v1"),
            ("u1", "v2"),
            ("u1", "v3"),
            ("v0", "v2"),
            ("v1", "v3"),
        ],
        "Q14": [
            ("u0", "u1"),
            ("u2", "u3"),
            ("v0", "u0"),
            ("v0", "u1"),
            ("v0", "u2"),
            ("v0", "u3"),
            ("v1", "u0"),
            ("v1", "u1"),
            ("v1", "u2"),
            ("v1", "u3"),
        ],
        "Q15": [
            ("u0", "u1"),
            ("u1", "u2"),
            ("u0", "u2"),
            ("u0", "v0"),
            ("u1", "v0"),
            ("u0", "v1"),
            ("u2", "v1"),
            ("u1", "v2"),
            ("u2", "v2"),
        ],
        "Q16": [
            ("u0", "u1"),
            ("u1", "u2"),
            ("u0", "v0"),
            ("u1", "v0"),
            ("u2", "v0"),
            ("u0", "v1"),
            ("u1", "v1"),
            ("u2", "v1"),
            ("u0", "v2"),
            ("u1", "v2"),
            ("u2", "v2"),
        ]
    }

    for query_name, edge_list in query_graphs.items():
        print(query_name)
        graph = edge_list_to_graph_with_random_label(edge_list, n_vertex_labels)
        elapse = run_query(graph, conn, use_cycle_join=False)
        print(f"Execution time (kuzu default): {elapse:.4f} ms.")
        elapse = run_query(graph, conn, use_cycle_join=True)
        print(f"Execution time (CycleJoin): {elapse:.4f} ms.")


if __name__ == "__main__":
    run_example()
