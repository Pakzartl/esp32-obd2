interface Env {
  DB: D1Database;
  API_KEY: string;
}

interface TelemetryRow {
  rpm?: number;
  speed?: number;
  throttle?: number;
  coolant_temp?: number;
  map_kpa?: number;
  iat?: number;
  engine_load?: number;
  ignition_timing?: number;
  raw_ble_hex?: string;
  recorded_at: string;
  trip_id?: string;
}

function json(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function authorize(request: Request, env: Env): boolean {
  const key = request.headers.get("X-API-Key");
  return key === env.API_KEY;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/api/health") {
      return json({ ok: true });
    }

    if (url.pathname === "/api/firmware/latest" && request.method === "GET") {
      return handleFirmwareLatest(env);
    }

    if (!authorize(request, env)) {
      return json({ error: "unauthorized" }, 401);
    }

    if (url.pathname === "/api/telemetry" && request.method === "POST") {
      return handleBatchInsert(request, env);
    }

    if (url.pathname === "/api/telemetry" && request.method === "GET") {
      return handleQuery(url, env);
    }

    return json({ error: "not found" }, 404);
  },
};

async function handleFirmwareLatest(env: Env): Promise<Response> {
  const res = await fetch(
    "https://api.github.com/repos/Pakzartl/esp32-obd2/releases/latest",
    { headers: { "User-Agent": "adv350-worker" } }
  );
  if (!res.ok) return json({ error: "github unavailable" }, 502);
  const release = await res.json<{ tag_name: string; body: string; assets: { name: string; size: number; browser_download_url: string }[] }>();
  const bin = release.assets?.find((a) => a.name.endsWith(".bin"));
  return json({
    version: release.tag_name?.replace(/^v/, ""),
    changelog: release.body ?? "",
    download_url: bin?.browser_download_url ?? null,
    size: bin?.size ?? 0,
  });
}

async function handleBatchInsert(request: Request, env: Env): Promise<Response> {
  const body = await request.json<{ device_id?: string; rows: TelemetryRow[] }>();
  const deviceId = body.device_id ?? "adv350-01";
  const rows = body.rows;

  if (!Array.isArray(rows) || rows.length === 0) {
    return json({ error: "rows must be a non-empty array" }, 400);
  }

  if (rows.length > 500) {
    return json({ error: "max 500 rows per batch" }, 400);
  }

  const stmt = env.DB.prepare(
    `INSERT INTO telemetry (device_id, rpm, speed, throttle, coolant_temp, map_kpa, iat, engine_load, ignition_timing, raw_ble_hex, recorded_at, trip_id)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
  );

  const batch = rows.map((r) =>
    stmt.bind(
      deviceId,
      r.rpm ?? null,
      r.speed ?? null,
      r.throttle ?? null,
      r.coolant_temp ?? null,
      r.map_kpa ?? null,
      r.iat ?? null,
      r.engine_load ?? null,
      r.ignition_timing ?? null,
      r.raw_ble_hex ?? null,
      r.recorded_at,
      r.trip_id ?? null
    )
  );

  const results = await env.DB.batch(batch);
  return json({ inserted: results.length });
}

async function handleQuery(url: URL, env: Env): Promise<Response> {
  const deviceId = url.searchParams.get("device_id") ?? "adv350-01";
  const since = url.searchParams.get("since");
  const until = url.searchParams.get("until");
  const limit = Math.min(Number(url.searchParams.get("limit") ?? 100), 1000);
  const tripId = url.searchParams.get("trip_id");

  let query = "SELECT * FROM telemetry WHERE device_id = ?";
  const params: unknown[] = [deviceId];

  if (since) {
    query += " AND recorded_at >= ?";
    params.push(since);
  }
  if (until) {
    query += " AND recorded_at <= ?";
    params.push(until);
  }
  if (tripId) {
    query += " AND trip_id = ?";
    params.push(tripId);
  }

  query += " ORDER BY recorded_at DESC LIMIT ?";
  params.push(limit);

  const result = await env.DB.prepare(query).bind(...params).all();
  return json({ rows: result.results, meta: result.meta });
}
