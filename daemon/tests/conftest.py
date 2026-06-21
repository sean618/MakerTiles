'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import logging
import pytest


@pytest.fixture(autouse=True)
def fail_on_logged_error(caplog):
    """Treat any daemon ERROR logged during a test as a test failure.

    Production code deliberately swallows things like a field request timing
    out (see board_manager.BoardManager.get_fields) - at runtime a board going
    offline is normal and user code shouldn't see an exception, just a logged
    error and stale data. During tests, though, that same logged error means
    something is wrong, so surface it as a failure here instead of changing the
    production behaviour.

    The daemon logger propagates to the root logger, so caplog captures its
    records. We only inspect ERROR (and above), letting INFO chatter through.
    """
    yield
    errors = [r for r in caplog.records if r.levelno >= logging.ERROR]
    assert not errors, "daemon logged error(s) during test:\n" + "\n".join(
        f"  {r.name}: {r.getMessage()}" for r in errors
    )