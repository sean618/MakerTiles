'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import threading

from . import field_protocol
from . import tx_manager

class RxManager:
    def __init__(self, connection, logger, tx: tx_manager.TxManager, bm: 'board_manager.BoardManager'):
        self.connection = connection
        self.logger = logger
        self.tx_manager = tx
        self.board_manager = bm
        
    def start_process_rx_thread(self):
        self.writer_thread = threading.Thread(target=self._process_rx, daemon=True)
        self.writer_thread.start()

    # Internal thread
    def _process_rx(self):
        try:
            while True:
                for raw_packet in self.connection.read():
                # while True:
                    packet = memoryview(raw_packet) #self.connection.read())
                    self._process_rx_packet(packet)
        except Exception as e:
            self.logger.exception(f"An error occurred {e}")
            raise

    def _process_rx_packet(self, packet):
        # print(f"Received command: {command}")
        try:
            response = field_protocol.Response(packet)
            return_rx_credits = response.return_rx_credits
            return_tx_credits = response.return_tx_credits
            src_node = response.src_node
            request_id = response.request_id
            command  = response.command
            num_fields  = response.num_fields
            field_index  = response.field_index
            data  = response.data

            if return_rx_credits > 0 or return_tx_credits > 0:
                self.tx_manager.return_credits(return_rx_credits, return_tx_credits)

            # An unrecognized opcode is a valid-but-unknown command (e.g. newer
            # firmware), not a corrupt packet. Log it distinctly and move on
            # instead of letting it fall through to the parse-error handler.
            if command is None:
                self.logger.warning(
                    "Unknown command byte %d from node %d (request %d), ignoring",
                    response.command_byte, src_node, request_id)
                return

            if command == field_protocol.Command.RETURNING_CREDITS:
                return
            
            self.logger.info("Rx: " + field_protocol.response_to_string(response))
            
            matching_request = self.tx_manager.get_matching_request(request_id)

            if command == field_protocol.Command.SENDING_FIELDS:
                if matching_request is not None:
                    if num_fields > matching_request.num_fields:
                        self.logger.warning("Received num_fields %d exceeds stored value %d (request %d)",
                                            num_fields, matching_request.num_fields, request_id)
                        return
                self.board_manager.update_field_data_from_packet(src_node, field_index, num_fields, data)

                if matching_request is not None:
                    matching_request.num_fields = matching_request.num_fields - num_fields
                    if matching_request.num_fields == 0:
                        self.tx_manager.return_id(matching_request.id)
                        if matching_request.response_event is not None:
                            matching_request.response_event.set()

            else:
                if command == field_protocol.Command.SENDING_MAX_BUFFER_CREDITS:
                    self.tx_manager.set_credits(data[0], data[1])

                elif command == field_protocol.Command.SENDING_TILE_INFO:
                    if field_index != 0 or num_fields != 4:
                        self.logger.warning("SENDING_TILE_INFO: unexpected field_index %d / num_fields %d",
                                            field_index, num_fields)
                        return
                    self.board_manager.update_tile_info_from_packet(src_node, data)

                elif command == field_protocol.Command.SENDING_FIELD_INFO:
                    if field_index != 0 or num_fields != 7:
                        self.logger.warning("SENDING_FIELD_INFO: unexpected field_index %d / num_fields %d",
                                            field_index, num_fields)
                        return

                    if matching_request is None:
                        self.logger.warning("SENDING_FIELD_INFO: no outstanding request %d", request_id)
                        return
                    
                    self.board_manager.update_field_info_from_packet(src_node, matching_request.field_index, data)

                elif command == field_protocol.Command.SENDING_CONNECTED_NODES:
                    connected_nodes = []
                    for i in range(field_protocol.MAX_NODES):
                        byte_index = i // 8
                        bit_index = 7 - (i % 8)
                        if (data[byte_index] >> bit_index) & 0x1:
                            connected_nodes.append(i)
                    self.board_manager.update_connected_nodes_from_packet(connected_nodes)

                else:
                    # A recognized opcode with no rx handler (e.g. a request-only
                    # command echoed back). Not corruption, so warn and drop it.
                    self.logger.warning("No rx handler for command %s from node %d", command, src_node)
                    return

                if matching_request is not None:
                    self.tx_manager.return_id(matching_request.id)
                    if matching_request.response_event is not None:
                        # self.logger.info("Rx event set")
                        matching_request.response_event.set()
                # else:
                #     # TODO: this is a hack
        except Exception:
            self.logger.exception(f'Error processing rx packet, data:{packet.hex()}')


