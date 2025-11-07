"""
Network Renderer for Routing Table Visualization

Renders network topology using NetworkX with ETX-based edge coloring.
"""

import networkx as nx
import matplotlib.pyplot as plt
from typing import List, Dict, Tuple
from logParser import RoutingTableSnapshot, RouteTimeoutEvent
import sys
import os

# Import device colors from existing module
sys.path.insert(0, os.path.dirname(__file__))
from deviceColors import get_color_by_devices


def get_etx_color(etx: float) -> str:
    """
    Map ETX value to color

    Args:
        etx: ETX value (scaled by 10, e.g., 10 = 1.0)

    Returns:
        Color string (hex or name)
    """
    # Convert from scaled value if needed
    if etx > 10:
        etx = etx / 10.0

    etx /= 2

    if etx <= 1.1:
        return '#006400'  # Dark Green - Excellent
    elif etx <= 1.3:
        return '#32CD32'  # Lime Green - Good
    elif etx <= 1.5:
        return '#FFD700'  # Gold - Fair
    elif etx <= 2.0:
        return '#FF8C00'  # Dark Orange - Poor
    else:
        return '#DC143C'  # Crimson - Very Poor


def get_etx_width(etx: float) -> float:
    """
    Map ETX value to edge width

    Args:
        etx: ETX value

    Returns:
        Edge width in points
    """

    if etx > 10:
        etx = etx / 10.0

    etx /= 2

    if etx <= 1.1:
        return 3.0
    elif etx <= 1.5:
        return 2.0
    elif etx <= 2.0:
        return 1.5
    else:
        return 1.0


def create_network_graph(snapshots: List[RoutingTableSnapshot]) -> nx.DiGraph:
    """
    Create NetworkX directed graph from routing table snapshots

    Uses device addresses as node IDs to avoid duplication.

    Args:
        snapshots: List of routing table snapshots

    Returns:
        NetworkX directed graph
    """
    G = nx.DiGraph()

    # Map device addresses to COM ports for display
    device_to_com = {}

    # Add nodes and edges from all snapshots
    for snapshot in snapshots:
        # Use device address as node ID (not COM port)
        source_node = snapshot.device_id

        # Track device to COM port mapping
        device_to_com[source_node] = snapshot.com_port

        # Add source node if not exists
        if not G.has_node(source_node):
            G.add_node(source_node, com_port=snapshot.com_port, device_id=snapshot.device_id)

        # Add edges from routes
        for route in snapshot.routes:
            target_node = route.address

            # Add target node if not exists
            if not G.has_node(target_node):
                # Check if we know the COM port for this device
                target_com = device_to_com.get(target_node, "?")
                G.add_node(target_node, com_port=target_com, device_id=target_node)

            # Add edge with ETX information (keep best/latest ETX if duplicate)
            is_timed_out = getattr(route, 'is_timed_out', False)
            # Determine if this is a direct link (via == destination) or multi-hop route
            is_direct = (route.via == route.address)

            if G.has_edge(source_node, target_node):
                # Update if this ETX is better OR if this route is timed out
                existing_etx = G.edges[source_node, target_node]['total_etx']
                if route.total_etx < existing_etx or is_timed_out:
                    G.add_edge(
                        source_node,
                        target_node,
                        total_etx=route.total_etx,
                        reverse_etx=route.reverse_etx,
                        forward_etx=route.forward_etx,
                        via=route.via,
                        hops=route.hops,
                        role=route.role,
                        is_timed_out=is_timed_out,
                        is_direct=is_direct
                    )
            else:
                G.add_edge(
                    source_node,
                    target_node,
                    total_etx=route.total_etx,
                    reverse_etx=route.reverse_etx,
                    forward_etx=route.forward_etx,
                    via=route.via,
                    hops=route.hops,
                    role=route.role,
                    is_timed_out=is_timed_out,
                    is_direct=is_direct
                )

    return G


def render_network_graph(snapshots: List[RoutingTableSnapshot], ax, timeout_events: List[RouteTimeoutEvent] = None):
    """
    Render network graph to matplotlib axes

    Args:
        snapshots: List of routing table snapshots (routes may have is_timed_out flag set)
        ax: Matplotlib axes to draw on
        timeout_events: (Deprecated - timeout info is now in route flags) List of timeout events at current time
    """
    # Note: timeout_events parameter is kept for backward compatibility but is no longer used
    # Timeout information is now stored in the RouteEntry.is_timed_out flag
    ax.clear()

    if not snapshots:
        ax.text(0.5, 0.5, "No routing data available", ha='center', va='center', transform=ax.transAxes)
        ax.axis('off')
        return

    # Create graph
    G = create_network_graph(snapshots)

    if len(G.nodes()) == 0:
        ax.text(0.5, 0.5, "No nodes in network", ha='center', va='center', transform=ax.transAxes)
        ax.axis('off')
        return

    # Layout - use spring layout for nice spacing
    try:
        pos = nx.spring_layout(G, k=2, iterations=50, seed=42)
    except:
        pos = nx.circular_layout(G)

    # Get device colors
    device_ids = [G.nodes[node].get('com_port', str(node)) for node in G.nodes()]
    node_colors_list = get_color_by_devices(device_ids)
    node_colors = {node: node_colors_list[i] for i, node in enumerate(G.nodes())}

    # Draw nodes
    nx.draw_networkx_nodes(
        G, pos,
        node_color=[node_colors[node] for node in G.nodes()],
        node_size=800,
        ax=ax
    )

    # Draw edges with ETX-based coloring
    for edge in G.edges():
        source, target = edge
        edge_data = G.edges[edge]
        etx = edge_data['total_etx']

        # Check if this edge has timed out (from edge attributes)
        is_timed_out = edge_data.get('is_timed_out', False)
        # Check if this is a direct link or multi-hop route
        is_direct = edge_data.get('is_direct', True)

        if is_timed_out:
            # Draw timed-out edge with special styling
            color = '#8B0000'  # Dark red
            width = 2.0
            alpha = 0.4
            style = 'dashed'
        elif not is_direct:
            # Multi-hop route styling (indirect)
            color = '#9370DB'  # Medium purple - neutral color for indirect routes
            width = 1.0
            alpha = 0.6
            style = 'dotted'
        else:
            # Direct link styling - ETX-based coloring
            color = get_etx_color(etx)
            width = get_etx_width(etx)
            alpha = 1.0
            style = 'solid'

        nx.draw_networkx_edges(
            G, pos,
            edgelist=[edge],
            edge_color=color,
            width=width,
            alpha=alpha,
            style=style,
            ax=ax,
            arrows=True,
            arrowsize=15,
            arrowstyle='->'
        )

        # Add red X marker for timed-out edges
        if is_timed_out:
            # Calculate midpoint of edge
            x1, y1 = pos[source]
            x2, y2 = pos[target]
            mid_x = (x1 + x2) / 2
            mid_y = (y1 + y2) / 2

            # Draw red X marker
            ax.text(mid_x, mid_y, '⨯', fontsize=16, color='red',
                   ha='center', va='center', weight='bold',
                   bbox=dict(boxstyle='circle,pad=0.1', facecolor='white', edgecolor='red', linewidth=1.5))

    # Draw node labels
    labels = {}
    for node in G.nodes():
        com_port = G.nodes[node].get('com_port', '?')
        device_id = G.nodes[node].get('device_id', str(node))
        if device_id != "UNKNOWN" and device_id != str(node):
            labels[node] = f"{com_port}\n({device_id})"
        else:
            labels[node] = str(node)

    nx.draw_networkx_labels(G, pos, labels, font_size=8, ax=ax)

    # Draw edge labels (Forward and Reverse ETX values for direct links, Total for indirect)
    # Position labels near source node to show source's F and R values
    edge_labels = {}
    for edge in G.edges():
        edge_data = G.edges[edge]
        is_direct = edge_data.get('is_direct', True)

        if is_direct:
            # Direct link: Show forward and reverse ETX
            forward_etx = edge_data['forward_etx']
            reverse_etx = edge_data['reverse_etx']
            edge_labels[edge] = f"F:{forward_etx:.1f} R:{reverse_etx:.1f}"
        else:
            # Indirect/multi-hop route: Show total ETX and via node
            total_etx = edge_data['total_etx']
            via = edge_data['via']
            edge_labels[edge] = f"Total:{total_etx:.1f}\n(via {via})"

    # Position labels closer to source node (0.3 = 30% along the edge from source)
    nx.draw_networkx_edge_labels(G, pos, edge_labels, font_size=6, ax=ax, label_pos=0.3)

    ax.axis('off')
    ax.set_title("Network Topology with ETX Values", fontsize=12, fontweight='bold')


def render_color_legend(ax):
    """
    Render color legend for ETX values

    Args:
        ax: Matplotlib axes to draw on
    """
    ax.clear()
    ax.axis('off')

    legend_data = [
        ("1.0-1.1", "Excellent", '#006400'),
        ("1.1-1.3", "Good", '#32CD32'),
        ("1.3-1.5", "Fair", '#FFD700'),
        ("1.5-2.0", "Poor", '#FF8C00'),
        ("2.0+", "Very Poor", '#DC143C'),
    ]

    y_pos = 0.9
    ax.text(0.1, y_pos, "ETX Color Legend:", fontsize=10, fontweight='bold', transform=ax.transAxes)
    y_pos -= 0.15

    for etx_range, quality, color in legend_data:
        # Draw color box
        ax.add_patch(plt.Rectangle((0.1, y_pos - 0.05), 0.1, 0.08, facecolor=color, transform=ax.transAxes))

        # Draw text
        ax.text(0.25, y_pos, f"{etx_range}: {quality}", fontsize=9, va='center', transform=ax.transAxes)
        y_pos -= 0.15


if __name__ == "__main__":
    # Test the renderer
    from logParser import parse_log_file

    if len(sys.argv) < 2:
        print("Usage: python networkRenderer.py <log_file.ans>")
        sys.exit(1)

    log_file = sys.argv[1]
    print(f"Parsing and rendering: {log_file}")

    snapshots = parse_log_file(log_file)
    print(f"Found {len(snapshots)} snapshots")

    if snapshots:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

        render_network_graph(snapshots, ax1)
        render_color_legend(ax2)

        plt.tight_layout()
        plt.show()
    else:
        print("No routing table data found in log file")
