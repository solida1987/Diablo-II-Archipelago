"""Connect briefly to the AP server, ask !checked and !missing, print
the server's text replies. Used to independently verify which locations
the server has on record without taking it from script-side state."""
import asyncio
import json
import sys
import websockets


async def run(slot: str, password: str):
    uri = "ws://127.0.0.1:38281"
    print(f"Connecting to {uri} as {slot!r}...", flush=True)
    async with websockets.connect(uri, max_size=2**24) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{
            "cmd": "Connect",
            "game": "Diablo II Archipelago",
            "name": slot,
            "password": password,
            "uuid": "status-query",
            "version": {"major": 0, "minor": 5, "build": 0, "class": "Version"},
            "tags": ["AP"],
            "items_handling": 0b111,
            "slot_data": False,
        }]))
        async for raw in ws:
            for pkt in json.loads(raw):
                if pkt.get("cmd") == "ConnectionRefused":
                    print(f"REFUSED: {pkt.get('errors')}", flush=True); return
                if pkt.get("cmd") == "Connected":
                    print(f"checked_locations = {len(pkt.get('checked_locations', []))}",
                          flush=True)
                    print(f"missing_locations = {len(pkt.get('missing_locations', []))}",
                          flush=True)
                    print(f"checked IDs = {sorted(pkt.get('checked_locations', []))[:30]}",
                          flush=True)
                    # Ask !checked via Say
                    await ws.send(json.dumps([{"cmd": "Say", "text": "!checked"}]))
                    try:
                        for _ in range(20):
                            r = json.loads(await asyncio.wait_for(ws.recv(), 0.4))
                            for p in r:
                                if p.get("cmd") == "PrintJSON":
                                    txt = "".join(x.get("text", "")
                                                  for x in p.get("data", []))
                                    if txt.strip():
                                        print(f"SERVER REPLY: {txt.strip()}", flush=True)
                    except asyncio.TimeoutError:
                        pass
                    return


if __name__ == "__main__":
    asyncio.run(run(sys.argv[1] if len(sys.argv) > 1 else "solida",
                    sys.argv[2] if len(sys.argv) > 2 else ""))
