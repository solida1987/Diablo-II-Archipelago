"""Release ALL missing locations for a slot in one big batch.

Connects, reads missing_locations from the Connected packet, sends them
ALL in a single LocationChecks message, then waits ~10 seconds for
RoomUpdate / ReceivedItems echoes before disconnecting.

Usage: py -3 ap_release_all.py <slot> <password>
"""
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
            "uuid": "release-all",
            "version": {"major": 0, "minor": 5, "build": 0, "class": "Version"},
            "tags": ["AP"],
            "items_handling": 0b111,
            "slot_data": False,
        }]))
        async for raw in ws:
            for pkt in json.loads(raw):
                cmd = pkt.get("cmd")
                if cmd == "ConnectionRefused":
                    print(f"REFUSED: {pkt.get('errors')}", flush=True); return
                if cmd == "Connected":
                    missing = sorted(pkt.get("missing_locations", []))
                    checked = sorted(pkt.get("checked_locations", []))
                    print(f"checked={len(checked)} missing={len(missing)}", flush=True)
                    if not missing:
                        print("Nothing to release.", flush=True); return
                    # Send all in ONE LocationChecks packet
                    await ws.send(json.dumps([{
                        "cmd": "LocationChecks",
                        "locations": missing,
                    }]))
                    print(f"--> Sent LocationChecks with {len(missing)} locations",
                          flush=True)
                    # Drain responses for 12s (ReceivedItems may come in
                    # multiple chunks for huge batches)
                    items_count = 0
                    rooms_count = 0
                    try:
                        end = asyncio.get_event_loop().time() + 12.0
                        while asyncio.get_event_loop().time() < end:
                            timeout = max(0.1, end - asyncio.get_event_loop().time())
                            resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
                            for r in json.loads(resp):
                                rcmd = r.get("cmd")
                                if rcmd == "ReceivedItems":
                                    items_count += len(r.get("items", []))
                                elif rcmd == "RoomUpdate":
                                    rooms_count += 1
                                    cl = r.get("checked_locations", [])
                                    if cl:
                                        print(f"   <-- RoomUpdate +{len(cl)} checked",
                                              flush=True)
                    except asyncio.TimeoutError:
                        pass
                    print(f"\nTotal items received: {items_count}", flush=True)
                    print(f"Total RoomUpdate packets: {rooms_count}", flush=True)
                    print("Done — disconnecting.", flush=True)
                    return


if __name__ == "__main__":
    asyncio.run(run(sys.argv[1] if len(sys.argv) > 1 else "solida",
                    sys.argv[2] if len(sys.argv) > 2 else ""))
