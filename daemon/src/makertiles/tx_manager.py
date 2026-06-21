'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import collections
import threading

from . import field_protocol


CREDITS_ENABLED = True

# Requests are coalesced into a single write until adding the next one would push
# the batch past this many bytes, at which point the batch is flushed.
MAX_TX_BATCH_BYTES = 100


class TxManager:

    def __init__(self, connection, logger, max_requests: int = 250):
        self.connection = connection
        self.logger = logger
        self._max_requests = max_requests

        # The one lock guarding everything below; also the Tx thread's wait point.
        self._cond = threading.Condition()

        self._pending = collections.deque()        # requests waiting to be sent
        self._free_ids = list(range(1, max_requests + 1))  # unused request ids
        self._outstanding = {}                     # id -> request awaiting a response

        # Flow-control credits. Seeded with 1 so the very first request (asking
        # the device how many credits exist) can go out before we know the real
        # number. set_credits tops this up once the device replies.
        self._credits = 1
        self._max_credits = None
        self._credits_initialized = False

    def start_process_tx_thread(self):
        self.writer_thread = threading.Thread(target=self._process_tx, daemon=True)
        self.writer_thread.start()

    ################ Shared-state helpers (call while holding self._cond) #######

    def _can_send(self) -> bool:
        return bool(self._pending) and self._credits > 0 and bool(self._free_ids)

    def _reserve(self) -> field_protocol.Request:
        """Pop one request, spend a credit and an id, record it as outstanding."""
        request = self._pending.popleft()
        self._credits -= 1
        request.id = self._free_ids.pop()
        field_protocol.set_id_in_packet(request.packet, request.id)
        self._outstanding[request.id] = request
        # A producer may be blocked waiting for room in _pending.
        self._cond.notify_all()
        return request

    ################ Credits and ids ###########################################
    # Spent by the Tx thread, returned by the Rx thread.

    def set_credits(self, num_rx_credits: int, num_tx_credits: int) -> None:
        max_credits = num_tx_credits + num_rx_credits - 2
        with self._cond:
            if self._credits_initialized:
                # The device reports its buffer size once at start-up; a second
                # message would over-grant credits and break flow control.
                self.logger.warning("Ignoring duplicate max buffer credits message (already set to %s)",
                                    self._max_credits)
                return
            self._credits_initialized = True
            self._max_credits = max_credits
            self._credits += max_credits - 1
            self.logger.info(f"Setting max credits to: {max_credits}")
            self._cond.notify_all()

    def return_credits(self, returned_rx_credits: int, returned_tx_credits: int) -> None:
        if not CREDITS_ENABLED or returned_tx_credits <= 0:
            return
        with self._cond:
            if self._max_credits is None:
                return
            if self._credits <= self._max_credits:
                self._credits += returned_tx_credits
                self._cond.notify_all()

    def get_matching_request(self, id: int) -> field_protocol.Request:
        with self._cond:
            return self._outstanding.get(id)

    def return_id(self, id: int) -> None:
        with self._cond:
            # pop (not del) so a stray double-return can't raise; remove from the
            # outstanding map before recycling the id so it can't be handed out
            # and re-inserted before we've cleaned up.
            if self._outstanding.pop(id, None) is not None:
                self._free_ids.append(id)
                self._cond.notify_all()

    ################ Internal Tx thread ########################################

    def _tx_send(self, data, requests):
        self.logger.info(f"Tx: {len(requests)} requests")
        # write() returns the number of bytes written, 0 if the device wasn't
        # ready (retry), or None if the port failed and was closed (give up so
        # we don't spin forever on a dead link).
        while True:
            bytes_written = self.connection.write(bytes(data))
            if bytes_written is None:
                self.logger.error("Write failed, dropping batch of %d requests", len(requests))
                return
            if bytes_written > 0:
                break

        # TODO: we can't tell if the packet was sent or not, we need to handle this in the future
        for request in requests:
            self.logger.info("Tx: " + field_protocol.request_to_string(request))
            if request.command == field_protocol.Command.SENDING_FIELDS:
                # A "set" has no data response, so retire it as soon as it's on the wire.
                self.return_id(request.id)
                if request.response_event is not None:
                    request.response_event.set()

    def _process_tx(self) -> None:
        data_to_send = bytearray()
        requests_to_send = []
        try:
            while True:
                reserved = None
                with self._cond:
                    # If a batch is in hand but we can't extend it right now,
                    # leave the lock and flush it. Otherwise wait until we can
                    # send, then reserve one request.
                    if not (requests_to_send and not self._can_send()):
                        self._cond.wait_for(self._can_send)
                        reserved = self._reserve()

                if reserved is not None:
                    # Flush first if this packet would overflow the batch.
                    if requests_to_send and len(data_to_send) + len(reserved.packet) > MAX_TX_BATCH_BYTES:
                        self._tx_send(data_to_send, requests_to_send)
                        data_to_send = bytearray()
                        requests_to_send = []
                    data_to_send += bytearray(reserved.packet)
                    requests_to_send.append(reserved)
                    continue

                # Starved mid-batch: send what we have, then loop back to wait.
                if requests_to_send:
                    self._tx_send(data_to_send, requests_to_send)
                    data_to_send = bytearray()
                    requests_to_send = []
        except Exception as e:
            self.logger.exception(f"An error occurred {e}")
            raise

    ################ Called by external producer threads #######################

    def send_request(self, dst_node: int, command: field_protocol.Command, field_index: int,
                     num_fields: int, response_event=None, data: bytes = None):
        if data is not None:
            assert num_fields > 0
        request = field_protocol.Request(dst_node, command, field_index, num_fields, data, response_event)
        request.packet = field_protocol.request_to_packet(request)
        with self._cond:
            # Bounded backpressure: wait briefly for room, then drop.
            if not self._cond.wait_for(lambda: len(self._pending) < self._max_requests, timeout=2):
                self.logger.info("Queue full, request failed")
                return
            self._pending.append(request)
            self._cond.notify_all()