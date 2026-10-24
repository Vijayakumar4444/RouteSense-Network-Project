"""Correlate ARP PCAPs with PHY and terminal-drop events (stdlib only)."""
import csv
import socket
import struct
import sys
from collections import Counter
from pathlib import Path

def arp_frames(path):
    with path.open("rb") as f:
        header = f.read(24)
        assert header[:4] == b"\xd4\xc3\xb2\xa1" and struct.unpack("<I", header[20:])[0] == 105
        while raw := f.read(16):
            sec, usec, size, _ = struct.unpack("<IIII", raw)
            packet = f.read(size)
            # Current simulation uses non-QoS three-address 802.11 data headers.
            if len(packet) >= 60 and packet[24:32] == bytes.fromhex("aaaa030000000806"):
                yield (sec + usec / 1e6, struct.unpack("!H", packet[38:40])[0],
                       socket.inet_ntoa(packet[46:50]), socket.inet_ntoa(packet[56:60]))

root = Path(sys.argv[1])
for protocol in ("routesense", "routesense-aodv"):
    folder = root / f"{protocol}-static-cross"
    events = list(csv.DictReader((folder / "events.csv").open()))
    print(protocol, "drop reasons", dict(Counter(
        (e["event"], e["reason"]) for e in events if "drop" in e["event"])))
    requests = {}
    for node in (10, 11):
        pcap = next(folder.glob(f"*-{node}-0.pcap"))
        frames = list(arp_frames(pcap))
        requests[node] = [(t, src, dst) for t, op, src, dst in frames
                          if op == 1 and src == f"10.1.1.{node+1}" and dst == f"10.1.1.{22-node}"]
        replies = [(t, src, dst) for t, op, src, dst in frames
                   if op == 2 and dst == f"10.1.1.{node+1}"]
        print("node", node, "reciprocal ARP requests", requests[node][:5], "received replies", replies[:5])
    if protocol == "routesense":
        assert requests[10][0][0] == requests[11][0][0] == 2.00205
        for node, peer in ((10, 11), (11, 10)):
            tx = next(e for e in events if e["node"] == str(node) and
                      e["event"] == "phy_tx" and e["time"] == "2.00205")
            assert any(e["node"] == str(peer) and e["event"] == "phy_rx_drop"
                       and e["uid"] == tx["uid"] and e["reason"] == "TXING" for e in events)
print("ARP/PHY correlation PASS")
