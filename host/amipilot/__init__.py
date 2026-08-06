"""AmiPilot host-side client (phase 0.3, growing).

Currently: the wire-protocol client (`amipilot.wire`) speaking
server/WIRE.md's framing over any byte-stream transport. The object API
and pytest plugin land on top of this.
"""

from .wire import Reply, WireClient, WireError, ProtocolMismatch

__all__ = ["Reply", "WireClient", "WireError", "ProtocolMismatch"]
