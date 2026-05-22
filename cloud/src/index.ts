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
  fuel_rate_lph?: number;
  cvt_ratio?: number;
  riding_score?: number;
  board_temp?: number;
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
      return handleFirmwareLatest(env, url);
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

async function handleFirmwareLatest(env: Env, url?: URL): Promise<Response> {
  const component = url?.searchParams.get("component") ?? "firmware-s3";
  const row = await env.DB.prepare(
    "SELECT component, version, changelog, download_url, size FROM firmware WHERE component = ? ORDER BY id DESC LIMIT 1"
  ).bind(component).first<{ component: string; version: string; changelog: string; download_url: string; size: number }>();
  if (!row) return json({ error: "no firmware release found" }, 404);
  return json(row);
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
    `INSERT INTO telemetry (device_id, rpm, speed, throttle, coolant_temp, map_kpa, iat, engine_load, ignition_timing, raw_ble_hex, fuel_rate_lph, cvt_ratio, riding_score, board_temp, recorded_at, trip_id)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
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
      r.fuel_rate_lph ?? null,
      r.cvt_ratio ?? null,
      r.riding_score ?? null,
      r.board_temp ?? null,
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

async function handleBackfill(env: Env): Promise<Response> {
  const rows = await env.DB.prepare(
    `SELECT id, raw_ble_hex FROM telemetry
     WHERE raw_ble_hex != '' AND length(raw_ble_hex) >= 32
     AND fuel_rate_lph = 0`
  ).all<{ id: number; raw_ble_hex: string }>();

  if (!rows.results.length) return json({ backfilled: 0 });

  const stmts = [];
  for (const row of rows.results) {
    const hex = row.raw_ble_hex;
    try {
      const fuelLo = parseInt(hex.substring(22, 24), 16);
      const fuelHi = parseInt(hex.substring(24, 26), 16);
      const cvtLo = parseInt(hex.substring(26, 28), 16);
      const cvtHi = parseInt(hex.substring(28, 30), 16);
      const score = hex.length >= 32 ? parseInt(hex.substring(30, 32), 16) : 0;
      const fuelRate = (fuelLo | (fuelHi << 8)) / 100.0;
      const cvtRatio = (cvtLo | (cvtHi << 8)) / 100.0;
      const boardTemp = hex.length >= 34 ? parseInt(hex.substring(32, 34), 16) - 40 : 0;

      stmts.push(
        env.DB.prepare(
          `UPDATE telemetry SET fuel_rate_lph=?, cvt_ratio=?, riding_score=?, board_temp=? WHERE id=?`
        ).bind(fuelRate, cvtRatio, score, boardTemp, row.id)
      );
    } catch {}
  }

  if (stmts.length) await env.DB.batch(stmts);
  return json({ backfilled: stmts.length });
}
