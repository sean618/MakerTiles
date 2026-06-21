


import re
from collections import defaultdict
from tabulate import tabulate

log_file_path = 'log.txt'


nodes = defaultdict(dict)
nodeIdToMbNodeIndex = {}


# Microbus

def microbus_new_node_request(line, hex_pattern=re.compile(r"Node, Tx new node request: 0x(.+)")):
    match = hex_pattern.search(line)
    if match:
        nIndex = int(match.group(1), 16) & 0xFF
        mbIndex = (int(match.group(1), 16) >> 8) & 0xFF
        idx = (mbIndex, nIndex)
        nodes[idx]["newNodeTx"] = nodes[idx].get("newNodeTx", 0) + 1

def microbus_partial_join(line, hex_pattern=re.compile(r"Master - Node:(\d+) partial join - uniqueId:0x(.+)")):
    match = hex_pattern.search(line)
    if match:
        nodeId = int(match.group(1))
        nIndex = int(match.group(2), 16) & 0xFF
        mbIndex = (int(match.group(2), 16) >> 8) & 0xFF
        nodeOrAgent = (int(match.group(2), 16) >> 16) & 0xFF
        idx = (mbIndex, nIndex)
        nodeIdToMbNodeIndex[nodeId] = idx
        if nodeOrAgent == 2:
            nodes[idx]["nodeId"] = nodeId

def microbus_node_join(line, hex_pattern=re.compile(r"Node:(\d+) - joined - uniqueId:0x(.+)")):
    match = hex_pattern.search(line)
    if match:
        nodeId = int(match.group(1))
        nIndex = int(match.group(2), 16) & 0xFF
        mbIndex = (int(match.group(2), 16) >> 8) & 0xFF
        idx = (mbIndex, nIndex)
        nodeIdToMbNodeIndex[nodeId] = idx
        nodes[idx]["Joined"] = nodes[idx].get("Joined", 0) + 1

def microbus_node_fully_joined(line, hex_pattern=re.compile(r"Master Node:(\d+) - fully joined")):
    match = hex_pattern.search(line)
    if match:
        nodeId = int(match.group(1))
        idx = nodeIdToMbNodeIndex[nodeId]
        nodes[idx]["OnNetwork"] = 1
        nodes[idx]["MasterJoined"] = nodes[idx].get("MasterJoined", 0) + 1

def microbus_node_removed(line, hex_pattern=re.compile(r"Master - Node:(\d+), removed from network")):
    match = hex_pattern.search(line)
    if match:
        nodeId = int(match.group(1))
        idx = nodeIdToMbNodeIndex[nodeId]
        nodes[idx]["OnNetwork"] = 0
        nodes[idx]["Removed"] = nodes[idx].get("Removed", 0) + 1



with open(log_file_path, 'r') as log_file:
    for line in log_file:
        try:            
            microbus_new_node_request(line)
            microbus_partial_join(line)
            microbus_node_join(line)
            microbus_node_fully_joined(line)
            microbus_node_removed(line)
        except Exception as e:
            print(line, flush=True)
            raise

