"""Vista en vivo (B5): sesiones, límites, fin de sesión y reparto de cuadros por WebSocket."""

import asyncio
import time
from datetime import UTC, datetime, timedelta

import pytest
from httpx import AsyncClient
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

from app.models.node import Node
from app.models.stream import StreamSession
from app.stream.hub import Viewer
from tests.conftest import TOKEN_CAM, TOKEN_CAM2, USER, jpeg

S = "/api/v1/stream/sessions"
FRAME = jpeg(320, 240)


async def online(db, *node_ids: str, state: str = "operational") -> None:
    for nid in node_ids:
        db.add(Node(node_id=nid, kind=nid.split("-")[0], node_state=state))
    await db.commit()


async def mark(db, node_id: str, state: str) -> None:
    n = await db.get(Node, node_id)
    if n is None:
        db.add(Node(node_id=node_id, kind="cam", node_state=state))
    else:
        n.node_state = state
    await db.commit()


async def test_create_requires_online_camera(client, db):
    assert (await client.post(S, headers=USER, json={"node_id": "cam-09"})).status_code == 404
    await online(db, "ctrl-01")
    assert (await client.post(S, headers=USER, json={"node_id": "ctrl-01"})).status_code == 404
    await mark(db, "cam-01", "offline")  # cam-01 existe (token de .env) pero está caída
    r = await client.post(S, headers=USER, json={"node_id": "cam-01"})
    assert r.status_code == 409 and r.json()["code"] == "node_offline"
    # un nodo no puede pedir sesiones
    cam = {"Authorization": f"Bearer {TOKEN_CAM}"}
    assert (await client.post(S, headers=cam, json={"node_id": "cam-01"})).status_code == 403


async def test_create_clamps_limits_and_sends_start_stream(client, app, db):
    await mark(db, "cam-01", "operational")
    await client.put("/api/v1/config/nodes/cam-01/override", headers=USER,
                     json={"stream_resolution_max": "qvga", "stream_fps_max": 5})
    r = await client.post(S, headers=USER,
                          json={"node_id": "cam-01", "resolution": "vga", "fps_max": 30,
                                "ttl_s": 3000})
    assert r.status_code == 201, r.text
    body = r.json()
    sid = body["session_id"]
    assert (body["resolution"], body["fps_max"], body["ttl_s"]) == ("qvga", 5, 600)
    assert body["stream_state"] == "requested" and body["requested_by"] == "dev"
    assert body["ws_view_url"] == f"ws://test/api/v1/stream/{sid}/view"
    assert body["ws_publish_url"] == f"/api/v1/stream/{sid}/publish"  # relativa: sin API_PUBLIC_URL
    assert len(body["token"]) >= 30

    cmds = app.state.hub.send_cmd
    assert cmds == [{"node_id": "cam-01", "action": "start_stream", "cmd_id": f"st-{sid}",
                     "params": {"session_id": sid, "ws_publish_url": body["ws_publish_url"],
                                "resolution": "qvga", "fps_max": 5, "ttl_s": 600}}]

    # otra pantalla pide la misma cámara: comparte la sesión, sin otro start_stream
    r2 = await client.post(S, headers=USER, json={"node_id": "cam-01"})
    assert r2.status_code == 200 and r2.json()["session_id"] == sid
    assert r2.json()["token"] == body["token"] and len(cmds) == 1


async def test_session_limit(client, db, settings):
    settings.stream_max_sessions = 1
    await mark(db, "cam-01", "operational")
    await mark(db, "cam-02", "operational")
    assert (await client.post(S, headers=USER, json={"node_id": "cam-01"})).status_code == 201
    r = await client.post(S, headers=USER, json={"node_id": "cam-02"})
    assert r.status_code == 409 and r.json()["code"] == "stream_limit"


async def test_delete_sends_stop_and_persists(client, app, db, maker):
    await mark(db, "cam-01", "operational")
    sid = (await client.post(S, headers=USER, json={"node_id": "cam-01"})).json()["session_id"]
    assert (await client.delete(f"{S}/{sid}", headers=USER)).status_code == 204
    assert app.state.hub.send_cmd[-1]["action"] == "stop_stream"
    assert app.state.hub.send_cmd[-1]["params"] == {"session_id": sid}

    g = (await client.get(f"{S}/{sid}", headers=USER)).json()
    assert g["stream_state"] == "ended" and g["end_reason"] == "stopped_by:dev"
    assert g["ended_at"].endswith("Z")
    assert (await client.delete(f"{S}/{sid}", headers=USER)).status_code == 204  # idempotente
    assert (await client.get(f"{S}/s-nada", headers=USER)).status_code == 404

    items = (await client.get(S, headers=USER, params={"node_id": "cam-01"})).json()["items"]
    assert [i["session_id"] for i in items] == [sid]
    assert (await client.get(S, headers=USER, params={"live": True})).json()["items"] == []


async def test_rejected_ack_ends_with_error(client, app, db):
    await mark(db, "cam-01", "operational")
    sid = (await client.post(S, headers=USER, json={"node_id": "cam-01"})).json()["session_id"]
    hub = app.state.hub
    await hub.handle_ack("cam-02", {"cmd_id": f"st-{sid}", "result": "rejected"})  # ajeno
    assert hub.get(sid) is not None
    await hub.handle_ack("cam-01", {"cmd_id": f"st-{sid}", "result": "accepted"})
    assert hub.get(sid) is not None
    await hub.handle_ack("cam-01", {"cmd_id": f"st-{sid}", "result": "rejected",
                                    "reason": "unsupported_action"})
    g = (await client.get(f"{S}/{sid}", headers=USER)).json()
    assert g["stream_state"] == "error" and g["end_reason"] == "rejected:unsupported_action"
    assert hub.send_cmd[-1]["action"] == "start_stream"  # no manda stop a quien rechazó


async def test_broker_down_returns_503(client, app, db):
    await mark(db, "cam-01", "operational")
    app.state.hub.send_cmd.ok = False
    r = await client.post(S, headers=USER, json={"node_id": "cam-01"})
    assert r.status_code == 503 and r.json()["code"] == "unavailable"
    assert app.state.hub.live_count() == 0


@pytest.mark.parametrize("case", ["ttl", "node_timeout", "no_viewers"])
async def test_reaper(client, app, db, maker, case):
    await mark(db, "cam-01", "operational")
    sid = (await client.post(S, headers=USER, json={"node_id": "cam-01"})).json()["session_id"]
    hub = app.state.hub
    s = hub.get(sid)
    if case == "ttl":
        s.expires_at = datetime.now(UTC) - timedelta(seconds=1)
    elif case == "node_timeout":
        s.created_mono -= 60
    else:
        s.state = "active"
        s.idle_since = time.monotonic() - 60
    await hub.reap()
    async with maker() as db2:
        row = await db2.get(StreamSession, sid)
    assert row.end_reason == case
    assert row.stream_state == ("error" if case == "node_timeout" else "ended")


def test_slow_viewer_keeps_only_last_frame():
    v = Viewer(ws=None)  # type: ignore[arg-type]
    v.push(b"1")
    v.push(b"2")
    v.push(b"3")
    assert v.frame == b"3" and v.dropped == 2


# ---------------------------------------------------------------- WebSocket de punta a punta

async def _close_code(ws) -> int:
    try:
        await asyncio.wait_for(ws.recv(), 2)
    except ConnectionClosed as e:
        return e.rcvd.code if e.rcvd else -1
    return 0


async def test_publish_and_view(server, app, db):
    await mark(db, "cam-01", "operational")
    async with AsyncClient(base_url=f"http://{server}") as http:
        r = await http.post(S, headers=USER, json={"node_id": "cam-01"})
        body = r.json()
    sid, tok = body["session_id"], body["token"]
    base = f"ws://{server}/api/v1/stream/{sid}"
    assert body["ws_view_url"] == f"{base}/view"
    cam1 = {"Authorization": f"Bearer {TOKEN_CAM}"}

    # token de otro nodo, o sin token: no publica
    async with connect(f"{base}/publish",
                       additional_headers={"Authorization": f"Bearer {TOKEN_CAM2}"}) as ws:
        assert await _close_code(ws) == 4403
    async with connect(f"{base}/publish") as ws:
        assert await _close_code(ws) == 4401
    async with connect(f"{base}/view?token=malo") as ws:
        assert await _close_code(ws) == 4401

    hub = app.state.hub
    async with connect(f"{base}/publish", additional_headers=cam1) as pub:
        await pub.send(b"no es jpeg")  # se ignora
        await pub.send(FRAME)
        for _ in range(100):
            if hub.get(sid).frames:
                break
            await asyncio.sleep(0.01)
        assert hub.get(sid).state == "active"

        async with connect(f"{base}/view?token={tok}") as view:
            assert await asyncio.wait_for(view.recv(), 2) == FRAME  # el último, enseguida
            second = FRAME + b"\x00"
            await pub.send(second)
            assert await asyncio.wait_for(view.recv(), 2) == second

            async with AsyncClient(base_url=f"http://{server}") as http:
                g = (await http.get(f"{S}/{sid}", headers=USER)).json()
            assert g["viewers"] == 1 and g["frames"] == 2 and g["publisher_connected"]

            await pub.close()  # el nodo corta: la sesión termina y el espectador se entera
            assert await _close_code(view) == 1000

    async with AsyncClient(base_url=f"http://{server}") as http:
        g = (await http.get(f"{S}/{sid}", headers=USER)).json()
    assert g["stream_state"] == "ended" and g["end_reason"] == "publisher_closed"
    assert g["frames"] == 2
    async with connect(f"{base}/view?token={tok}") as view:  # sesión terminada
        assert await _close_code(view) == 4410


async def test_delete_closes_websockets(server, app, db):
    await mark(db, "cam-01", "operational")
    async with AsyncClient(base_url=f"http://{server}") as http:
        body = (await http.post(S, headers=USER, json={"node_id": "cam-01"})).json()
        base = f"ws://{server}/api/v1/stream/{body['session_id']}"
        async with connect(f"{base}/publish",
                           additional_headers={"Authorization": f"Bearer {TOKEN_CAM}"}) as pub, \
                connect(f"{base}/view?token={body['token']}") as view:
            await pub.send(FRAME)
            assert await asyncio.wait_for(view.recv(), 2) == FRAME
            await http.delete(f"{S}/{body['session_id']}", headers=USER)
            assert await _close_code(view) == 1000
            assert await _close_code(pub) == 1000
