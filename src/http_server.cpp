#include "chronocore/http_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace chronocore {
namespace {
constexpr char kDashboard[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ChronoCore</title><style>
*{box-sizing:border-box}body{margin:0;background:#07111f;color:#dbeafe;font:15px ui-sans-serif,system-ui,sans-serif}main{max-width:1120px;margin:auto;padding:42px 24px}h1{font-size:38px;margin:0 0 6px}.tag{color:#7dd3fc}.grid{display:grid;grid-template-columns:2fr 1fr;gap:16px;margin-top:28px}.card{background:#0d1b2d;border:1px solid #1e3a5f;border-radius:12px;padding:18px}.live{color:#34d399;font-weight:700}table{width:100%;border-collapse:collapse;margin-top:10px}th,td{text-align:left;padding:12px 8px;border-bottom:1px solid #19324e}th{color:#93c5fd;font-size:12px;text-transform:uppercase}#alerts{display:grid;gap:10px}.alert{border-left:3px solid #fb7185;padding:10px;background:#241427;border-radius:4px}.empty{color:#94a3b8}.value{font:600 28px ui-monospace,SFMono-Regular,monospace}</style></head>
<body><main><p class="tag">CHRONOCORE / LIVE DEMO</p><h1>Hardware-aware regression detection</h1><p>Event-to-counter correlation window: <b>±500 ns</b> <span class="live">● streaming</span></p>
<section class="grid"><div class="card"><h2>Function latency</h2><table><thead><tr><th>Function</th><th>Samples</th><th>Mean</th><th>p99</th><th>L3 / event</th></tr></thead><tbody id="metrics"></tbody></table></div><div class="card"><h2>3σ alerts</h2><div id="alerts"><p class="empty">Establishing baseline…</p></div></div></section>
<p class="tag">Synthetic source injects an OrderBook regression after the configured warm-up. Replace it with a Linux PEBS/eBPF collector without changing the correlation or alerting API.</p></main>
<script>const n=x=>Number(x).toFixed(1);function render(d){document.querySelector('#metrics').innerHTML=d.functions.map(x=>`<tr><td>${x.function}</td><td>${x.samples}</td><td>${n(x.mean_latency_ns)} ns</td><td>${n(x.p99_latency_ns)} ns</td><td>${n(x.l3_misses_per_event)}</td></tr>`).join('');document.querySelector('#alerts').innerHTML=d.alerts.length?d.alerts.slice().reverse().map(x=>`<div class="alert"><b>${x.function}</b><br>Observed ${n(x.observed_latency_ns)} ns; baseline ${n(x.baseline_mean_ns)} ± ${n(x.baseline_sigma_ns)} ns</div>`).join(''):'<p class="empty">Establishing baseline…</p>'}async function refresh(){render(await (await fetch('/api/metrics')).json())}refresh();const stream=new EventSource('/api/stream');stream.addEventListener('metrics',e=>render(JSON.parse(e.data)));stream.onerror=()=>setTimeout(refresh,500);</script></body></html>)HTML";

std::string json_number(double value) { return std::to_string(value); }

std::string metrics_json(const CorrelationEngine& engine) {
  std::string json = "{\"functions\":[";
  bool first = true;
  for (const auto& metric : engine.metrics()) {
    if (!first) json += ',';
    first = false;
    json += "{\"function\":\"" + metric.function + "\",\"samples\":" + std::to_string(metric.samples) +
        ",\"mean_latency_ns\":" + json_number(metric.mean_latency_ns) + ",\"p99_latency_ns\":" +
        json_number(metric.p99_latency_ns) + ",\"l3_misses_per_event\":" + json_number(metric.l3_misses_per_event) +
        ",\"branch_misses_per_event\":" + json_number(metric.branch_misses_per_event) + "}";
  }
  json += "],\"alerts\":[";
  first = true;
  for (const auto& alert : engine.recent_alerts()) {
    if (!first) json += ',';
    first = false;
    json += "{\"function\":\"" + alert.function + "\",\"observed_latency_ns\":" +
        json_number(alert.observed_latency_ns) + ",\"baseline_mean_ns\":" + json_number(alert.baseline_mean_ns) +
        ",\"baseline_sigma_ns\":" + json_number(alert.baseline_sigma_ns) + ",\"timestamp_ns\":" +
        std::to_string(alert.timestamp_ns) + "}";
  }
  return json + "]}";
}

void respond(int client, const char* status, const char* type, const std::string& body) {
  const std::string headers = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
      "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  (void)send(client, headers.data(), headers.size(), 0);
  (void)send(client, body.data(), body.size(), 0);
}

bool send_all(int client, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto result = send(client, data.data() + sent, data.size() - sent, 0);
    if (result <= 0) return false;
    sent += static_cast<std::size_t>(result);
  }
  return true;
}
}  // namespace

HttpServer::HttpServer(const CorrelationEngine& engine, std::uint16_t port) : engine_(engine), port_(port) {}

void HttpServer::serve_forever() const {
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) throw std::runtime_error("could not create HTTP socket");
  const int enabled = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port_);
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 16) < 0) {
    close(server);
    throw std::runtime_error("could not bind HTTP server to localhost:" + std::to_string(port_));
  }
  std::cout << "ChronoCore dashboard: http://127.0.0.1:" << port_ << '\n';
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) { if (errno == EINTR) continue; break; }
    char request[2048]{};
    const auto bytes = recv(client, request, sizeof(request) - 1, 0);
    const std::string request_line(request, bytes > 0 ? static_cast<std::size_t>(bytes) : 0);
    std::thread([this, client, request_line] {
      if (request_line.starts_with("GET /api/health ")) {
        respond(client, "200 OK", "application/json", "{\"status\":\"ok\"}");
      } else if (request_line.starts_with("GET /api/metrics ")) {
        respond(client, "200 OK", "application/json", metrics_json(engine_));
      } else if (request_line.starts_with("GET /api/stream ")) {
        const std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
        if (send_all(client, headers)) {
          auto generation = std::uint64_t{0};
          while (true) {
            const bool changed = engine_.wait_for_update(generation, std::chrono::seconds(5));
            const auto next_generation = engine_.generation();
            if (changed) {
              if (!send_all(client, "event: metrics\ndata: " + metrics_json(engine_) + "\n\n")) break;
              generation = next_generation;
            } else if (!send_all(client, ": keepalive\n\n")) {
              break;
            }
          }
        }
      } else if (request_line.starts_with("GET / ")) {
        respond(client, "200 OK", "text/html; charset=utf-8", kDashboard);
      } else {
        respond(client, "404 Not Found", "text/plain", "Not found\n");
      }
      close(client);
    }).detach();
  }
  close(server);
}

}  // namespace chronocore
