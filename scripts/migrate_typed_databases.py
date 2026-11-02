#!/usr/bin/env python3
"""One-shot offline Store 4->5, Chat 1->2 and typed Bridge 4->5 migration.

Python standard library only. Not imported by the runtime. Never fetches media,
contacts a provider, admits replies, or starts/restarts OBCX. Run `plan` first.
Every message must convert: any error aborts; no skipping or clearing history.

Chat's SQL/document layout is unchanged between these two schema generations;
its documents, pending journals, epochs, controls, source IDs and cursors are
preserved verbatim. This does NOT recreate old live media/reply authority.
QQ historical media receives explicitly unavailable, process-unregistered
references; metadata/presentation survives, but old URLs are NOT imported.
Bridge converts only current typed admissions/member/album metadata and edit
intents. Receipts, pending attempts, retry payloads, command fences and retired
tables remain unchanged. Unsupported historical rows are never promoted.
Already-current Store/Chat/Bridge schemas are accepted without reconversion.

apply: consistent backups -> migrate separate copies -> integrity/count checks
-> replace originals. Originals are not modified during conversion. A journal
and backups remain if the process is interrupted between replacements.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import copy
from contextlib import closing
import hashlib
import json
import os
from collections import deque
from decimal import Decimal
from pathlib import Path
import re
import shutil
import sqlite3
import struct
import sys
import uuid


class MigrationError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise MigrationError(message)


def dumps(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def loads(value):
    return json.loads(value, parse_constant=lambda x: (_ for _ in ()).throw(
        MigrationError(f"nonfinite JSON number: {x}")))


def record(**fields):
    return {"schema": 1, **fields}


def alternative(index, value):
    return {"alternative": index, "value": value}


def identifier(value, maximum=1024):
    require(isinstance(value, str) and 0 < len(value.encode("utf-8")) <= maximum,
            "missing/oversized identity")
    require(not any(ord(c) < 32 or ord(c) == 127 for c in value), "control in identity")
    return value


def number(value, low=-(2**63), high=2**63 - 1):
    require(isinstance(value, (str, int)) and not isinstance(value, bool), "nonintegral identity")
    text = str(value)
    require(re.fullmatch(r"0|-?[1-9][0-9]*", text) is not None, "noncanonical decimal identity")
    require(low <= int(text) <= high, "identity out of range")
    return text


def opaque(surface, tag, kind, *components):
    fields = [surface, kind, *(identifier(str(x)) for x in components)]
    wire = b"".join(struct.pack(">I", len(x.encode("utf-8"))) + x.encode("utf-8") for x in fields)
    value = f"obcx1:{tag}:{wire.hex()}"
    require(len(value) <= 4096, "opaque identity exceeds bound")
    return value


def installation(platform, bot):
    require(platform in ("qq", "telegram"), "unsupported platform")
    return {"installation_id": identifier(bot, 256),
            "surface": "onebot11.qq" if platform == "qq" else "telegram.bot_api"}


def user(inst, native_id):
    maximum = 2**63 - 1 if inst["surface"] == "onebot11.qq" else 2**52 - 1
    return {"installation": inst, "id": opaque(inst["surface"], "user", "user",
                                              number(native_id, 1, maximum))}


def conversation(inst, kind, native_id, topic=None):
    qq = inst["surface"] == "onebot11.qq"
    kind = "group" if kind == "supergroup" else kind
    require(kind in ("group", "private", "channel") and not (qq and kind == "channel"),
            "unsupported conversation kind")
    native_id = number(native_id, 1 if qq or kind == "private" else -(2**52 - 1),
                       2**63 - 1 if qq else 2**52 - 1 if kind == "private" else -1)
    components = [native_id]
    if topic is not None:
        require(not qq and kind == "group", "topic on incompatible conversation")
        topic = number(topic, 1, 2**31 - 1)
        if topic != "1":
            kind = "topic"
            components.append(topic)
    return {"installation": inst, "id": opaque(inst["surface"], "conversation", kind, *components)}


def message_ref(conv, native_id):
    inst = conv["installation"]
    qq = inst["surface"] == "onebot11.qq"
    return {"conversation": conv, "id": opaque(inst["surface"], "message", "message",
            number(native_id, -(2**63) if qq else 1, 2**63 - 1 if qq else 2**31 - 1))}


def timestamp(milliseconds):
    require(isinstance(milliseconds, int) and -(2**63) <= milliseconds < 2**63,
            "invalid recorded timestamp")
    return record(unix_milliseconds=milliseconds)


def native_time(value):
    if value is None:
        return None
    milliseconds = Decimal(str(value)) * 1000
    require(milliseconds.is_finite() and milliseconds == milliseconds.to_integral_value(),
            "native time is not exact milliseconds")
    return timestamp(int(milliseconds))


def literal(text, styles=()):
    require(isinstance(text, str) and text and len(text.encode()) <= 65536, "invalid text")
    return alternative(0, record(content=text, styles=sorted(set(styles))))


def unsupported(kind, description=None):
    return alternative(2, record(category=identifier(kind, 128), description=description))


def metadata(data, telegram=False):
    result = record(mime_type=None, filename=None, size_bytes=None,
                    width=None, height=None, duration_ms=None)
    for key in (("file_size",) if telegram else ("file_size", "size")):
        try:
            result["size_bytes"] = int(number(data[key], 0, 2**64 - 1))
            break
        except (KeyError, MigrationError):
            pass
    for key in (("file_name",) if telegram else ("file_name", "name")):
        name = data.get(key)
        if isinstance(name, str) and name and not any(c in name for c in "/\\:?#") and name not in (".", ".."):
            result["filename"] = identifier(name)
            break
    for key in ("width", "height"):
        try:
            result[key] = int(number(data[key], 1, 2**32 - 1))
        except (KeyError, MigrationError):
            pass
    if telegram:
        mime = data.get("mime_type")
        if isinstance(mime, str) and len(mime) <= 256 and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*/[A-Za-z0-9][A-Za-z0-9.+_-]*", mime):
            result["mime_type"] = mime
        try:
            result["duration_ms"] = int(number(data["duration"], 0, (2**64 - 1)//1000))*1000
        except (KeyError, MigrationError):
            pass
        try:
            result["width"] = result["height"] = int(number(data["length"], 1, 2**32 - 1))
        except (KeyError, MigrationError):
            pass
    return result


def reply(source, target, observation, author=None):
    return record(source=source, reference=record(source=source, target=target,
                  observation_id=observation), author=author, relation=0,
                  availability=1 if author is not None else 0)


def base_message(row, inst, conv, kind, group, topic):
    return record(message=message_ref(conv, row["message_id"]),
                  sender=alternative(2, record()), sent_at=None,
                  received_at=timestamp(row["created_at"]), body=[], reply=None,
                  media_group=None, context=record(kind=kind, group_id=group,
                  topic_id=topic, topic_root_reply=False, sender_is_bot=None,
                  observed_self=None, sender_profile=None, edited_at=None), observations=[])


def native_archive(row):
    payload = loads(row["payload"])
    raw = loads(row["raw"]) if row["raw"] else payload
    pending = [raw, payload]
    for _ in range(24):
        if not pending:
            break
        value = pending.pop(0)
        if not isinstance(value, dict):
            continue
        if row["source_platform"] == "qq" and isinstance(value.get("message"), list) and "message_type" in value:
            return value
        if row["source_platform"] == "telegram" and isinstance(value.get("chat"), dict):
            return value
        pending.extend(value[k] for k in ("payload", "raw", "message", "edited_message", "channel_post", "edited_channel_post") if k in value)
    raise MigrationError("no unambiguous native message archive")


def qq_observation(kind, data):
    def face(value):
        return record(id=number(value["id"], 0), description=value.get("text"))
    if kind == "face":
        return alternative(0, face(data))
    if kind == "mface":
        return alternative(1, record(description=data.get("summary")))
    if kind in ("json", "app", "ark", "miniapp", "share"):
        return alternative(2, record(kind=("json", "app", "ark", "miniapp", "share").index(kind), metadata=data))
    if kind == "forward":
        return alternative(3, record(token=identifier(data["id"], 512)))
    if kind == "music":
        return alternative(4, record(title=data.get("title")))
    if kind == "node":
        content = data.get("content")
        parts = []
        form = 0 if "content" not in data else 3
        if isinstance(content, str):
            form, parts = 1, [alternative(0, record(content=content))]
        elif isinstance(content, list):
            form = 2
            for part in content:
                native_kind = identifier(part["type"], 80)
                if native_kind == "text":
                    parts.append(alternative(0, record(content=part["data"]["text"])))
                elif native_kind == "face":
                    parts.append(alternative(1, face(part["data"])))
                else:
                    parts.append(alternative(2, record(native_kind=native_kind)))
        return alternative(5, record(display_name=data.get("nickname"), form=form, parts=parts))
    return None


def normalize_qq(row, native, inst):
    kind = native["message_type"]
    require(kind in ("private", "group"), "unsupported QQ scope")
    group = number(native["group_id"], 1) if kind == "group" else ""
    peer = group or number(native["user_id"], 1)
    require(row["conversation_id"] == f"{kind}:{peer}", "QQ source/index mismatch")
    require(number(native["message_id"]) == row["message_id"], "QQ message/index mismatch")
    conv = conversation(inst, kind, peer)
    out = base_message(row, inst, conv, kind, group, None)
    if native.get("user_id") not in (None, "") and not native.get("anonymous"):
        out["sender"] = alternative(0, record(user=user(inst, native["user_id"])))
    if native.get("self_id") not in (None, ""):
        out["context"]["observed_self"] = user(inst, native["self_id"])
    out["sent_at"] = native_time(native.get("time"))
    segments = native["message"]
    single_reply = sum(s.get("type") == "reply" for s in segments) == 1
    for index, segment in enumerate(segments):
        kind, data = segment["type"], segment["data"]
        if kind in ("text", "at"):
            if kind == "text":
                span = literal(data["text"]) if data["text"] else None
            elif str(data["qq"]) == "all":
                span = alternative(3, record())
            else:
                span = alternative(1, record(user=user(inst, data["qq"])))
            if span is not None:
                if not out["body"] or out["body"][-1]["alternative"] != 0:
                    out["body"].append(alternative(0, []))
                out["body"][-1]["value"].append(span)
            continue
        if kind == "reply" and single_reply:
            out["reply"] = reply(out["message"], message_ref(conv, data["id"]), f"offline-migration:{row['id']}")
            continue
        if kind in ("image", "video", "record", "file", "mface"):
            # Structural references only: no Receipt is inserted into a process.
            # In particular, migration cannot turn archived URLs into authority.
            key = dumps([inst, row["conversation_id"], row["message_id"], index])
            token = f"{uuid.uuid5(uuid.NAMESPACE_URL, key)}:1:{uuid.uuid5(uuid.NAMESPACE_OID, key)}"
            family = "received-" + kind
            media = record(installation=inst, id=opaque(inst["surface"], "media", family, token))
            caption = [literal(data["summary"])] if kind == "mface" and data.get("summary") else []
            out["body"].append(alternative(1, record(media=media,
                kind={"image": 0, "mface": 0, "video": 2, "record": 4, "file": 5}[kind],
                metadata=metadata(data), identity=None, caption=caption)))
        else:
            safe = kind[:80] if re.fullmatch(r"[a-z0-9_-]+", kind) else "unknown"
            out["body"].append(unsupported("onebot11." + safe))
        observation = qq_observation(kind, data)
        if observation is not None:
            out["observations"].append(record(block_index=len(out["body"])-1, value=observation))
    return out


def tg_sender(inst, native):
    if "sender_chat" in native:
        chat = native["sender_chat"]
        return alternative(1, record(conversation=conversation(inst, chat["type"], chat["id"])))
    if "from" in native:
        return alternative(0, record(user=user(inst, native["from"]["id"])))
    return alternative(2, record())


def tg_rich(inst, native, text_key, entities_key):
    text = native[text_key]
    require(isinstance(text, str) and len(text.encode()) <= 65536, "invalid Telegram text")
    boundaries, offset = {0: 0}, 0
    for index, char in enumerate(text):
        offset += 2 if ord(char) > 65535 else 1
        boundaries[offset] = index + 1
    cuts, entities, mentions = {0, len(text)}, [], []
    for entity in native.get(entities_key, []):
        start, length = entity["offset"], entity["length"]
        require(type(start) is int and type(length) is int and length > 0 and start in boundaries and start+length in boundaries,
                "invalid Telegram UTF-16 entity")
        begin, end = boundaries[start], boundaries[start+length]
        entities.append((begin, end, entity))
        cuts.update((begin, end))
        if entity["type"] in ("mention", "text_mention"):
            mentions.append((begin, end))
    mentions.sort()
    require(all(a[1] <= b[0] for a, b in zip(mentions, mentions[1:])), "overlapping Telegram mentions")
    result, styled = [], False
    cuts = sorted(cuts)
    for begin, end in zip(cuts, cuts[1:]):
        active = [(a, b, e) for a, b, e in entities if a <= begin and end <= b]
        styles = sorted({{"bold": 0, "italic": 1, "code": 2, "pre": 2}[e["type"]]
                         for _, _, e in active if e["type"] in ("bold", "italic", "code", "pre")})
        mention = next(((a, b, e) for a, b, e in active if e["type"] in ("mention", "text_mention")), None)
        if mention:
            a, b, entity = mention
            styled |= bool(styles)
            if begin != a:
                continue
            if entity["type"] == "text_mention":
                result.append(alternative(1, record(user=user(inst, entity["user"]["id"]))))
            else:
                spelling = text[a:b]
                require(re.fullmatch(r"@[A-Za-z0-9_]{1,32}", spelling) is not None, "invalid Telegram mention")
                result.append(alternative(2, record(username=spelling[1:].lower())))
        else:
            result.append(literal(text[begin:end], styles))
    return result, styled


def normalize_telegram(row, native, inst):
    native = copy.deepcopy(native)
    for key in ("time", "self_id", "post_type", "message_type", "sub_type", "user_id", "message", "raw_message", "font", "group_id", "anonymous", "guild_id", "channel_id"):
        native.pop(key, None)  # common archive overlay, not Telegram content
    chat = native["chat"]
    kind = "group" if chat["type"] == "supergroup" else chat["type"]
    peer = number(chat["id"])
    require(row["conversation_id"] == "chat:" + peer, "Telegram source/index mismatch")
    require(number(native["message_id"]) == row["message_id"], "Telegram message/index mismatch")
    topic = int(number(native["message_thread_id"], 1, 2**31-1)) if native.get("is_topic_message", False) else None
    conv = conversation(inst, kind, peer, topic)
    out = base_message(row, inst, conv, kind, peer if kind == "group" else "", topic)
    out["sender"] = tg_sender(inst, native)
    out["sent_at"] = native_time(native.get("date"))
    out["context"]["edited_at"] = native_time(native.get("edit_date"))
    if "from" in native:
        profile = native["from"]
        out["context"]["sender_is_bot"] = profile.get("is_bot")
        out["context"]["sender_profile"] = record(**{k: profile.get(k) for k in ("first_name", "last_name", "username")})
    def rich(text_key, entities_key):
        value, styled = tg_rich(inst, native, text_key, entities_key)
        if styled:
            out["body"].append(unsupported("telegram.styled-mention"))
        return value
    if "text" in native:
        out["body"].append(alternative(0, rich("text", "entities")))
    media_key = next((k for k in ("photo", "animation", "video", "video_note", "audio", "voice", "document", "sticker") if k in native), None)
    if media_key:
        data = native[media_key][-1] if media_key == "photo" else native[media_key]
        family = "video-note" if media_key == "video_note" else media_key
        media_kind = {"photo": 0, "animation": 1, "video": 2, "video_note": 2, "audio": 3, "voice": 4, "document": 5, "sticker": 0}[media_key]
        components = [identifier(data["file_id"])]
        require(re.fullmatch(r"[A-Za-z0-9_-]+", components[0]) is not None, "invalid Telegram file identity")
        meta = metadata(data, telegram=True)
        if media_key == "sticker":
            family = "sticker-webm" if data.get("is_video") else "sticker-tgs" if data.get("is_animated") else "sticker-static"
            media_kind = 0 if family == "sticker-static" else 1
            meta["mime_type"] = {"sticker-static": "image/webp", "sticker-webm": "video/webm", "sticker-tgs": "application/x-tgsticker"}[family]
            if data.get("emoji"):
                components.append(data["emoji"])
        identity = None
        if data.get("file_unique_id"):
            identity = record(installation=inst, id=opaque(inst["surface"], "media_identity", "file-unique", data["file_unique_id"]))
        caption = rich("caption", "caption_entities") if "caption" in native else []
        out["body"].append(alternative(1, record(media=record(installation=inst,
            id=opaque(inst["surface"], "media", family, *components)), kind=media_kind,
            metadata=meta, identity=identity, caption=caption)))
    elif "caption" in native:
        out["body"].append(alternative(0, rich("caption", "caption_entities")))
    for key, label in (("sticker", "sticker"), ("video_note", "video-note")):
        if key in native:
            out["body"].append(unsupported("telegram." + label))
    for key in ("reply_markup", "show_caption_above_media", "has_media_spoiler", "effect_id", "reply_to_story", "quote"):
        if native.get(key, False) is not False:
            out["body"].append(unsupported("telegram." + key))
    for key in ("entities", "caption_entities"):
        for entity in native.get(key, []):
            if entity["type"] not in ("bold", "italic", "code", "pre", "mention", "text_mention", "bot_command"):
                out["body"].append(unsupported("telegram.entity", entity["type"][:128] or None))
    known = set("is_edited message_id message_thread_id is_topic_message from sender_chat sender_boost_count sender_business_bot date chat forward_origin is_automatic_forward reply_to_message external_reply quote reply_to_story via_bot edit_date has_protected_content is_from_offline media_group_id author_signature text entities link_preview_options effect_id animation audio document photo sticker video video_note voice caption caption_entities show_caption_above_media has_media_spoiler reply_markup business_connection_id".split())
    for key in native.keys() - known:
        out["body"].append(unsupported(key))
    if "edit_date" in native or native.get("is_edited"):
        out["body"].append(unsupported("telegram.edited"))
    if "media_group_id" in native:
        out["media_group"] = record(conversation=conv, id=opaque(inst["surface"], "media_group", "album", native["media_group_id"]))
    if "external_reply" in native:
        out["reply"] = record(source=out["message"], reference=None, author=None, relation=2, availability=2)
    elif "reply_to_message" in native:
        target = native["reply_to_message"]
        require(number(target["chat"]["id"]) == peer, "Telegram reply chat mismatch")
        if target.get("is_topic_message"):
            require(target.get("message_thread_id") == topic, "Telegram reply topic mismatch")
        author = tg_sender(inst, target)
        out["reply"] = reply(out["message"], message_ref(conv, target["message_id"]),
            f"offline-migration:{row['id']}", None if author["alternative"] == 2 else author)
        out["context"]["topic_root_reply"] = topic is not None and int(target["message_id"]) == topic
    return out


def convert_batch(rows):
    result = []
    for row in rows:
        try:
            inst = installation(row["source_platform"], row["source_bot"])
            native = native_archive(row)
            message = normalize_qq(row, native, inst) if row["source_platform"] == "qq" else normalize_telegram(row, native, inst)
            sender = message["sender"]["value"]["user"]["id"] if message["sender"]["alternative"] == 0 else ""
            result.append((row["id"], row["message_id"], row["source_platform"], row["source_bot"],
                row["conversation_id"], sender, message["context"]["group_id"], message["context"]["kind"],
                message["message"]["id"], dumps(message), row["timestamp"], row["created_at"], row["updated_at"]))
        except Exception as error:
            # Report only identity/row location, never message text or URLs.
            raise MigrationError(f"{row['source_platform']} row {row['id']}: {error}") from error
    return result


def quote(name):
    return '"' + name.replace('"', '""') + '"'


def prefix(namespace):
    identifier(namespace, 200)
    if re.fullmatch(r"[a-z0-9_]+", namespace) and not namespace.startswith("x__"):
        return namespace
    return "x__" + namespace.encode().hex()


def connect(path, readonly=False):
    connection = sqlite3.connect(path.resolve().as_uri() + ("?mode=ro" if readonly else "?mode=rw"), uri=True)
    connection.row_factory = sqlite3.Row
    if readonly:
        connection.execute("PRAGMA query_only=ON")
    return connection


def version(connection, table):
    return [row[0] for row in connection.execute(f"SELECT version FROM {quote(table)}")]


def count(connection, table):
    return connection.execute(f"SELECT COUNT(*) FROM {quote(table)}").fetchone()[0]


def integrity(connection):
    require([row[0] for row in connection.execute("PRAGMA integrity_check")] == ["ok"], "SQLite integrity check failed")


def batches(cursor):
    while rows := cursor.fetchmany(128):
        yield [dict(row) for row in rows]


def converted(cursor, workers):
    # Bounded submission: do not materialize the entire message archive in RAM.
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:
        pending = deque()
        for batch in batches(cursor):
            pending.append(pool.submit(convert_batch, batch))
            if len(pending) >= 2 * workers:
                yield from pending.popleft().result()
        for future in pending:
            yield from future.result()


def migrate_store(connection, ns, workers):
    versions = version(connection, ns + "_schema_version")
    if versions == [5]:
        platforms = list(connection.execute(f"SELECT platform,table_name FROM {quote(ns+'_platforms')}"))
        for platform, table in platforms:
            require(platform in ('qq', 'telegram') and table == f'{ns}_{platform}_messages', 'unexpected current Store table')
            columns = {r[1] for r in connection.execute(f'PRAGMA table_info({quote(table)})')}
            require('message' in columns and 'domain_message_id' in columns and not {'payload', 'raw'} & columns, 'partial current Store schema')
        return {'already_current': True, 'messages': {p: count(connection, t) for p, t in platforms}, 'users': count(connection, ns+'_users')}
    require(versions and max(versions) == 4 and all(1 <= x <= 4 for x in versions), "Store must be schema 4 or 5")
    platforms = list(connection.execute(f"SELECT platform,table_name FROM {quote(ns+'_platforms')}"))
    require(len({r[0] for r in platforms}) == len(platforms), "duplicate platform registry")
    report = {}
    for platform, table in platforms:
        require(platform in ("qq", "telegram") and table == f"{ns}_{platform}_messages", "unexpected platform table")
        require(not list(connection.execute("SELECT name FROM sqlite_master WHERE type='trigger' AND tbl_name=?", (table,))), "message table has custom triggers")
        total = count(connection, table)
        sequence = connection.execute("SELECT seq FROM sqlite_sequence WHERE name=?", (table,)).fetchone()
        temporary = table + "__typed_migration"
        connection.execute(f"CREATE TABLE {quote(temporary)} (id INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT NOT NULL,source_platform TEXT NOT NULL,source_bot TEXT NOT NULL,conversation_id TEXT NOT NULL,sender TEXT NOT NULL,group_id TEXT NOT NULL,message_type TEXT NOT NULL,domain_message_id TEXT NOT NULL,message TEXT NOT NULL,timestamp INTEGER NOT NULL,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL,UNIQUE(source_platform,source_bot,conversation_id,message_id))")
        cursor = connection.execute(f"SELECT * FROM {quote(table)} ORDER BY id")
        insert = f"INSERT INTO {quote(temporary)} VALUES ({','.join('?' for _ in range(13))})"
        completed = 0
        for row in converted(cursor, workers):
            connection.execute(insert, row)
            completed += 1
            if completed % 10000 == 0:
                print(f"{platform}: {completed}/{total}", flush=True)
        require(completed == total == count(connection, temporary), "message count changed")
        columns = "id,message_id,source_platform,source_bot,conversation_id,timestamp,created_at,updated_at"
        require(connection.execute(f"SELECT COUNT(*) FROM (SELECT {columns} FROM {quote(table)} EXCEPT SELECT {columns} FROM {quote(temporary)})").fetchone()[0] == 0, "source identity/timestamp changed")
        connection.execute(f"DROP TABLE {quote(table)}")
        connection.execute(f"ALTER TABLE {quote(temporary)} RENAME TO {quote(table)}")
        connection.execute(f"CREATE INDEX {quote(table+'_domain')} ON {quote(table)} (source_bot,domain_message_id)")
        if sequence:
            connection.execute("UPDATE sqlite_sequence SET seq=MAX(seq,?) WHERE name=?", (sequence[0], table))
        report[platform] = completed
    table, temporary = ns + "_users", ns + "_users__typed_migration"
    original_count = count(connection, table)
    connection.execute(f"CREATE TABLE {quote(temporary)} (source_platform TEXT NOT NULL,source_bot TEXT NOT NULL,user_id TEXT NOT NULL,group_id TEXT NOT NULL,user TEXT NOT NULL,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL,PRIMARY KEY(source_platform,source_bot,user_id,group_id))")
    for row in connection.execute(f"SELECT * FROM {quote(table)}"):
        value = user(installation(row["source_platform"], row["source_bot"]), row["user_id"])
        connection.execute(f"INSERT INTO {quote(temporary)} VALUES (?,?,?,?,?,?,?)", (row["source_platform"], row["source_bot"], value["id"], row["group_id"], dumps(value), row["created_at"], row["updated_at"]))
    require(count(connection, temporary) == original_count, "user count changed")
    connection.execute(f"DROP TABLE {quote(table)}")
    connection.execute(f"ALTER TABLE {quote(temporary)} RENAME TO {quote(table)}")
    connection.execute(f"DELETE FROM {quote(ns+'_schema_version')}")
    connection.execute(f"INSERT INTO {quote(ns+'_schema_version')} (version) VALUES (5)")
    return {"messages": report, "users": original_count}


def migrate_chat(connection, ns):
    versions = version(connection, ns + "_schema")
    require(versions in ([1], [2]), "Chat must be schema 1 or 2")
    expected = {
        "sessions": {"session", "document", "updated"},
        "epochs": {"session", "epoch", "document", "retired"},
        "refs": {"session", "source_db", "source_namespace", "platform", "bot", "conversation", "message_id", "source_id", "fingerprint"},
        "controls": {"scope", "enabled", "commands"},
    }
    report = {}
    for suffix, columns in expected.items():
        table = ns + "_" + suffix
        require({r[1] for r in connection.execute(f"PRAGMA table_info({quote(table)})")} == columns,
                f"unexpected Chat {suffix} layout")
        report[suffix] = count(connection, table)
        if suffix in ("sessions", "epochs"):
            for row in connection.execute(f"SELECT document FROM {quote(table)}"):
                require(isinstance(loads(row[0]), dict), "invalid Chat session/epoch document")
    # No document/epoch/journal rewrite: v2 changes the source projection, not
    # this SQL layout. Never mark external effects confirmed or replay a turn.
    if versions == [1]:
        connection.execute(f"UPDATE {quote(ns+'_schema')} SET version=2")
    return report


def cbor_encode(value):
    """The definite-length, sorted-object encoding used by nlohmann::json."""
    def head(major, size):
        require(0 <= size < 2**64, 'CBOR integer overflow')
        for maximum, code, width in ((23, 0, 0), (255, 24, 1), (65535, 25, 2), (2**32-1, 26, 4), (2**64-1, 27, 8)):
            if size <= maximum:
                return bytes([(major << 5) | (size if not width else code)]) + (size.to_bytes(width, 'big') if width else b'')
    if value is None:
        return b'\xf6'
    if isinstance(value, bool):
        return b'\xf5' if value else b'\xf4'
    if isinstance(value, int):
        return head(0, value) if value >= 0 else head(1, -1-value)
    if isinstance(value, float):
        require(value == value and abs(value) != float('inf'), 'nonfinite CBOR value')
        if abs(value) <= 3.4028234663852886e38 and struct.unpack('>f', struct.pack('>f', value))[0] == value:
            return b'\xfa' + struct.pack('>f', value)
        return b'\xfb' + struct.pack('>d', value)
    if isinstance(value, str):
        data = value.encode('utf-8')
        return head(3, len(data)) + data
    if isinstance(value, bytes):
        return head(2, len(value)) + value
    if isinstance(value, (list, tuple)):
        return head(4, len(value)) + b''.join(cbor_encode(v) for v in value)
    require(isinstance(value, dict) and all(isinstance(k, str) for k in value), 'unsupported CBOR value')
    return head(5, len(value)) + b''.join(cbor_encode(k) + cbor_encode(value[k]) for k in sorted(value))


def cbor_decode(data):
    position, nodes = 0, 0
    def take(size):
        nonlocal position
        require(size >= 0 and position + size <= len(data), 'truncated CBOR')
        value = data[position:position+size]
        position += size
        return value
    def read(depth):
        nonlocal nodes
        nodes += 1
        require(depth <= 64 and nodes <= 1000000, 'CBOR budget exceeded')
        byte = take(1)[0]
        major, extra = byte >> 5, byte & 31
        if major == 7:
            if extra in (20, 21, 22):
                return {20: False, 21: True, 22: None}[extra]
            require(extra in (25, 26, 27), 'unsupported CBOR simple value')
            width, fmt = {25: (2, '>e'), 26: (4, '>f'), 27: (8, '>d')}[extra]
            return struct.unpack(fmt, take(width))[0]
        require(extra <= 27, 'indefinite CBOR is not current canonical evidence')
        size = extra if extra < 24 else int.from_bytes(take({24: 1, 25: 2, 26: 4, 27: 8}[extra]), 'big')
        if major == 0:
            return size
        if major == 1:
            return -1-size
        if major in (2, 3):
            raw = take(size)
            return raw if major == 2 else raw.decode('utf-8')
        require(size <= 1000000, 'CBOR collection exceeds budget')
        if major == 4:
            return [read(depth+1) for _ in range(size)]
        require(major == 5, 'unsupported CBOR tag')
        result = {}
        for _ in range(size):
            key = read(depth+1)
            require(isinstance(key, str) and key not in result, 'duplicate/nontext CBOR key')
            result[key] = read(depth+1)
        return result
    result = read(0)
    require(position == len(data) and cbor_encode(result) == data, 'noncanonical CBOR evidence')
    return result


def bridge_document(value, codec, generation):
    require(set(value) == {'storage_format', 'codec', 'version', 'bytes_hex'} and value['storage_format'] == 1 and value['codec'] == codec and value['version'] == generation, 'unsupported Bridge envelope')
    text = value['bytes_hex']
    require(isinstance(text, str) and re.fullmatch(r'(?:[0-9a-f]{2})+', text) is not None and len(text) <= 128*1024*1024, 'invalid Bridge evidence bytes')
    return cbor_decode(bytes.fromhex(text))


def bridge_set_document(value, document):
    value['bytes_hex'] = cbor_encode(document).hex()
    value['version'] = 2


def bridge_member(member, scope, convert):
    ref = bridge_document(member['message_ref'], 'obcx.bot.MessageRef.cbor', 1)
    require(ref['conversation'] == scope, 'Bridge source member scope mismatch')
    if convert:
        key = loads(member.pop('legacy_source_key'))
        inst = scope['installation']
        require(key['installation'] == inst['installation_id'] and key['platform'] == ('qq' if inst['surface'] == 'onebot11.qq' else 'telegram') and ref == message_ref(scope, key['message']), 'Bridge native/source binding mismatch')
    else:
        require('legacy_source_key' not in member, 'old source member in current Bridge schema')
    return ref


def bridge_target(document, target):
    legacy, configured = document['legacy_target'], document['configured_target']
    require(configured['topic_observed'] is True and configured['conversation_id'].startswith('group:'), 'Bridge target lacks explicit group/topic evidence')
    topic = configured['topic_id']
    expected = conversation(target['installation'], 'group', configured['conversation_id'][6:], topic)
    require(expected == target and legacy['installation'] == target['installation']['installation_id'], 'Bridge configured target changed')
    require(legacy['platform'] == ('qq' if target['installation']['surface'] == 'onebot11.qq' else 'telegram'), 'Bridge target surface mismatch')
    del document['legacy_target'], document['configured_target']


def bridge_digest(connection, table, excluded=()):
    columns = [r[1] for r in connection.execute(f'PRAGMA table_info({quote(table)})') if r[1] not in excluded]
    keys = [r[1] for r in sorted(connection.execute(f'PRAGMA table_info({quote(table)})'), key=lambda r: r[5]) if r[5]]
    ordering = ','.join(map(quote, keys)) if keys else 'rowid'
    digest = hashlib.sha256()
    for row in connection.execute(f"SELECT {','.join(map(quote, columns))} FROM {quote(table)} ORDER BY {ordering}"):
        digest.update(repr(tuple(row)).encode('utf-8'))
        digest.update(b'\n')
    return digest.hexdigest()


def migrate_bridge(connection):
    versions = version(connection, 'bridge_schema_version')
    require(versions in ([4], [5]), 'Bridge must have a current typed schema 4 or 5')
    convert = versions == [4]
    changed = {'bridge_schema_version': ('version',), 'bridge_deliveries': ('admission',), 'bridge_delivery_members': ('data',), 'bridge_album_observations': ('data',), 'bridge_delivery_revisions': ('admission',)}
    tables = [r[0] for r in connection.execute("SELECT name FROM sqlite_master WHERE type='table' AND name GLOB 'bridge_*' ORDER BY name")]
    require(set(changed) <= set(tables), 'partial Bridge typed schema')
    require(not connection.execute("SELECT 1 FROM sqlite_master WHERE type='trigger' AND tbl_name IN (?,?,?,?)", tuple(t for t in changed if t != 'bridge_schema_version')).fetchall(), 'custom triggers on Bridge metadata tables')
    counts = {t: count(connection, t) for t in tables}
    before = {t: bridge_digest(connection, t, changed.get(t, ())) for t in tables if t != 'bridge_schema_version'}
    for row in connection.execute('SELECT delivery_id,admission,legacy_unknown FROM bridge_deliveries ORDER BY delivery_id'):
        if row['legacy_unknown']:
            continue  # Retain unsupported rows, never promote them to typed admissions.
        admission = loads(row['admission'])
        scope = bridge_document(admission['source_scope'], 'obcx.bot.ConversationRef.cbor', 1)
        target = bridge_document(admission['target_scope'], 'obcx.bot.ConversationRef.cbor', 1)
        for member in admission['members']:
            bridge_member(member, scope, convert)
        policy = bridge_document(admission['policy_and_limits'], 'obcx.bot.BridgeDeliveryPolicy.cbor', 1 if convert else 2)
        require(policy['publication']['source_bot'] == scope['installation']['installation_id'], 'Bridge publication owner mismatch')
        if convert:
            address = policy.pop('source_route_address')
            require(address['topic_observed'] is True and address['conversation_id'].startswith('group:'), 'Bridge source lacks actual topic evidence')
            topic = None if address['topic_id'] is None else int(number(address['topic_id'], 1, 2**31-1))
            group = address['conversation_id'][6:]
            require(conversation(scope['installation'], 'group', group, topic) == scope, 'Bridge source route binding mismatch')
            policy['source_route'] = {'group_id': group, 'topic_id': topic}
            policy.pop('source_address')
            bridge_target(policy, target)
            bridge_set_document(admission['policy_and_limits'], policy)
            connection.execute('UPDATE bridge_deliveries SET admission=? WHERE delivery_id=?', (json.dumps(admission, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False), row['delivery_id']))
        facts = policy['source_route']
        require(conversation(scope['installation'], 'group', facts['group_id'], facts['topic_id']) == scope, 'invalid current Bridge source route')
    for row in connection.execute('SELECT delivery_id,member_id,data FROM bridge_delivery_members ORDER BY delivery_id,member_id'):
        member = loads(row['data'])
        ref = bridge_document(member['message_ref'], 'obcx.bot.MessageRef.cbor', 1)
        bridge_member(member, ref['conversation'], convert)
        if convert:
            connection.execute('UPDATE bridge_delivery_members SET data=? WHERE delivery_id=? AND member_id=?', (json.dumps(member, ensure_ascii=False, sort_keys=True, separators=(',', ':')), row['delivery_id'], row['member_id']))
    for row in connection.execute('SELECT rowid,data FROM bridge_album_observations ORDER BY rowid'):
        value = loads(row['data'])
        observation = value['observation']
        ref = bridge_document(observation['message_ref'], 'obcx.bot.MessageRef.cbor', 1)
        bridge_member(observation, ref['conversation'], convert)
        if convert:
            connection.execute('UPDATE bridge_album_observations SET data=? WHERE rowid=?', (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')), row['rowid']))
    for row in connection.execute('SELECT delivery_id,revision_id,admission FROM bridge_delivery_revisions ORDER BY delivery_id,revision_id'):
        admission = loads(row['admission'])
        intent = bridge_document(admission['intent'], 'bridge.EditIntent.cbor', 1 if convert else 2)
        source = bridge_document(admission['source_ref'], 'obcx.bot.MessageRef.cbor', 1)
        target = bridge_document(admission['target_scope'], 'obcx.bot.ConversationRef.cbor', 1)
        require(intent['original_message']['message'] == source and intent['send']['target'] == target and intent['revision_id'] == admission['revision_id'] and intent['delivery_id'] == admission['delivery_id'], 'Bridge immutable edit intent mismatch')
        if convert:
            key = intent.pop('legacy_source')
            require(message_ref(source['conversation'], key['message']) == source and key['installation'] == source['conversation']['installation']['installation_id'], 'Bridge edit source binding mismatch')
            bridge_target(intent, target)
            bridge_set_document(admission['intent'], intent)
            connection.execute('UPDATE bridge_delivery_revisions SET admission=? WHERE delivery_id=? AND revision_id=?', (json.dumps(admission, ensure_ascii=False, sort_keys=True, separators=(',', ':')), row['delivery_id'], row['revision_id']))
    if convert:
        connection.execute('UPDATE bridge_schema_version SET version=5')
    require(counts == {t: count(connection, t) for t in tables}, 'Bridge row counts changed')
    require(before == {t: bridge_digest(connection, t, changed.get(t, ())) for t in tables if t != 'bridge_schema_version'}, 'Bridge receipts, retry payloads, command fences or immutable columns changed')
    require(not list(connection.execute('PRAGMA foreign_key_check')), 'Bridge foreign key check failed')
    return {'already_current': not convert, 'counts': counts, 'preserved_columns_sha256': before}


def fingerprint(path):
    result = []
    for file in (path, Path(str(path)+"-wal")):
        if file.exists() and (file == path or file.stat().st_size != 0):
            with file.open("rb") as stream:
                result.append((str(file), hashlib.file_digest(stream, "sha256").hexdigest()))
        else:
            result.append((str(file), None))
    return result


def backup(source, destination):
    require(not destination.exists(), f"refusing to overwrite {destination}")
    with closing(connect(source, readonly=True)) as src, closing(sqlite3.connect(destination)) as dst:
        src.backup(dst)
        dst.commit()
    os.chmod(destination, 0o600)


def durable_json(path, value):
    with path.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("plan", "apply"))
    parser.add_argument("--store-db", required=True, type=Path)
    parser.add_argument("--chat-db", required=True, type=Path)
    parser.add_argument("--bridge-db", required=True, type=Path)
    parser.add_argument("--store-namespace", required=True)
    parser.add_argument("--chat-namespace", required=True)
    parser.add_argument("--backup-dir", required=True, type=Path)
    parser.add_argument("--workers", required=True, type=int)
    parser.add_argument("--confirm-stopped", action="store_true", required=True)
    args = parser.parse_args()
    require(args.workers >= 1, "workers must be positive")
    store_path, chat_path, bridge_path = (p.resolve(strict=True) for p in (args.store_db, args.chat_db, args.bridge_db))
    require(store_path != chat_path, "Store and Chat must be separate database files")
    paths = list(dict.fromkeys((store_path, chat_path, bridge_path)))
    bridge_index = paths.index(bridge_path)
    sns, cns = prefix(args.store_namespace), prefix(args.chat_namespace)
    if args.mode == "plan":
        with connect(store_path, readonly=True) as store, connect(chat_path, readonly=True) as chat, connect(bridge_path, readonly=True) as bridge:
            print(dumps({"store_versions": version(store, sns+"_schema_version"),
                         "chat_versions": version(chat, cns+"_schema"),
                         "bridge_versions": version(bridge, 'bridge_schema_version'),
                         "bridge_deliveries": count(bridge, 'bridge_deliveries'),
                         "messages": {r[0]: count(store, r[1]) for r in store.execute(f"SELECT platform,table_name FROM {quote(sns+'_platforms')}")},
                         "action": "apply converts copies; preserves current typed Bridge effects/fences; originals untouched until every conversion succeeds"}))
        return
    args.backup_dir.mkdir(mode=0o700, parents=True, exist_ok=False)
    saved = [args.backup_dir / 'message-store-original.sqlite3', args.backup_dir / 'chat-original.sqlite3']
    if len(paths) == 3:
        saved.append(args.backup_dir / 'bridge-original.sqlite3')
    baseline = [fingerprint(p) for p in paths]
    for source, destination in zip(paths, saved):
        backup(source, destination)
    require([fingerprint(p) for p in paths] == baseline, "database changed during backup; stop all writers")
    candidates = [p.with_name(p.name + ".typed-migration-" + str(uuid.uuid4())) for p in paths]
    journal_path = args.backup_dir / "migration.json"
    journal = {"phase": "backed-up", "originals": list(map(str, paths)), "backups": list(map(str, saved)), "candidates": list(map(str, candidates))}
    durable_json(journal_path, journal)
    for source, destination in zip(saved, candidates):
        backup(source, destination)
    for candidate in candidates:
        with connect(candidate) as connection:
            connection.execute("PRAGMA journal_mode=DELETE")
            integrity(connection)
    with connect(candidates[0]) as store:
        journal["store"] = migrate_store(store, sns, args.workers)
    with connect(candidates[1]) as chat:
        journal["chat"] = migrate_chat(chat, cns)
    with connect(candidates[bridge_index]) as bridge:
        journal['bridge'] = migrate_bridge(bridge)
    for candidate in candidates:
        with connect(candidate, readonly=True) as connection:
            integrity(connection)
    require([fingerprint(p) for p in paths] == baseline, "original database changed during conversion; refusing replacement")
    # A nonempty WAL must not be paired with a replacement main database.
    for path in paths:
        wal = Path(str(path)+"-wal")
        require(not wal.exists() or wal.stat().st_size == 0, "nonempty WAL; checkpoint offline before retrying")
    journal["phase"] = "verified-ready-to-replace"
    durable_json(journal_path, journal)
    replaced = []
    try:
        for index, (candidate, original) in enumerate(zip(candidates, paths)):
            # Only empty, stopped-process sidecars; full database backups exist.
            for suffix in ("-wal", "-shm"):
                Path(str(original)+suffix).unlink(missing_ok=True)
            os.replace(candidate, original)
            replaced.append(index)
            journal["phase"] = f"replaced-{index+1}-of-{len(paths)}"
            durable_json(journal_path, journal)
    except BaseException:
        for index in replaced:
            restore = paths[index].with_name(paths[index].name + ".restore-" + str(uuid.uuid4()))
            shutil.copy2(saved[index], restore)
            os.replace(restore, paths[index])
        journal["phase"] = "replacement-failed-restored"
        durable_json(journal_path, journal)
        raise
    journal["phase"] = "complete"
    durable_json(journal_path, journal)
    print(dumps(journal))
    print("Migration complete. No program was started or restarted.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Migration aborted: {error}", file=sys.stderr)
        sys.exit(1)
