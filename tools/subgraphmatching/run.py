import kuzu

from os.path import isfile, basename
from networkx import Graph, DiGraph



def translate_query_graph_to_cypher(query:  Graph) -> str:
    """Translate a query graph to Cypher query.

    Args:
        query (Graph | DiGraph): The query graph with label.
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
        if query.is_directed() else
        f"({v_src}:Vertex)-[{v_src}_{v_dst}:Edge]-({v_dst}:Vertex)"
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
        float: execution time in millisecond
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


def run_example():
    database_path = "./kuzu.db"
    convert_dataset("./nodes.csv", "./edges.csv", database_path)
    db = kuzu.Database(database_path, max_num_threads=1)
    conn = kuzu.Connection(db)

    graph = DiGraph()
    graph.add_node("a", label=0)
    graph.add_node("b", label=1)
    graph.add_node("c", label=2)
    graph.add_node("d", label=3)
    graph.add_edges_from([
        ("a", "b"),
        ("a", "c"),
        ("a", "d"),
        ("b", "c"),
        ("b", "d"),
    ], label=0)

    elapse = run_query(graph, conn, use_cycle_join=False)
    print(f"Execution time (kuzu default): {elapse:.4f} ms")
    elapse = run_query(graph, conn, use_cycle_join=True)
    print(f"Execution time (CycleJoin): {elapse:.4f} ms")


if __name__ == "__main__":
    run_example()
