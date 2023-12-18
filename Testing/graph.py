import json
import networkx as nx
import matplotlib.pyplot as plt
import numpy as np
import sys


class Graph:
    def __init__(self):
        # Initialize as a networkx Graph
        self.graph = nx.Graph()

    def add_edge(self, node1, node2, distance):
        # Add edges to the networkx Graph object
        self.graph.add_edge(node1, node2, weight=distance)

    def add_edges_from_json(self, json_graph):
        for node, edges in json_graph.items():
            for connected_node, distance in edges.items():
                self.add_edge(node, connected_node, distance)

    def check_if_edge_exists(self, node1, node2):
        return self.graph.has_edge(node1, node2)

    def add_edges_from_matrix(self, matrix):
        nodes = matrix[0, 1:]  # Node IDs from the first row (excluding the first cell)
        n = len(nodes)

        for i in range(1, n + 1):
            for j in range(1, n + 1):
                weight = matrix[i, j]
                if weight not in [0, None, np.inf, "inf", ""]:
                    self.add_edge(nodes[i - 1], nodes[j - 1], weight)

    def get_adjacency_matrix(self):
        nodes = sorted(self.graph.nodes())
        n = len(nodes)
        matrix = np.zeros((n + 1, n + 1), dtype=object)

        for i in range(1, n + 1):
            matrix[0][i] = matrix[i][0] = nodes[i - 1]

        for u, v, data in self.graph.edges(data=True):
            u_index = nodes.index(u) + 1
            v_index = nodes.index(v) + 1
            matrix[u_index][v_index] = data["weight"]
            matrix[v_index][u_index] = data["weight"]

        # # Formatting the matrix for print
        # formatted_matrix = "[" + ", ".join([str(list(row)) for row in matrix]) + "]"

        return matrix

    def print_adjacency_matrix(self):
        print("Adjacency Matrix with Node IDs:")
        print(self.get_adjacency_matrix())

    def visualize(self):
        pos = nx.spring_layout(self.graph)  # positions for all nodes
        nx.draw(self.graph, pos, with_labels=True)
        labels = nx.get_edge_attributes(self.graph, "weight")
        nx.draw_networkx_edge_labels(self.graph, pos, edge_labels=labels)
        plt.show()

    def print_graph(self):
        # Reconstructing the graph in a dictionary format
        graph_dict = {}
        for u, v, data in self.graph.edges(data=True):
            if u not in graph_dict:
                graph_dict[u] = {}
            graph_dict[u][v] = data["weight"]
        print(json.dumps(graph_dict, indent=4))


def main(arg):
    if arg:
        print("Reading from file:", arg)
        with open(arg, "r") as file:
            json_graph = json.load(file)
        g = Graph()
        g.add_edges_from_json(json_graph["LoRaMesherAdjacencyGraph"])
        g.print_adjacency_matrix()
        g.visualize()
        return

    # Example usage
    g = Graph()

    # JSON input for edges
    # json_edges = {1111: {2222: 10, 3333: 15}, 2222: {4444: 20}, 3333: {4444: 25}}

    # g.add_edges_from_json(json_edges)

    # Adjacency matrix input
    matrix = np.array(
        [
            [
                0,
                20056,
                20068,
                22212,
                22300,
                22312,
                22656,
                25516,
                27980,
                37428,
                38560,
            ],
            [20056, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1],
            [20068, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0],
            [22212, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0],
            [22300, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0],
            [22312, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0],
            [22656, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0],
            [25516, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1],
            [27980, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0],
            [37428, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0],
            [38560, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1],
        ]
    )

    g.add_edges_from_matrix(matrix)

    # Print the graph in JSON format
    # g.print_graph()

    # Print the adjacency matrix
    g.print_adjacency_matrix()

    # Visualize the graph
    g.visualize()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        main(None)
