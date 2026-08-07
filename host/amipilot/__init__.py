"""AmiPilot host-side client (phase 0.3, growing).

- `amipilot.wire` -- the transport-level client (server/WIRE.md framing)
  over any byte-stream transport.
- `amipilot.model` -- parses the TREE text format into Window/Gadget.
- `amipilot.client` -- `Amipilot`, the object API on top of both: the
  thing test code actually imports.

The pytest plugin (emulator-booting fixtures) lands on top of this.
"""

from .client import ActionFailed, Amipilot, AmipilotError, CommandError, NotFound
from .fs import FsEntry, FsParseError
from .menu import Menu, MenuItem, MenuParseError, MenuStrip
from .model import Gadget, TreeParseError, Window
from .screen import Screen, ScreenParseError
from .wire import ProtocolMismatch, Reply, ServerInfo, WireClient, WireError

__all__ = [
    "ActionFailed",
    "Amipilot",
    "AmipilotError",
    "CommandError",
    "FsEntry",
    "FsParseError",
    "Gadget",
    "Menu",
    "MenuItem",
    "MenuParseError",
    "MenuStrip",
    "NotFound",
    "ProtocolMismatch",
    "Reply",
    "Screen",
    "ScreenParseError",
    "ServerInfo",
    "TreeParseError",
    "WireClient",
    "WireError",
    "Window",
]
