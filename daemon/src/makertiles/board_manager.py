'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import threading
import logging
import time

from . import board
# from .field_protocol import MAX_PACKET_SIZE, MAX_NODES, MASTER_NODE_ID, Command, FieldDataType, uuid_to_alphanumeric
from . import field_protocol
from . import tx_manager
from . import rx_manager
from . import ui

class BoardManager:
    def __init__(self, logger, connection=None, max_requests=250):
        self.tiles = {}
        self.logger = logger
        self.connection = connection
        self.tx = tx_manager.TxManager(self.connection, self.logger, max_requests)
        self.rx = rx_manager.RxManager(self.connection, self.logger, self.tx, self)
        self.connected_nodes = []
        self.ui_tile_names =  {}
        self.ui_tiles = ui.UITiles(logger)
        # self.tx_timeout = 


    ########## Rx callbacks ##########

    def update_connected_nodes_from_packet(self, connected_nodes):
        self.connected_nodes = connected_nodes

    def update_tile_info_from_packet(self, src_node, data):
        self.tiles[src_node].update_tile_info_from_packet(data)

    def update_field_info_from_packet(self, src_node, field_index, data):
        if src_node in self.tiles:
            self.tiles[src_node].update_field_info_from_packet(field_index, data)

    def update_field_data_from_packet(self, src_node, field_index, num_fields, data):
        if src_node in self.tiles:
            self.tiles[src_node].update_field_data_from_packet(field_index, num_fields, data)


    ########## Tx Requests ##########

    def send_get_credits_request(self, response_event=None):
        self.tx.send_request(field_protocol.MASTER_NODE_ID, field_protocol.Command.GET_MAX_BUFFER_CREDITS, 0, 0, response_event)
    
    def send_get_connected_nodes_request(self, response_event=None):
        self.tx.send_request(field_protocol.MASTER_NODE_ID, field_protocol.Command.GET_CONNECTED_NODES, 0, 0, response_event)

    def send_get_tile_info_request(self, dst_node: int, response_event=None):
        self.tx.send_request(dst_node, field_protocol.Command.GET_TILE_INFO, 0, 4, response_event)

    def send_get_field_info_request(self, dst_node: int, field_index: int, response_event=None):
        self.tx.send_request(dst_node, field_protocol.Command.GET_FIELD_INFO, field_index, 1, response_event)

    def send_get_fields_request(self, dst_node : int, field_index : int, num_fields : int, response_event=None):
        self.tx.send_request(dst_node, field_protocol.Command.GET_FIELDS, field_index, num_fields, response_event)

    def send_set_fields_request(self, dst_node : int, field_index : int, num_fields : int, data : bytes, response_event=None):
        self.tx.send_request(dst_node, field_protocol.Command.SENDING_FIELDS, field_index, num_fields, response_event, data)


    ########## Main ##########

    def _create_board(self, node_id):
        self.tiles[node_id] = board.Board(node_id, self.logger)

    def _remove_board(self, node_id):
        # TODO: what to do about objects created in user space
        del self.tiles[node_id]

    def _create_ui_tiles(self, new_nodes):
        for node_id in new_nodes:
            board = self.tiles[node_id]
            # Create UI board object
            ui_board = ui.UIBoard(self, self.logger, board)
            # Add to board object
            board.ui_board = ui_board
            # Add to the UI tiles object under it's full name
            setattr(self.ui_tiles, board.full_name, ui_board)

            # Also expose it under the short name. If another board already uses
            # that name, group them into a (sorted) list.
            board_already_exists = board.name in self.ui_tiles.__dict__

            if board_already_exists:
                obj = getattr(self.ui_tiles, board.name)
                if type(obj) is list:
                    # Add to list
                    obj.append(ui_board)
                    obj.sort(key=lambda ui_board: ui_board._board.full_name)
                elif type(obj) is ui.UIBoard:
                    # Create list
                    ui_board_list = [obj, ui_board]
                    ui_board_list.sort(key=lambda ui_board: ui_board._board.full_name)
                    setattr(self.ui_tiles, board.name, ui_board_list)
                else:
                    raise ValueError("Unknown board attribute")
            else:
                # Add to the UI tiles object under the short name if it's the first
                setattr(self.ui_tiles, board.name, ui_board)


    def _remove_ui_board(self, board):
        # Note: not currently used
        delattr(self.ui_tiles, board.full_name)
        if board.name != board.custom_name:
            delattr(self.ui_tiles, board.custom_name)
        if isinstance(getattr(self.ui_tiles, board.name), list):
            getattr(self.ui_tiles, board.name).remove(board)
            if len(getattr(self.ui_tiles, board.name)) == 1:
                setattr(self.ui_tiles, board.name, getattr(self.ui_tiles, board.name)[0])
        else:
            delattr(self.ui_tiles, board.name)


    def _get_multi_tile_info(self, nodes):
        request_events = {}
        for node_id in nodes:
            event = threading.Event()
            request_events[node_id] = event
            self.send_get_tile_info_request(node_id, event)
        for node_id, event in request_events.items():
            response_received = event.wait(1.0)
            if not response_received:
                self.logger.error("Failed to get tile info: %d", node_id)
                self._remove_board(node_id)
                nodes.remove(node_id)
        return nodes

    def _get_multi_board_field_info(self, nodes):
        request_events = {}
        for node_id in nodes:
            for field_idx in range(0, self.tiles[node_id].num_fields):
                event = threading.Event()
                request_events[node_id, field_idx] = event
                self.send_get_field_info_request(node_id, field_idx, event)
        for (node_id, field_idx), event in request_events.items():
            response_received = event.wait(1.0)
            if not response_received:
                self.logger.error(f"Failed to get field info: {node_id}, field {field_idx}")


    def _get_multi_board_field_data(self, nodes):
        request_events = {}
        for node_id in nodes:
            for field_idx in range(0, self.tiles[node_id].num_fields):
                event = threading.Event()
                request_events[node_id, field_idx] = event
                field = self.tiles[node_id].fields[field_idx]
                self.send_get_fields_request(node_id, field.start_field, field.span, event)
        for (node_id, field_idx), event in request_events.items():
            response_received = event.wait(1.0)
            if not response_received:
                self.logger.error(f"Failed to get field data: {node_id}, field {field_idx}")

    def update_connected_tiles(self):
        # Get a list of connected nodes from the master
        prev_connected = set(self.connected_nodes[:])
        event = threading.Event()
        self.send_get_connected_nodes_request(event)
        response_received = event.wait(1.0)
        if not response_received:
            self.logger.error("Failed to get connected tiles")
            return
        self.logger.info("Connected nodes: %s", self.connected_nodes)
        connected = set(self.connected_nodes[:])
        new_nodes = connected - prev_connected
        disconnected_nodes = prev_connected - connected

        # Remove any nodes that are no longer connected
        for node_id in disconnected_nodes:
            self._remove_ui_board(self.tiles[node_id])
            self._remove_board(node_id)
        
        # Create tiles for new nodes
        for node_id in new_nodes:
            self._create_board(node_id)

        # Get board info for any new nodes
        new_nodes = self._get_multi_tile_info(new_nodes)
        self._get_multi_board_field_info(new_nodes)
        try:
            self._get_multi_board_field_data(new_nodes)
        except:
            pass

        # Create UI objects for new nodes
        self._create_ui_tiles(new_nodes)
        return self.ui_tiles

    def start(self):
        time.sleep(1)

        # Start Tx and Rx threads
        self.tx.start_process_tx_thread()
        self.rx.start_process_rx_thread()

        # First get the credits
        event = threading.Event()
        self.send_get_credits_request(event)
        response_received = event.wait(2.0)
        if not response_received:
            self.logger.error("Failed to get credits")
            return
        
        # Create all the tiles and field objects
        self.update_connected_tiles()
        return self.ui_tiles

    # def get_stream(self, dst_node, field_index):
    #     return self.tiles[dst_node].read_stream(field_index)

    def get_fields(self, dst_node, field_index, num_fields, wait_time=0.0):
        # try:
        self.logger.info(f"get_fields dst_node:{dst_node}, field_index:{field_index}, num_fields:{num_fields}")
        response_event = None
        if wait_time > 0.0:
            response_event = threading.Event()
        request = self.send_get_fields_request(dst_node, field_index, num_fields, response_event)
        if wait_time > 0.0:
            response_received = response_event.wait(wait_time)
            if not response_received:
                tmp_board = self.tiles[dst_node]
                field = tmp_board.get_field_from_flat_index(field_index)
                self.logger.error("%s:%s - Get fields timed-out!", tmp_board.name, field.name)
        # except Exception as e:
        #     self.logger.exception("An error occurred {e}")
    
    def set_fields(self, dst_node, field_index, num_fields, wait_time=0.0):
        # try:
        self.logger.info(f"set_fields dst_node:{dst_node}, field_index:{field_index}, num_fields:{num_fields}")
        response_events = []
        packets = self.tiles[dst_node].convert_field_data_to_packets(field_index, num_fields)
        for packet_start_field, packet_num_fields, packet in packets:
            if len(packets) > 1:
                self.logger.info(f"Multiple packets {packet_start_field}, {packet_num_fields}")
            assert(packet_num_fields > 0)
            response_event = None
            if wait_time > 0.0:
                response_event = threading.Event()
                response_events.append(response_event)
            self.send_set_fields_request(dst_node, packet_start_field, packet_num_fields, packet, response_event)
        
        if wait_time > 0.0:
            for response_event in response_events:
                response_received = response_event.wait(wait_time)
                if not response_received:
                    tmp_board = self.tiles[dst_node]
                    field = tmp_board.get_field_from_flat_index(field_index)
                    self.logger.error("%s:%s - Set fields timed-out!", tmp_board.name, field.name)
        # except Exception as e:
        #     self.logger.exception("An error occurred {e}")


    def print_tiles(self):
        print("Tiles:")
        for name, ui_board in self.ui_tiles.__dict__.items():
            if type(ui_board) is ui.UIBoard:
                if name is not ui_board._board.full_name:
                    print(name)
            elif type(ui_board) is list:
                if type(ui_board[0]) is ui.UIBoard:
                    print(f"{name}[{len(ui_board)}]")

    def print_tiles_and_fields(self):
        for node_id in self.tiles:
            self.tiles[node_id].print()


