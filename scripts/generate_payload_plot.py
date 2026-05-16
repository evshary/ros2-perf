#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


SERIES = {
    "latency": [
        {"label": "min", "key": "min_rtt_us", "color": "#0f766e"},
        {"label": "median", "key": "median_rtt_us", "color": "#2563eb"},
        {"label": "max", "key": "max_rtt_us", "color": "#dc2626"},
    ],
    "throughput": [
        {"label": "min", "key": "min_mib_per_sec", "color": "#0f766e"},
        {"label": "median", "key": "median_mib_per_sec", "color": "#2563eb"},
        {"label": "max", "key": "max_mib_per_sec", "color": "#dc2626"},
    ],
}

Y_AXIS_LABEL = {
    "latency": "RTT (us)",
    "throughput": "Throughput (MiB/s)",
}

TITLES = {
    "latency": "Latency Payload Sweep",
    "throughput": "Throughput Payload Sweep",
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", choices=sorted(SERIES.keys()), required=True)
    parser.add_argument("--input-dir", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def load_runs(benchmark: str, input_dir: Path):
    runs_by_payload = {}
    for path in sorted(input_dir.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("benchmark_type") != benchmark or data.get("status") != "ok":
            continue
        payload_size = data["parameters"]["payload_size_bytes"]
        previous = runs_by_payload.get(payload_size)
        if previous is None or data.get("timestamp", "") >= previous.get("timestamp", ""):
            runs_by_payload[payload_size] = data

    runs = list(runs_by_payload.values())
    runs.sort(key=lambda item: item["parameters"]["payload_size_bytes"])
    if not runs:
        raise RuntimeError(f"no successful {benchmark} result files found in {input_dir}")
    return runs


def build_chart_rows(benchmark: str, runs):
    rows = []
    for run in runs:
        payload = run["parameters"]["payload_size_bytes"]
        summary = run["summary"]
        row = {"payload_size_bytes": payload}
        for entry in SERIES[benchmark]:
            row[entry["label"]] = summary[entry["key"]]
        rows.append(row)
    return rows


def build_metadata(runs):
    first = runs[0]
    qos = first["qos"]
    qos_text = f'{qos["reliability"]} / {qos["durability"]} / {qos["history"]}'
    if qos["history"] == "KEEP_LAST":
        qos_text += f'({qos["history_depth"]})'

    return {
        "rmw_implementation": first.get("rmw_implementation", "UNSET"),
        "ros_distro": first.get("ros_distro", "UNSET"),
        "qos_text": qos_text,
        "timestamp": first["timestamp"],
    }


def render_html(benchmark: str, rows, metadata):
    rows_json = json.dumps(rows)
    metadata_json = json.dumps(metadata)
    series_json = json.dumps(SERIES[benchmark])
    y_axis = Y_AXIS_LABEL[benchmark]
    title = TITLES[benchmark]

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{title}</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f7f4ec;
      --card: #fffdfa;
      --line: #d8d0c2;
      --text: #1f2937;
      --muted: #6b7280;
      --accent: #b45309;
      --active: #fff5e6;
    }}
    * {{
      box-sizing: border-box;
    }}
    body {{
      margin: 0;
      font-family: "Iowan Old Style", "Palatino Linotype", "Book Antiqua", serif;
      background:
        radial-gradient(circle at top left, rgba(180, 83, 9, 0.12), transparent 30%),
        linear-gradient(180deg, #f8f5ef 0%, var(--bg) 100%);
      color: var(--text);
    }}
    .page {{
      max-width: 1240px;
      margin: 0 auto;
      padding: 32px 20px 48px;
    }}
    .hero {{
      margin-bottom: 24px;
      padding: 24px;
      border: 1px solid rgba(180, 83, 9, 0.18);
      background: rgba(255, 253, 250, 0.92);
      border-radius: 20px;
      box-shadow: 0 24px 80px rgba(120, 53, 15, 0.08);
    }}
    h1 {{
      margin: 0 0 10px;
      font-size: clamp(2rem, 5vw, 3.6rem);
      line-height: 1;
      letter-spacing: -0.03em;
    }}
    .subtitle {{
      margin: 0;
      color: var(--muted);
      font-size: 1rem;
    }}
    .grid {{
      display: grid;
      gap: 20px;
      grid-template-columns: 1.9fr 1fr;
    }}
    .card {{
      position: relative;
      padding: 18px;
      border: 1px solid var(--line);
      border-radius: 18px;
      background: var(--card);
      box-shadow: 0 18px 54px rgba(15, 23, 42, 0.06);
    }}
    .chart-wrap {{
      min-height: 560px;
    }}
    .controls {{
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-bottom: 14px;
      font-family: "Helvetica Neue", Arial, sans-serif;
    }}
    .toggle {{
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 8px 12px;
      border: 1px solid var(--line);
      border-radius: 999px;
      background: #ffffff;
      color: var(--muted);
      cursor: pointer;
      user-select: none;
      transition: background 120ms ease, border-color 120ms ease, opacity 120ms ease;
    }}
    .toggle.active {{
      background: var(--active);
      border-color: rgba(180, 83, 9, 0.4);
      color: var(--text);
    }}
    .toggle input {{
      margin: 0;
    }}
    .swatch {{
      width: 14px;
      height: 14px;
      border-radius: 999px;
      display: inline-block;
    }}
    svg {{
      width: 100%;
      height: auto;
      overflow: visible;
    }}
    .tooltip {{
      position: absolute;
      display: none;
      pointer-events: none;
      padding: 10px 12px;
      border-radius: 12px;
      background: rgba(17, 24, 39, 0.92);
      color: #fff;
      font-family: "Helvetica Neue", Arial, sans-serif;
      font-size: 0.9rem;
      box-shadow: 0 12px 32px rgba(17, 24, 39, 0.24);
      transform: translate(12px, -12px);
      white-space: nowrap;
      z-index: 2;
    }}
    .meta-list {{
      display: grid;
      gap: 12px;
      margin: 0;
      padding: 0;
      list-style: none;
      font-family: "Helvetica Neue", Arial, sans-serif;
    }}
    .meta-list strong {{
      display: block;
      margin-bottom: 4px;
      color: var(--accent);
      font-size: 0.82rem;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      font-family: "Helvetica Neue", Arial, sans-serif;
      font-size: 0.93rem;
    }}
    th,
    td {{
      padding: 10px 8px;
      text-align: right;
      border-bottom: 1px solid var(--line);
    }}
    th:first-child,
    td:first-child {{
      text-align: left;
    }}
    @media (max-width: 900px) {{
      .grid {{
        grid-template-columns: 1fr;
      }}
    }}
  </style>
</head>
<body>
  <div class="page">
    <section class="hero">
      <h1>{title}</h1>
      <p class="subtitle">
        Payload sweep with interactive min, median, and max toggles.
        The y-axis auto-rescales to the visible series.
      </p>
    </section>

    <section class="grid">
      <article class="card chart-wrap">
        <div class="controls" id="controls"></div>
        <svg id="chart" viewBox="0 0 920 540" role="img" aria-label="{title} chart"></svg>
        <div class="tooltip" id="tooltip"></div>
      </article>

      <article class="card">
        <ul class="meta-list">
          <li><strong>Runs</strong>{len(rows)} payload sizes</li>
          <li><strong>ROS Distro</strong><span id="ros-distro-value"></span></li>
          <li><strong>RMW Implementation</strong><span id="rmw-value"></span></li>
          <li><strong>QoS</strong><span id="qos-value"></span></li>
          <li><strong>Timestamp</strong><span id="timestamp-value"></span></li>
          <li><strong>Y axis</strong>{y_axis}</li>
        </ul>
      </article>
    </section>

    <section class="card" style="margin-top: 20px;">
      <table>
        <thead>
          <tr>
            <th>Payload (bytes)</th>
            <th>Min</th>
            <th>Median</th>
            <th>Max</th>
          </tr>
        </thead>
        <tbody id="summary-table"></tbody>
      </table>
    </section>
  </div>

  <script>
    const rows = {rows_json};
    const metadata = {metadata_json};
    const series = {series_json};
    const yAxisLabel = {json.dumps(y_axis)};
    const svg = document.getElementById("chart");
    const tooltip = document.getElementById("tooltip");
    const controls = document.getElementById("controls");
    const table = document.getElementById("summary-table");
    const width = 920;
    const height = 540;
    const margin = {{ top: 28, right: 34, bottom: 72, left: 92 }};
    const innerWidth = width - margin.left - margin.right;
    const innerHeight = height - margin.top - margin.bottom;
    const visible = new Set(series.map((entry) => entry.label));

    document.getElementById("ros-distro-value").textContent = metadata.ros_distro;
    document.getElementById("rmw-value").textContent = metadata.rmw_implementation;
    document.getElementById("qos-value").textContent = metadata.qos_text;
    document.getElementById("timestamp-value").textContent = metadata.timestamp;

    function addSvg(tag, attrs, parent = svg) {{
      const element = document.createElementNS("http://www.w3.org/2000/svg", tag);
      Object.entries(attrs).forEach(([key, value]) => element.setAttribute(key, String(value)));
      parent.appendChild(element);
      return element;
    }}

    function xScale(value, minX, maxX) {{
      const minLog = Math.log10(minX);
      const maxLog = Math.log10(maxX);
      const ratio = (Math.log10(value) - minLog) / (maxLog - minLog || 1);
      return margin.left + ratio * innerWidth;
    }}

    function computeYRange() {{
      const activeSeries = series.filter((entry) => visible.has(entry.label));
      const sourceSeries = activeSeries.length === 0 ? [series[1]] : activeSeries;
      const values = rows.flatMap((row) => sourceSeries.map((entry) => row[entry.label]));
      const minY = Math.min(...values);
      const maxY = Math.max(...values);
      const padding = (maxY - minY || 1) * 0.12;
      return {{
        low: Math.max(0, minY - padding),
        high: maxY + padding,
      }};
    }}

    function yScale(value, range) {{
      const ratio = (value - range.low) / (range.high - range.low || 1);
      return margin.top + innerHeight - ratio * innerHeight;
    }}

    function drawAxes(range) {{
      const xValues = rows.map((row) => row.payload_size_bytes);
      const minX = Math.min(...xValues);
      const maxX = Math.max(...xValues);

      addSvg("rect", {{
        x: margin.left,
        y: margin.top,
        width: innerWidth,
        height: innerHeight,
        fill: "#fffdfa",
        rx: 16,
      }});

      const yTicks = 5;
      for (let index = 0; index <= yTicks; index += 1) {{
        const value = range.low + (range.high - range.low) * (index / yTicks);
        const y = yScale(value, range);
        addSvg("line", {{
          x1: margin.left,
          y1: y,
          x2: margin.left + innerWidth,
          y2: y,
          stroke: "#e7dfd2",
          "stroke-width": 1,
        }});
        const label = addSvg("text", {{
          x: margin.left - 12,
          y: y + 4,
          "text-anchor": "end",
          fill: "#6b7280",
          "font-size": 12,
          "font-family": "Helvetica Neue, Arial, sans-serif",
        }});
        label.textContent = value.toFixed(2);
      }}

      xValues.forEach((value) => {{
        const x = xScale(value, minX, maxX);
        addSvg("line", {{
          x1: x,
          y1: margin.top,
          x2: x,
          y2: margin.top + innerHeight,
          stroke: "#efe7da",
          "stroke-width": 1,
        }});
        const label = addSvg("text", {{
          x,
          y: margin.top + innerHeight + 22,
          "text-anchor": "middle",
          fill: "#6b7280",
          "font-size": 12,
          "font-family": "Helvetica Neue, Arial, sans-serif",
        }});
        label.textContent = String(value);
      }});

      const xAxisLabel = addSvg("text", {{
        x: margin.left + innerWidth / 2,
        y: height - 18,
        "text-anchor": "middle",
        fill: "#374151",
        "font-size": 13,
        "font-family": "Helvetica Neue, Arial, sans-serif",
      }});
      xAxisLabel.textContent = "Payload size (bytes, log scale)";

      const yLabel = addSvg("text", {{
        x: 26,
        y: margin.top + innerHeight / 2,
        transform: `rotate(-90 26 ${{margin.top + innerHeight / 2}})`,
        "text-anchor": "middle",
        fill: "#374151",
        "font-size": 13,
        "font-family": "Helvetica Neue, Arial, sans-serif",
      }});
      yLabel.textContent = yAxisLabel;

      return {{ minX, maxX }};
    }}

    function drawSeries(range, xBounds) {{
      series.forEach((entry) => {{
        if (!visible.has(entry.label)) {{
          return;
        }}

        const points = rows
          .map(
            (row) =>
              `${{xScale(row.payload_size_bytes, xBounds.minX, xBounds.maxX)}},` +
              `${{yScale(row[entry.label], range)}}`,
          )
          .join(" ");
        addSvg("polyline", {{
          points,
          fill: "none",
          stroke: entry.color,
          "stroke-width": 3,
          "stroke-linecap": "round",
          "stroke-linejoin": "round",
        }});

        rows.forEach((row) => {{
          const circle = addSvg("circle", {{
            cx: xScale(row.payload_size_bytes, xBounds.minX, xBounds.maxX),
            cy: yScale(row[entry.label], range),
            r: 5,
            fill: entry.color,
            stroke: "#fffdfa",
            "stroke-width": 2,
            style: "cursor:pointer",
          }});
          circle.addEventListener("mousemove", (event) => {{
            tooltip.style.display = "block";
            tooltip.style.left = `${{event.offsetX}}px`;
            tooltip.style.top = `${{event.offsetY}}px`;
            tooltip.textContent =
              `payload=${{row.payload_size_bytes}} bytes, ` +
              `${{entry.label}}=${{row[entry.label].toFixed(2)}}`;
          }});
          circle.addEventListener("mouseleave", () => {{
            tooltip.style.display = "none";
          }});
        }});
      }});
    }}

    function buildControls() {{
      series.forEach((entry) => {{
        const label = document.createElement("label");
        label.className = "toggle active";
        label.innerHTML = `
          <input type="checkbox" checked />
          <span class="swatch" style="background:${{entry.color}}"></span>
          <span>${{entry.label}}</span>
        `;
        const input = label.querySelector("input");
        input.addEventListener("change", () => {{
          if (input.checked) {{
            visible.add(entry.label);
            label.classList.add("active");
          }} else {{
            visible.delete(entry.label);
            label.classList.remove("active");
          }}
          renderChart();
        }});
        controls.appendChild(label);
      }});
    }}

    function fillTable() {{
      rows.forEach((row) => {{
        const tr = document.createElement("tr");
        tr.innerHTML = `
          <td>${{row.payload_size_bytes}}</td>
          <td>${{row.min.toFixed(2)}}</td>
          <td>${{row.median.toFixed(2)}}</td>
          <td>${{row.max.toFixed(2)}}</td>
        `;
        table.appendChild(tr);
      }});
    }}

    function renderChart() {{
      svg.replaceChildren();
      tooltip.style.display = "none";
      const range = computeYRange();
      const xBounds = drawAxes(range);
      drawSeries(range, xBounds);
    }}

    buildControls();
    fillTable();
    renderChart();
  </script>
</body>
</html>
"""


def main():
    args = parse_args()
    input_dir = Path(args.input_dir)
    output = Path(args.output)
    runs = load_runs(args.benchmark, input_dir)
    rows = build_chart_rows(args.benchmark, runs)
    metadata = build_metadata(runs)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_html(args.benchmark, rows, metadata), encoding="utf-8")
    print(f"Generated plot: {output}")


if __name__ == "__main__":
    main()
