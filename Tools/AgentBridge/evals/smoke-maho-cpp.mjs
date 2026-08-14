import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { existsSync } from "node:fs";
import { createServer } from "node:net";
import { resolve } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorReasons,
} from "../src/world/world-adapter-errors.mjs";

const READY_PREFIX = "MAHO_WORLD_ADAPTER_READY ";
const MINIMAL_WORLD_PROFILE = Object.freeze({
  supports_atomic_transactions: false,
  supports_dry_run: false,
  supports_undo: false,
  supports_idempotency: true,
  max_tool_calls: 1,
  supported_tools: Object.freeze([
    "world.get_summary",
    "entity.spawn_primitive",
    "entity.set_transform",
  ]),
});

function toolCall(tool_name, args = {}) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

function executeRequest(identity, expected_revision, tool_call) {
  return {
    request_id: randomUUID(),
    ...identity,
    expected_revision,
    dry_run: false,
    atomic: false,
    tool_calls: [tool_call],
  };
}

function waitForExit(child, timeout_ms) {
  if (child.exitCode !== null || child.signalCode !== null) {
    return Promise.resolve();
  }
  return new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error("C++ Harness exit timed out"));
    }, timeout_ms);
    const onExit = () => {
      cleanup();
      resolvePromise();
    };
    const cleanup = () => {
      clearTimeout(timer);
      child.off("exit", onExit);
    };
    child.once("exit", onExit);
  });
}

function waitForReady(child, error_lines, timeout_ms = 10_000) {
  const lines = createInterface({ input: child.stdout });
  return new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error("C++ Harness ready line timed out"));
    }, timeout_ms);
    const onLine = (line) => {
      if (!line.startsWith(READY_PREFIX)) {
        return;
      }
      try {
        const ready = JSON.parse(line.slice(READY_PREFIX.length));
        assert.deepEqual(Object.keys(ready).sort(), ["host", "port", "protocol"]);
        assert.equal(ready.host, "127.0.0.1");
        assert.equal(ready.protocol, "1.0");
        assert.equal(Number.isInteger(ready.port) && ready.port > 0 && ready.port <= 65535, true);
        cleanup();
        resolvePromise(ready);
      } catch (error) {
        cleanup();
        reject(new Error(`C++ Harness ready line is invalid: ${error.message}`));
      }
    };
    const onExit = (code) => {
      cleanup();
      const detail = error_lines.join("").trim();
      reject(new Error(`C++ Harness exited before ready (code ${code})${detail ? `: ${detail}` : ""}`));
    };
    const onError = (error) => {
      cleanup();
      reject(new Error(`C++ Harness failed to start: ${error.message}`));
    };
    const cleanup = () => {
      clearTimeout(timer);
      lines.off("line", onLine);
      child.off("exit", onExit);
      child.off("error", onError);
      lines.close();
    };
    lines.on("line", onLine);
    child.once("exit", onExit);
    child.once("error", onError);
  });
}

async function stopHarness(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) {
    return;
  }
  if (child.stdin.writable) {
    child.stdin.write("shutdown\n");
  }
  try {
    await waitForExit(child, 5_000);
  } catch {
    child.kill();
    await waitForExit(child, 5_000);
  }
}

function confirmPortReleased(port, timeout_ms = 5_000) {
  return new Promise((resolvePromise, reject) => {
    const server = createServer();
    const timer = setTimeout(() => {
      server.close();
      reject(new Error("Port release check timed out"));
    }, timeout_ms);
    server.once("error", (error) => {
      clearTimeout(timer);
      reject(new Error(`Harness port was not released: ${error.message}`));
    });
    server.listen({ host: "127.0.0.1", port, exclusive: true }, () => {
      server.close((error) => {
        clearTimeout(timer);
        if (error) {
          reject(new Error(`Port release listener failed to close: ${error.message}`));
          return;
        }
        resolvePromise();
      });
    });
  });
}

export async function main({
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  const started_at = performance.now();
  const configured_harness = process.env.MAHO_WORLD_ADAPTER_HARNESS;
  if (!configured_harness) {
    error_output.write("Maho C++ smoke FAIL: MAHO_WORLD_ADAPTER_HARNESS is required\n");
    return 1;
  }
  const harness_path = resolve(configured_harness);
  if (!existsSync(harness_path)) {
    error_output.write("Maho C++ smoke FAIL: configured Harness does not exist\n");
    return 1;
  }

  const error_lines = [];
  let adapter;
  let child;
  let port;
  let primary_error;
  try {
    child = spawn(harness_path, ["--port", "0"], {
      env: process.env,
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", (chunk) => {
      if (error_lines.join("").length < 16_384) {
        error_lines.push(chunk);
      }
    });
    const ready = await waitForReady(child, error_lines);
    port = ready.port;

    const identity = {
      session_id: randomUUID(),
      world_id: randomUUID(),
    };
    adapter = new RemoteWorldAdapter({
      ...identity,
      tool_registry: createDefaultToolRegistry(),
      base_url: `http://127.0.0.1:${port}`,
      timeout_ms: 5_000,
      auth_token: process.env.MAHO_WORLD_AUTH_TOKEN || "",
    });

    const health = await adapter.health();
    assert.equal(health.ok, true);
    assert.deepEqual(health.capabilities, MINIMAL_WORLD_PROFILE);
    assert.deepEqual(adapter.capabilities, MINIMAL_WORLD_PROFILE);

    const initial = await adapter.getSnapshot({ request_id: randomUUID(), ...identity });
    assert.equal(initial.revision, 0);
    assert.deepEqual(initial.entities, []);

    const summary = await adapter.executeTransaction(
      executeRequest(identity, 0, toolCall("world.get_summary"))
    );
    assert.equal(summary.ok, true);
    assert.equal(summary.before_revision, 0);
    assert.equal(summary.after_revision, 0);
    assert.equal(summary.tool_results[0].data.entity_count, 0);

    const spawn_request = executeRequest(
      identity,
      0,
      toolCall("entity.spawn_primitive", {
        primitive_type: "cube",
        name: "CppSmokeCube",
      })
    );
    const spawned = await adapter.executeTransaction(spawn_request);
    const replayed = await adapter.executeTransaction(spawn_request);
    const entity_id = spawned.tool_results[0]?.data?.entity?.entity_id;
    assert.equal(spawned.ok, true);
    assert.equal(spawned.before_revision, 0);
    assert.equal(spawned.after_revision, 1);
    assert.equal(spawned.replayed, false);
    assert.match(entity_id, /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i);
    assert.equal(replayed.ok, true);
    assert.equal(replayed.replayed, true);
    assert.equal(replayed.after_revision, 1);
    assert.equal(replayed.tool_results[0].data.entity.entity_id, entity_id);

    const transformed = await adapter.executeTransaction(
      executeRequest(
        identity,
        1,
        toolCall("entity.set_transform", {
          entity_id,
          transform: { position: [3, 1, 5] },
        })
      )
    );
    assert.equal(transformed.ok, true);
    assert.equal(transformed.before_revision, 1);
    assert.equal(transformed.after_revision, 2);

    const final = await adapter.getSnapshot({ request_id: randomUUID(), ...identity });
    assert.equal(final.revision, 2);
    assert.equal(final.entities.length, 1);
    assert.equal(final.entities[0].entity_id, entity_id);
    assert.deepEqual(final.entities[0].transform.position, [3, 1, 5]);

    await assert.rejects(
      adapter.undo({
        request_id: randomUUID(),
        ...identity,
        expected_revision: 2,
        undo_token: null,
      }),
      (error) =>
        error instanceof WorldAdapterError &&
        error.reason === worldAdapterErrorReasons.UNDO_NOT_AVAILABLE
    );

    await adapter.close();
    adapter = null;
    await stopHarness(child);
    await confirmPortReleased(port);
    output.write(
      `Maho C++ smoke PASS adapter=remote profile=minimal revision=2 replayed=true port_released=true duration_ms=${Math.round(performance.now() - started_at)}\n`
    );
    return 0;
  } catch (error) {
    primary_error = error;
    error_output.write(`Maho C++ smoke FAIL: ${error?.message || String(error)}\n`);
    return 1;
  } finally {
    await adapter?.close().catch(() => {});
    await stopHarness(child).catch((cleanup_error) => {
      error_output.write(`Maho C++ smoke cleanup FAIL: ${cleanup_error.message}\n`);
    });
    if (primary_error && port) {
      await confirmPortReleased(port).catch((cleanup_error) => {
        error_output.write(`Maho C++ smoke port cleanup FAIL: ${cleanup_error.message}\n`);
      });
    }
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
