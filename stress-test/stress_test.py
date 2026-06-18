#!/usr/bin/env python3
"""Async stress/benchmark client for ChatService's TCP JSON protocol."""

from __future__ import annotations

import argparse
import asyncio
import csv
import json
import logging
import math
import os
import random
import time
import uuid
from collections import Counter, OrderedDict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PROTOCOL_MARKER = "__chatservice_stress_v1__"
VALID_MODES = ("connect_only", "private", "room", "mixed")
LOG = logging.getLogger("chatservice-stress")


class JsonLogFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "time": datetime.now(timezone.utc).isoformat(),
            "level": record.levelname,
            "event": getattr(record, "event", "log"),
            "message": record.getMessage(),
        }
        details = getattr(record, "details", None)
        if details:
            payload.update(details)
        return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def configure_logging(level: str) -> None:
    handler = logging.StreamHandler()
    handler.setFormatter(JsonLogFormatter())
    LOG.handlers.clear()
    LOG.addHandler(handler)
    LOG.setLevel(getattr(logging, level.upper()))
    LOG.propagate = False


def log_event(level: int, event: str, message: str, **details: Any) -> None:
    LOG.log(level, message, extra={"event": event, "details": details})


def utc_now_text() -> str:
    return datetime.now(timezone.utc).isoformat()


def percentile(values: list[float], percent: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil((percent / 100.0) * len(ordered)))
    return ordered[rank - 1]


def safe_rate(numerator: int | float, seconds: float) -> float:
    return 0.0 if seconds <= 0 else float(numerator) / seconds


def make_payload(
    run_id: str,
    message_id: int,
    sent_ns: int,
    sequence: int,
    kind: str,
    sender: str,
    destination: str,
) -> str:
    return "|".join(
        (
            PROTOCOL_MARKER,
            run_id,
            str(message_id),
            str(sent_ns),
            str(sequence),
            kind,
            sender,
            destination,
        )
    )


@dataclass(frozen=True)
class ParsedPayload:
    run_id: str
    message_id: int
    sent_ns: int
    sequence: int
    kind: str
    sender: str
    destination: str


def parse_payload(message: Any) -> ParsedPayload | None:
    if not isinstance(message, str) or not message.startswith(PROTOCOL_MARKER + "|"):
        return None
    parts = message.split("|")
    if len(parts) != 8:
        return None
    try:
        message_id = int(parts[2])
        sent_ns = int(parts[3])
        sequence = int(parts[4])
    except ValueError:
        return None
    if message_id <= 0 or sent_ns <= 0 or sequence <= 0:
        return None
    return ParsedPayload(
        run_id=parts[1],
        message_id=message_id,
        sent_ns=sent_ns,
        sequence=sequence,
        kind=parts[5],
        sender=parts[6],
        destination=parts[7],
    )


class LatencyReservoir:
    def __init__(self, capacity: int, seed: int) -> None:
        self.capacity = capacity
        self.values_ms: list[float] = []
        self.seen = 0
        self.total_ms = 0.0
        self.maximum_ms = 0.0
        self.random = random.Random(seed)

    def add(self, latency_ms: float) -> None:
        self.seen += 1
        self.total_ms += latency_ms
        self.maximum_ms = max(self.maximum_ms, latency_ms)
        if len(self.values_ms) < self.capacity:
            self.values_ms.append(latency_ms)
            return
        replacement = self.random.randrange(self.seen)
        if replacement < self.capacity:
            self.values_ms[replacement] = latency_ms

    @property
    def average_ms(self) -> float | None:
        return None if self.seen == 0 else self.total_ms / self.seen


@dataclass
class ResourceSample:
    elapsed_seconds: float
    cpu_percent: float
    rss_bytes: int


class ProcessSampler:
    def __init__(
        self,
        pid: int,
        interval: float,
        cpu_threshold: float,
        cpu_lag_seconds: float,
    ) -> None:
        self.pid = pid
        self.interval = interval
        self.cpu_threshold = cpu_threshold
        self.cpu_lag_seconds = cpu_lag_seconds
        self.samples: list[ResourceSample] = []
        self.stop_event = asyncio.Event()
        self.error: str | None = None
        self.sustained_high_cpu = False

    async def run(self, start_time: float) -> None:
        try:
            import psutil  # type: ignore

            await self._run_psutil(psutil, start_time)
        except ImportError:
            await self._run_procfs(start_time)
        except Exception as exc:  # pragma: no cover - monitor isolation
            self.error = f"{type(exc).__name__}: {exc}"

    async def _run_psutil(self, psutil: Any, start_time: float) -> None:
        process = psutil.Process(self.pid)
        process.cpu_percent(None)
        while not self.stop_event.is_set():
            await self._sleep_or_stop()
            if self.stop_event.is_set():
                break
            try:
                cpu = float(process.cpu_percent(None))
                rss = int(process.memory_info().rss)
            except (psutil.NoSuchProcess, psutil.AccessDenied) as exc:
                self.error = f"{type(exc).__name__}: {exc}"
                break
            self.samples.append(
                ResourceSample(time.monotonic() - start_time, cpu, rss)
            )
        self._calculate_sustained_high_cpu()

    async def _run_procfs(self, start_time: float) -> None:
        stat_path = Path(f"/proc/{self.pid}/stat")
        statm_path = Path(f"/proc/{self.pid}/statm")
        if not stat_path.exists():
            self.error = (
                "psutil is not installed and /proc sampling is unavailable for "
                f"PID {self.pid}"
            )
            return

        ticks_per_second = os.sysconf("SC_CLK_TCK")
        page_size = os.sysconf("SC_PAGE_SIZE")
        previous_cpu: float | None = None
        previous_wall: float | None = None

        while not self.stop_event.is_set():
            await self._sleep_or_stop()
            if self.stop_event.is_set():
                break
            try:
                stat_text = stat_path.read_text(encoding="utf-8")
                fields = stat_text[stat_text.rfind(")") + 2 :].split()
                cpu_seconds = (int(fields[11]) + int(fields[12])) / ticks_per_second
                rss_pages = int(statm_path.read_text(encoding="utf-8").split()[1])
                now = time.monotonic()
            except (FileNotFoundError, PermissionError, ValueError, IndexError) as exc:
                self.error = f"{type(exc).__name__}: {exc}"
                break

            if previous_cpu is not None and previous_wall is not None:
                wall_delta = now - previous_wall
                cpu = (
                    0.0
                    if wall_delta <= 0
                    else (cpu_seconds - previous_cpu) / wall_delta * 100.0
                )
                self.samples.append(
                    ResourceSample(now - start_time, cpu, rss_pages * page_size)
                )
            previous_cpu = cpu_seconds
            previous_wall = now
        self._calculate_sustained_high_cpu()

    async def _sleep_or_stop(self) -> None:
        try:
            await asyncio.wait_for(self.stop_event.wait(), timeout=self.interval)
        except asyncio.TimeoutError:
            pass

    def _calculate_sustained_high_cpu(self) -> None:
        required = max(1, math.ceil(self.cpu_lag_seconds / self.interval))
        consecutive = 0
        for sample in self.samples:
            if sample.cpu_percent >= self.cpu_threshold:
                consecutive += 1
                if consecutive >= required:
                    self.sustained_high_cpu = True
                    return
            else:
                consecutive = 0

    def stop(self) -> None:
        self.stop_event.set()

    def summary(self) -> dict[str, Any]:
        if not self.samples:
            return {
                "server_pid": self.pid,
                "resource_sample_count": 0,
                "server_cpu_average_percent": None,
                "server_cpu_max_percent": None,
                "server_rss_average_bytes": None,
                "server_rss_max_bytes": None,
                "server_cpu_sustained_high": False,
                "resource_monitor_error": self.error,
            }
        return {
            "server_pid": self.pid,
            "resource_sample_count": len(self.samples),
            "server_cpu_average_percent": sum(
                sample.cpu_percent for sample in self.samples
            )
            / len(self.samples),
            "server_cpu_max_percent": max(
                sample.cpu_percent for sample in self.samples
            ),
            "server_rss_average_bytes": int(
                sum(sample.rss_bytes for sample in self.samples) / len(self.samples)
            ),
            "server_rss_max_bytes": max(sample.rss_bytes for sample in self.samples),
            "server_cpu_sustained_high": self.sustained_high_cpu,
            "resource_monitor_error": self.error,
        }


@dataclass
class BenchmarkMetrics:
    requested_users: int
    latency_reservoir: LatencyReservoir
    connection_attempts: int = 0
    tcp_connections: int = 0
    successful_connections: int = 0
    connection_failures: int = 0
    setup_failures: int = 0
    messages_sent: int = 0
    expected_deliveries: int = 0
    messages_received: int = 0
    unique_deliveries: int = 0
    duplicate_deliveries: int = 0
    out_of_order_deliveries: int = 0
    incorrect_deliveries: int = 0
    unexpected_disconnects: int = 0
    malformed_messages: int = 0
    error_count: int = 0
    errors_by_type: Counter[str] = field(default_factory=Counter)
    last_sequences: dict[tuple[str, str, str, str], int] = field(
        default_factory=dict
    )
    recent_delivery_keys: OrderedDict[tuple[str, int], None] = field(
        default_factory=OrderedDict
    )
    duplicate_tracking_capacity: int = 1_000_000

    def record_error(self, category: str) -> None:
        self.error_count += 1
        self.errors_by_type[category] += 1

    def record_sent(self, expected_deliveries: int) -> None:
        self.messages_sent += 1
        self.expected_deliveries += max(0, expected_deliveries)

    def record_delivery(
        self,
        receiver: str,
        payload: ParsedPayload,
        event: dict[str, Any],
    ) -> None:
        self.messages_received += 1
        delivery_key = (receiver, payload.message_id)
        if delivery_key in self.recent_delivery_keys:
            self.duplicate_deliveries += 1
            return
        self.recent_delivery_keys[delivery_key] = None
        if len(self.recent_delivery_keys) > self.duplicate_tracking_capacity:
            self.recent_delivery_keys.popitem(last=False)

        valid = (
            event.get("from") == payload.sender
            and payload.kind in ("private", "room")
        )
        if payload.kind == "private":
            valid = valid and receiver == payload.destination
        else:
            valid = valid and event.get("room") == payload.destination
        if not valid:
            self.incorrect_deliveries += 1
            self.record_error("incorrect_delivery")
            return

        sequence_key = (
            receiver,
            payload.sender,
            payload.kind,
            payload.destination,
        )
        previous = self.last_sequences.get(sequence_key)
        if previous is not None and payload.sequence <= previous:
            self.out_of_order_deliveries += 1
        self.last_sequences[sequence_key] = max(previous or 0, payload.sequence)

        latency_ms = max(0.0, (time.monotonic_ns() - payload.sent_ns) / 1_000_000)
        self.latency_reservoir.add(latency_ms)
        self.unique_deliveries += 1


class ProtocolError(RuntimeError):
    pass


class VirtualUser:
    def __init__(self, benchmark: "Benchmark", user_id: int) -> None:
        self.benchmark = benchmark
        self.user_id = user_id
        self.username = f"bot_{benchmark.run_id}_{user_id:06d}"
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self.reader_task: asyncio.Task[None] | None = None
        self.control_queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.accept_control = True
        self.connected = False
        self.logged_in = False
        self.ready = False
        self.closing = False
        self.disconnect_reported = False
        self.room: str | None = None
        self.role = "idle"
        self.sequence = 0
        self.random = random.Random(benchmark.args.seed + user_id * 104729)

    @property
    def is_live(self) -> bool:
        return self.connected and self.logged_in and not self.closing

    async def connect_and_login(self) -> bool:
        metrics = self.benchmark.metrics
        metrics.connection_attempts += 1
        try:
            connect = asyncio.open_connection(
                self.benchmark.args.host,
                self.benchmark.args.port,
                limit=self.benchmark.args.max_line_bytes,
            )
            self.reader, self.writer = await asyncio.wait_for(
                connect, timeout=self.benchmark.args.connect_timeout
            )
            self.connected = True
            metrics.tcp_connections += 1
            self.reader_task = asyncio.create_task(self._reader_loop())

            await self._wait_for_ok("connected")
            await self._send_request(
                {"type": "login", "username": self.username},
                expected_ok=f"logged in as {self.username}",
            )
            self.logged_in = True
            metrics.successful_connections += 1
            return True
        except Exception as exc:
            metrics.connection_failures += 1
            metrics.record_error("connection_or_login_failure")
            log_event(
                logging.DEBUG,
                "user_connect_failed",
                "virtual user failed to connect or log in",
                user=self.username,
                error=f"{type(exc).__name__}: {exc}",
            )
            await self.close()
            return False

    async def create_room(self, room: str) -> bool:
        try:
            await self._send_request(
                {"type": "create_room", "room": room},
                expected_ok="room created",
            )
            return True
        except Exception as exc:
            self.benchmark.metrics.setup_failures += 1
            self.benchmark.metrics.record_error("create_room_failure")
            log_event(
                logging.WARNING,
                "create_room_failed",
                "failed to create benchmark room",
                user=self.username,
                room=room,
                error=f"{type(exc).__name__}: {exc}",
            )
            return False

    async def join_room(self, room: str) -> bool:
        try:
            await self._send_request(
                {"type": "join", "room": room},
                expected_ok="joined room",
            )
            self.room = room
            self.ready = True
            return True
        except Exception as exc:
            self.benchmark.metrics.setup_failures += 1
            self.benchmark.metrics.record_error("join_room_failure")
            log_event(
                logging.WARNING,
                "join_room_failed",
                "failed to join benchmark room",
                user=self.username,
                room=room,
                error=f"{type(exc).__name__}: {exc}",
            )
            return False

    async def _send_request(
        self, request: dict[str, Any], expected_ok: str
    ) -> dict[str, Any]:
        request["sent_at_us"] = str(time.time_ns() // 1000)
        await self._write_json(request)
        return await self._wait_for_ok(expected_ok)

    async def _wait_for_ok(self, expected_message: str) -> dict[str, Any]:
        deadline = time.monotonic() + self.benchmark.args.operation_timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for: {expected_message}")
            response = await asyncio.wait_for(
                self.control_queue.get(), timeout=remaining
            )
            response_type = response.get("type")
            message = str(response.get("message", ""))
            if response_type == "error":
                raise ProtocolError(message or "server returned an error")
            if response_type == "ok" and message == expected_message:
                return response

    async def _write_json(self, payload: dict[str, Any]) -> None:
        if self.writer is None or self.writer.is_closing():
            raise ConnectionError("socket is not connected")
        data = (
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode(
                "utf-8"
            )
            + b"\n"
        )
        self.writer.write(data)
        await asyncio.wait_for(
            self.writer.drain(), timeout=self.benchmark.args.send_timeout
        )

    async def send_chat_message(self) -> None:
        if not self.is_live:
            return
        kind = self.role
        destination: str
        expected_deliveries: int
        if kind == "private":
            target = self.benchmark.choose_private_target(self)
            if target is None:
                return
            destination = target.username
            expected_deliveries = 1
        elif kind == "room" and self.room:
            destination = self.room
            expected_deliveries = self.benchmark.live_room_count(self.room)
        else:
            return

        self.sequence += 1
        message_id = self.benchmark.next_message_id()
        sent_ns = time.monotonic_ns()
        body = make_payload(
            self.benchmark.run_id,
            message_id,
            sent_ns,
            self.sequence,
            kind,
            self.username,
            destination,
        )
        request: dict[str, Any] = {
            "type": "private" if kind == "private" else "room_msg",
            "message": body,
            "sent_at_us": str(time.time_ns() // 1000),
        }
        if kind == "private":
            request["target"] = destination
        else:
            request["room"] = destination

        try:
            await self._write_json(request)
            self.benchmark.metrics.record_sent(expected_deliveries)
        except Exception as exc:
            self.benchmark.metrics.record_error("send_failure")
            log_event(
                logging.DEBUG,
                "send_failed",
                "virtual user send failed",
                user=self.username,
                error=f"{type(exc).__name__}: {exc}",
            )
            self.connected = False
            await self._report_disconnect()
            await self.close()

    async def sender_loop(self, end_time: float) -> None:
        rate = self.benchmark.args.message_rate
        if self.role == "idle" or rate <= 0:
            return
        interval = 1.0 / rate
        initial_delay = self.random.random() * interval
        await asyncio.sleep(initial_delay)
        next_send = time.monotonic()
        while time.monotonic() < end_time and self.is_live:
            await self.send_chat_message()
            next_send += interval
            delay = next_send - time.monotonic()
            if delay < -interval:
                next_send = time.monotonic()
                delay = 0.0
            if delay > 0:
                remaining = end_time - time.monotonic()
                if remaining <= 0:
                    break
                await asyncio.sleep(min(delay, remaining))

    async def _reader_loop(self) -> None:
        assert self.reader is not None
        try:
            while True:
                line = await self.reader.readline()
                if not line:
                    break
                if len(line) > self.benchmark.args.max_line_bytes:
                    self.benchmark.metrics.record_error("oversized_server_line")
                    break
                try:
                    event = json.loads(line.decode("utf-8"))
                    if not isinstance(event, dict):
                        raise ValueError("JSON value is not an object")
                except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
                    self.benchmark.metrics.malformed_messages += 1
                    self.benchmark.metrics.record_error("malformed_server_message")
                    continue
                self._handle_event(event)
        except (asyncio.CancelledError, ConnectionError):
            pass
        except Exception as exc:
            self.benchmark.metrics.record_error("reader_failure")
            log_event(
                logging.DEBUG,
                "reader_failed",
                "virtual user reader failed",
                user=self.username,
                error=f"{type(exc).__name__}: {exc}",
            )
        finally:
            self.connected = False
            await self._report_disconnect()

    def _handle_event(self, event: dict[str, Any]) -> None:
        event_type = event.get("type")
        if event_type in ("private", "room"):
            payload = parse_payload(event.get("message"))
            if payload is None or payload.run_id != self.benchmark.run_id:
                return
            self.benchmark.metrics.record_delivery(self.username, payload, event)
            return
        if event_type in ("ok", "error"):
            if self.accept_control:
                self.control_queue.put_nowait(event)
            elif event_type == "error":
                self.benchmark.metrics.record_error("server_error")

    async def _report_disconnect(self) -> None:
        if self.disconnect_reported:
            return
        self.disconnect_reported = True
        if self.logged_in and not self.closing:
            self.benchmark.metrics.unexpected_disconnects += 1
        self.benchmark.mark_user_disconnected(self)

    async def close(self) -> None:
        self.closing = True
        self.benchmark.mark_user_disconnected(self)
        if self.writer is not None:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except (ConnectionError, OSError):
                pass
        if self.reader_task is not None and not self.reader_task.done():
            try:
                await asyncio.wait_for(self.reader_task, timeout=1.0)
            except asyncio.TimeoutError:
                self.reader_task.cancel()
                await asyncio.gather(self.reader_task, return_exceptions=True)


class Benchmark:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.run_id = uuid.uuid4().hex[:8]
        self.metrics = BenchmarkMetrics(
            requested_users=args.users,
            latency_reservoir=LatencyReservoir(
                args.max_latency_samples, args.seed
            ),
            duplicate_tracking_capacity=args.duplicate_tracking_capacity,
        )
        self.users = [VirtualUser(self, index + 1) for index in range(args.users)]
        self.live_users: set[VirtualUser] = set()
        self.room_members: dict[str, set[VirtualUser]] = {}
        self.message_id = 0
        self.started_at_utc = utc_now_text()
        self.measurement_started = 0.0
        self.measurement_elapsed = 0.0
        self.sampler: ProcessSampler | None = None
        self.sampler_task: asyncio.Task[None] | None = None

    def next_message_id(self) -> int:
        self.message_id += 1
        return self.message_id

    def mark_user_disconnected(self, user: VirtualUser) -> None:
        self.live_users.discard(user)

    def live_room_count(self, room: str) -> int:
        return sum(
            1 for user in self.room_members.get(room, ()) if user in self.live_users
        )

    def choose_private_target(self, sender: VirtualUser) -> VirtualUser | None:
        if not self.live_users:
            return None
        candidates = tuple(
            user
            for user in self.live_users
            if user is not sender and user.is_live
        )
        if candidates:
            return sender.random.choice(candidates)
        return sender if sender.is_live else None

    async def run(self) -> dict[str, Any]:
        overall_start = time.monotonic()
        if self.args.server_pid:
            self.sampler = ProcessSampler(
                self.args.server_pid,
                self.args.resource_interval,
                self.args.cpu_lag_threshold,
                self.args.cpu_lag_seconds,
            )
            self.sampler_task = asyncio.create_task(
                self.sampler.run(overall_start)
            )

        log_event(
            logging.INFO,
            "benchmark_start",
            "starting ChatService benchmark",
            run_id=self.run_id,
            mode=self.args.mode,
            users=self.args.users,
            host=self.args.host,
            port=self.args.port,
        )

        await self._connect_users()
        await self._setup_users()

        active_users = [user for user in self.users if user.ready and user.is_live]
        self.measurement_started = time.monotonic()
        end_time = self.measurement_started + self.args.duration
        sender_tasks = [
            asyncio.create_task(user.sender_loop(end_time))
            for user in active_users
        ]

        await asyncio.sleep(self.args.duration)
        self.measurement_elapsed = max(
            0.001, time.monotonic() - self.measurement_started
        )
        await asyncio.gather(*sender_tasks, return_exceptions=True)

        if self.args.grace_period > 0:
            await asyncio.sleep(self.args.grace_period)

        await asyncio.gather(
            *(user.close() for user in self.users), return_exceptions=True
        )
        if self.sampler is not None:
            self.sampler.stop()
        if self.sampler_task is not None:
            await asyncio.gather(self.sampler_task, return_exceptions=True)

        summary = self._build_summary(active_users, overall_start)
        write_results(
            self.args.results_dir,
            self.args.result_name,
            summary,
            self.metrics.latency_reservoir.values_ms,
            self.sampler.samples if self.sampler else [],
        )
        print_summary(summary, self.args.results_dir)
        return summary

    async def _connect_users(self) -> None:
        semaphore = (
            asyncio.Semaphore(self.args.connect_concurrency)
            if self.args.connect_concurrency > 0
            else None
        )

        async def connect(user: VirtualUser) -> bool:
            if semaphore is None:
                return await user.connect_and_login()
            async with semaphore:
                return await user.connect_and_login()

        results = await asyncio.gather(
            *(connect(user) for user in self.users), return_exceptions=True
        )
        for user, result in zip(self.users, results):
            if result is True:
                self.live_users.add(user)
            elif isinstance(result, Exception):
                self.metrics.connection_failures += 1
                self.metrics.record_error("uncaught_connection_failure")

        log_event(
            logging.INFO,
            "connections_ready",
            "connection and login phase complete",
            successful=self.metrics.successful_connections,
            failed=self.args.users - self.metrics.successful_connections,
        )

    def _assign_roles(self, connected: list[VirtualUser]) -> None:
        for user in connected:
            if self.args.mode == "connect_only":
                user.role = "idle"
            elif self.args.mode == "private":
                user.role = "private"
            elif self.args.mode == "room":
                user.role = "room"
            else:
                bucket = (user.user_id - 1) % 10
                user.role = (
                    "room" if bucket < 4 else "private" if bucket < 8 else "idle"
                )

    async def _setup_users(self) -> None:
        connected = [user for user in self.users if user.logged_in and user.is_live]
        self._assign_roles(connected)

        room_users = [user for user in connected if user.role == "room"]
        if room_users:
            room_count = min(self.args.rooms, len(room_users))
            room_names = [
                f"st_{self.run_id}_r{index:03d}" for index in range(room_count)
            ]
            groups: dict[str, list[VirtualUser]] = {
                room: [] for room in room_names
            }
            for index, user in enumerate(room_users):
                groups[room_names[index % room_count]].append(user)

            create_results = await asyncio.gather(
                *(
                    members[0].create_room(room)
                    for room, members in groups.items()
                    if members
                )
            )
            created_rooms = {
                room
                for room, created in zip(groups.keys(), create_results)
                if created
            }
            join_pairs = [
                (user, room)
                for room, members in groups.items()
                if room in created_rooms
                for user in members
            ]
            join_results = await asyncio.gather(
                *(user.join_room(room) for user, room in join_pairs)
            )
            for (user, room), joined in zip(join_pairs, join_results):
                if joined:
                    self.room_members.setdefault(room, set()).add(user)

        for user in connected:
            if user.role != "room":
                user.ready = True
            user.accept_control = False

        log_event(
            logging.INFO,
            "setup_complete",
            "virtual user setup complete",
            active_users=sum(user.ready and user.is_live for user in connected),
            active_rooms=len(self.room_members),
            setup_failures=self.metrics.setup_failures,
        )

    def _build_summary(
        self, active_users: list[VirtualUser], overall_start: float
    ) -> dict[str, Any]:
        metrics = self.metrics
        latency = metrics.latency_reservoir
        p50 = percentile(latency.values_ms, 50)
        p95 = percentile(latency.values_ms, 95)
        p99 = percentile(latency.values_ms, 99)

        failed_connections = self.args.users - metrics.successful_connections
        connection_failure_rate = (
            failed_connections / self.args.users * 100.0
            if self.args.users
            else 0.0
        )
        loss_count = max(0, metrics.expected_deliveries - metrics.unique_deliveries)
        loss_rate = (
            loss_count / metrics.expected_deliveries * 100.0
            if metrics.expected_deliveries
            else 0.0
        )

        resource_summary = (
            self.sampler.summary()
            if self.sampler is not None
            else {
                "server_pid": None,
                "resource_sample_count": 0,
                "server_cpu_average_percent": None,
                "server_cpu_max_percent": None,
                "server_rss_average_bytes": None,
                "server_rss_max_bytes": None,
                "server_cpu_sustained_high": False,
                "resource_monitor_error": None,
            }
        )

        laggy_reasons: list[str] = []
        if p95 is not None and p95 > 1000.0:
            laggy_reasons.append("p95_latency_exceeds_1000_ms")
        if p99 is not None and p99 > 2000.0:
            laggy_reasons.append("p99_latency_exceeds_2000_ms")
        if loss_rate > 1.0:
            laggy_reasons.append("message_loss_exceeds_1_percent")
        if connection_failure_rate > 1.0:
            laggy_reasons.append("connection_failure_exceeds_1_percent")
        if metrics.unexpected_disconnects:
            laggy_reasons.append("unexpected_disconnects")
        if metrics.out_of_order_deliveries:
            laggy_reasons.append("out_of_order_deliveries")
        if metrics.incorrect_deliveries:
            laggy_reasons.append("incorrect_deliveries")
        if metrics.duplicate_deliveries:
            laggy_reasons.append("duplicate_deliveries")
        if resource_summary["server_cpu_sustained_high"]:
            laggy_reasons.append("server_cpu_sustained_high")

        return {
            "run_id": self.run_id,
            "started_at_utc": self.started_at_utc,
            "finished_at_utc": utc_now_text(),
            "host": self.args.host,
            "port": self.args.port,
            "mode": self.args.mode,
            "configured_duration_seconds": self.args.duration,
            "measurement_elapsed_seconds": self.measurement_elapsed,
            "overall_elapsed_seconds": time.monotonic() - overall_start,
            "message_rate_per_sending_user": self.args.message_rate,
            "rooms_requested": self.args.rooms,
            "active_rooms": len(self.room_members),
            "total_users_requested": self.args.users,
            "tcp_connections_established": metrics.tcp_connections,
            "successful_connections": metrics.successful_connections,
            "failed_connections": failed_connections,
            "connection_failure_rate_percent": connection_failure_rate,
            "active_users": sum(user.ready for user in active_users),
            "setup_failures": metrics.setup_failures,
            "total_messages_sent": metrics.messages_sent,
            "expected_message_deliveries": metrics.expected_deliveries,
            "total_messages_received": metrics.messages_received,
            "unique_message_deliveries": metrics.unique_deliveries,
            "message_loss_count": loss_count,
            "message_loss_rate_percent": loss_rate,
            "duplicate_delivery_count": metrics.duplicate_deliveries,
            "out_of_order_delivery_count": metrics.out_of_order_deliveries,
            "incorrect_delivery_count": metrics.incorrect_deliveries,
            "average_latency_ms": latency.average_ms,
            "p50_latency_ms": p50,
            "p95_latency_ms": p95,
            "p99_latency_ms": p99,
            "max_latency_ms": latency.maximum_ms if latency.seen else None,
            "latency_samples_seen": latency.seen,
            "latency_samples_recorded": len(latency.values_ms),
            "latency_percentiles_are_sampled": latency.seen
            > len(latency.values_ms),
            "sent_throughput_messages_per_second": safe_rate(
                metrics.messages_sent, self.measurement_elapsed
            ),
            "received_throughput_deliveries_per_second": safe_rate(
                metrics.unique_deliveries, self.measurement_elapsed
            ),
            "unexpected_disconnect_count": metrics.unexpected_disconnects,
            "malformed_server_message_count": metrics.malformed_messages,
            "error_count": metrics.error_count,
            "errors_by_type": dict(sorted(metrics.errors_by_type.items())),
            **resource_summary,
            "laggy": bool(laggy_reasons),
            "laggy_reasons": laggy_reasons,
        }


def scalar_summary_row(summary: dict[str, Any]) -> dict[str, Any]:
    row: dict[str, Any] = {}
    for key, value in summary.items():
        if isinstance(value, (dict, list)):
            row[key] = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        elif value is None:
            row[key] = ""
        else:
            row[key] = value
    return row


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="")
    os.replace(temporary, path)


def csv_text(fieldnames: Iterable[str], rows: Iterable[dict[str, Any]]) -> str:
    from io import StringIO

    output = StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=list(fieldnames))
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def write_result_set(
    results_dir: Path,
    prefix: str,
    summary: dict[str, Any],
    latency_samples: list[float],
    resource_samples: list[ResourceSample],
) -> None:
    summary_json = results_dir / f"{prefix}_summary.json"
    summary_csv = results_dir / f"{prefix}_summary.csv"
    latency_csv = results_dir / f"{prefix}_latency_samples.csv"
    resource_csv = results_dir / f"{prefix}_resource_samples.csv"

    atomic_write_text(
        summary_json, json.dumps(summary, indent=2, ensure_ascii=False) + "\n"
    )
    summary_row = scalar_summary_row(summary)
    atomic_write_text(summary_csv, csv_text(summary_row.keys(), [summary_row]))
    latency_rows = [
        {"sample_index": index + 1, "latency_ms": f"{latency:.6f}"}
        for index, latency in enumerate(latency_samples)
    ]
    atomic_write_text(
        latency_csv,
        csv_text(("sample_index", "latency_ms"), latency_rows),
    )
    resource_rows = [
        {
            "elapsed_seconds": f"{sample.elapsed_seconds:.6f}",
            "cpu_percent": f"{sample.cpu_percent:.6f}",
            "rss_bytes": sample.rss_bytes,
        }
        for sample in resource_samples
    ]
    atomic_write_text(
        resource_csv,
        csv_text(
            ("elapsed_seconds", "cpu_percent", "rss_bytes"), resource_rows
        ),
    )


def write_results(
    results_dir_value: str,
    result_name: str | None,
    summary: dict[str, Any],
    latency_samples: list[float],
    resource_samples: list[ResourceSample],
) -> None:
    results_dir = Path(results_dir_value)
    write_result_set(
        results_dir, "latest", summary, latency_samples, resource_samples
    )
    if result_name and result_name != "latest":
        write_result_set(
            results_dir,
            result_name,
            summary,
            latency_samples,
            resource_samples,
        )


def format_metric(value: Any, digits: int = 2) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def print_summary(summary: dict[str, Any], results_dir_value: str) -> None:
    print("\nChatService benchmark complete")
    print(
        f"  connections: {summary['successful_connections']}/"
        f"{summary['total_users_requested']} "
        f"(failed {summary['failed_connections']})"
    )
    print(
        f"  messages: sent={summary['total_messages_sent']} "
        f"expected_deliveries={summary['expected_message_deliveries']} "
        f"received={summary['unique_message_deliveries']}"
    )
    print(
        f"  loss: {format_metric(summary['message_loss_rate_percent'])}%  "
        f"unexpected_disconnects={summary['unexpected_disconnect_count']}"
    )
    print(
        "  latency ms: "
        f"avg={format_metric(summary['average_latency_ms'])} "
        f"p50={format_metric(summary['p50_latency_ms'])} "
        f"p95={format_metric(summary['p95_latency_ms'])} "
        f"p99={format_metric(summary['p99_latency_ms'])} "
        f"max={format_metric(summary['max_latency_ms'])}"
    )
    print(
        "  throughput: "
        f"sent={format_metric(summary['sent_throughput_messages_per_second'])} "
        "messages/s "
        f"received={format_metric(summary['received_throughput_deliveries_per_second'])} "
        "deliveries/s"
    )
    print(
        f"  laggy: {'yes' if summary['laggy'] else 'no'}"
        + (
            f" ({', '.join(summary['laggy_reasons'])})"
            if summary["laggy_reasons"]
            else ""
        )
    )
    print(f"  results: {Path(results_dir_value).resolve()}")


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def nonnegative_int(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return parsed


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def nonnegative_float(value: str) -> float:
    parsed = float(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Headless asyncio benchmark for ChatService's newline-delimited "
            "TCP JSON protocol."
        )
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=positive_int, default=8888)
    parser.add_argument("--users", type=positive_int, default=100)
    parser.add_argument("--rooms", type=positive_int, default=10)
    parser.add_argument("--duration", type=positive_float, default=60.0)
    parser.add_argument(
        "--message-rate",
        type=nonnegative_float,
        default=1.0,
        help="messages per second for each non-idle sending user",
    )
    parser.add_argument("--mode", choices=VALID_MODES, default="room")
    parser.add_argument("--connect-timeout", type=positive_float, default=10.0)
    parser.add_argument("--operation-timeout", type=positive_float, default=10.0)
    parser.add_argument("--send-timeout", type=positive_float, default=10.0)
    parser.add_argument(
        "--grace-period",
        type=nonnegative_float,
        default=2.0,
        help="seconds to receive in-flight messages after sending stops",
    )
    parser.add_argument(
        "--connect-concurrency",
        type=nonnegative_int,
        default=0,
        help="limit simultaneous connection attempts; 0 means unlimited",
    )
    parser.add_argument(
        "--max-line-bytes", type=positive_int, default=1024 * 1024
    )
    parser.add_argument(
        "--max-latency-samples",
        type=positive_int,
        default=1_000_000,
        help="reservoir size used for percentile calculation and sample CSV",
    )
    parser.add_argument(
        "--duplicate-tracking-capacity",
        type=positive_int,
        default=1_000_000,
    )
    parser.add_argument("--server-pid", type=positive_int)
    parser.add_argument("--resource-interval", type=positive_float, default=1.0)
    parser.add_argument("--cpu-lag-threshold", type=positive_float, default=90.0)
    parser.add_argument("--cpu-lag-seconds", type=positive_float, default=5.0)
    parser.add_argument(
        "--results-dir",
        default=str(Path(__file__).resolve().parent / "results"),
    )
    parser.add_argument(
        "--result-name",
        help="also save a named result set in addition to latest_* files",
    )
    parser.add_argument("--seed", type=int, default=20260618)
    parser.add_argument(
        "--log-level",
        choices=("DEBUG", "INFO", "WARNING", "ERROR"),
        default="INFO",
    )
    return parser


def validate_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    if args.port > 65535:
        parser.error("--port must be at most 65535")
    if args.result_name:
        allowed = set(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."
        )
        if any(character not in allowed for character in args.result_name):
            parser.error(
                "--result-name may contain only letters, numbers, dash, "
                "underscore, and dot"
            )


async def async_main(args: argparse.Namespace) -> int:
    benchmark = Benchmark(args)
    await benchmark.run()
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    validate_args(parser, args)
    configure_logging(args.log_level)
    try:
        return asyncio.run(async_main(args))
    except KeyboardInterrupt:
        log_event(
            logging.WARNING,
            "benchmark_interrupted",
            "benchmark interrupted by user",
        )
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
