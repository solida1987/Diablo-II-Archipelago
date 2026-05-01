"""Release locations one-at-a-time on a local Archipelago server.

Connects to ws://127.0.0.1:38281 using player slot, sends LocationChecks
packets directly. Avoids using `!admin /send_location` (which needs
remote admin enabled) — just emulates the bridge for one location at
a time.

Usage:
    py -3 ap_release.py <slot> <password> <loc_id> [<loc_id> ...]

If <password> is blank, pass "" as the second arg. Each loc_id is sent
as a separate LocationChecks packet so the AP server records them in
order. After each, the script polls for ReceivedItems for 2 seconds
before moving to the next.

NOTE: connecting under the same slot kicks the bridge — but only once
(the bridge auto-reconnects after this script exits). For a clean run,
use a SEPARATE slot if you have one in your YAML; otherwise plan on
the bridge briefly bouncing.
"""
import asyncio
import json
import sys
import os

import websockets


async def run(slot: str, password: str, loc_ids: list[int]):
    uri = "ws://127.0.0.1:38281"
    print(f"Connecting to {uri} as {slot!r}...", flush=True)
    async with websockets.connect(uri, max_size=2**24) as ws:
        # 1) Read RoomInfo
        room_msg = json.loads(await ws.recv())
        print(f"<-- {room_msg[0]['cmd']}", flush=True)

        # 2) Connect packet
        connect_pkt = [{
            "cmd": "Connect",
            "game": "Diablo II Archipelago",
            "name": slot,
            "password": password,
            "uuid": "release-helper-script",
            "version": {"major": 0, "minor": 5, "build": 0, "class": "Version"},
            "tags": ["AP"],
            "items_handling": 0b111,  # full handling
            "slot_data": False,
        }]
        await ws.send(json.dumps(connect_pkt))
        print(f"--> Connect (slot={slot})", flush=True)

        # 3) Wait for Connected (or ConnectionRefused)
        async for raw in ws:
            packets = json.loads(raw)
            for pkt in packets:
                cmd = pkt.get("cmd")
                print(f"<-- {cmd}", flush=True)
                if cmd == "ConnectionRefused":
                    print(f"REFUSED: {pkt.get('errors')}", flush=True)
                    return
                if cmd == "Connected":
                    print(f"   slot={pkt['slot']} team={pkt['team']} "
                          f"missing_locations={len(pkt.get('missing_locations', []))} "
                          f"checked_locations={len(pkt.get('checked_locations', []))}",
                          flush=True)
                    # Send each location one at a time
                    for i, loc_id in enumerate(loc_ids, 1):
                        pkt_out = [{"cmd": "LocationChecks", "locations": [loc_id]}]
                        await ws.send(json.dumps(pkt_out))
                        print(f"\n--> LocationChecks [{i}/{len(loc_ids)}] loc={loc_id}",
                              flush=True)
                        # Wait for echoed ReceivedItems / RoomUpdate
                        try:
                            for _ in range(8):  # up to ~2s at 250ms each
                                resp = await asyncio.wait_for(ws.recv(), timeout=0.25)
                                rps = json.loads(resp)
                                for r in rps:
                                    rcmd = r.get("cmd")
                                    if rcmd == "ReceivedItems":
                                        items = r.get("items", [])
                                        for it in items:
                                            print(f"   <-- ReceivedItems item={it.get('item')} "
                                                  f"location={it.get('location')} "
                                                  f"player={it.get('player')}",
                                                  flush=True)
                                    elif rcmd == "RoomUpdate":
                                        print(f"   <-- RoomUpdate "
                                              f"checked={len(r.get('checked_locations', []))}",
                                              flush=True)
                                    elif rcmd == "PrintJSON":
                                        # Server chat log of the check
                                        text = "".join(p.get("text", "")
                                                       for p in r.get("data", []))
                                        print(f"   <-- {text.strip()}", flush=True)
                                    else:
                                        print(f"   <-- {rcmd}", flush=True)
                        except asyncio.TimeoutError:
                            pass
                    print("\nDone — disconnecting.", flush=True)
                    return


def main():
    if len(sys.argv) < 4:
        print("Usage: py -3 ap_release.py <slot> <password> <loc_id> [<loc_id> ...]",
              flush=True)
        print("       password may be empty: pass \"\"", flush=True)
        sys.exit(2)
    slot = sys.argv[1]
    password = sys.argv[2]
    loc_ids = [int(x) for x in sys.argv[3:]]
    asyncio.run(run(slot, password, loc_ids))


if __name__ == "__main__":
    main()
