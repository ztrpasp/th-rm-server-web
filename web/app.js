(() => {
  "use strict";

  const state = { after: "0", logs: [], logPage: 1, logPageSize: 25, samples: [], refreshing: false,
    logFilters: { level: "", module: "", event: "", keyword: "" } };
  const $ = (id) => document.getElementById(id);
  const pages = {
    overview: ["运行概览", "实时查看 RM 服务状态和数据面关键运行情况。"],
    metrics: ["运行指标", "按报文类型查看累计计数与处理结果。"],
    "packet-stats": ["报文统计", "按报文类型查看接收量与各类处理结果。"],
    ports: ["端口状态", "查看 DPDK 端口链路、收发包和错误统计。"],
    "log-stats": ["日志队列", "查看日志接收、丢弃、限速和历史范围。"],
    traces: ["报文追踪", "查看 ANN 与 GET 对应的完整 AS 计数。"],
    logs: ["系统日志", "查看最近保留的结构化运行事件。"],
    "sid-acl": ["资源管理", "查看系统全部 SID，管理 ACL 访问控制规则。"]
  };

  const metricLabels = {
    rx_packets: "接收总包数", rx_non_ipv4: "非 IPv4 接收数",
    rx_non_color: "非 CoLoR 接收数", rx_unsupported: "不支持类型接收数",
    tx_packets: "发送总包数", tx_dropped: "发送丢弃数",
    ann_from_inside: "ANN 来自内部", ann_from_outside: "ANN 来自外部",
    ann_outbound: "ANN 出站数", ann_filtered: "ANN 过滤计数",
    get_from_inside: "GET 来自内部", get_from_outside: "GET 来自外部",
    get_to_inside: "GET 发往内部", get_to_outside: "GET 发往外部",
    get_filtered: "GET 过滤计数", proxy_registered: "Proxy 注册数",
    log_dropped: "日志丢弃数", log_suppressed: "日志限速数"
  };
  const resultLabels = {
    ok: "处理成功", bad_checksum: "校验和错误", malformed: "报文格式错误",
    sid_not_found: "SID 未找到", acl_denied: "ACL 拒绝", policy_denied: "策略拒绝",
    no_route: "无路由", no_memory: "内存不足", sid_list_returned: "SID 列表应答",
    internal_error: "内部错误", unknown: "未知结果"
  };
  const metricTypes = ["ann", "get", "control", "arp"];
  const typeCounterKeys = {
    ann: ["ann_from_inside", "ann_from_outside", "ann_outbound", "ann_filtered"],
    get: ["get_from_inside", "get_from_outside", "get_to_inside", "get_to_outside", "get_filtered"],
    control: ["proxy_registered"]
  };
  const packetLabels = { ann: "ANN", get: "GET", control: "CONTROL", arp: "ARP" };
  const fieldLabels = {
    port_id: "端口 ID", link_up: "链路已连接", link_speed_mbps: "链路速率 (Mbps)",
    rx_packets: "RX 包数", rx_bytes: "RX 字节数", rx_errors: "RX 错误数",
    rx_missed: "RX missed", rx_no_mbuf: "RX no-mbuf", tx_packets: "TX 包数",
    tx_bytes: "TX 字节数", tx_errors: "TX 错误数", accepted: "已接收日志数",
    dropped: "丢弃日志数", suppressed: "限速日志数", oldest_sequence: "最早序号",
    newest_sequence: "最新序号", history_count: "历史条数"
  };

  function formatInteger(value) {
    const text = String(value);
    if (!/^-?\d+$/.test(text)) return text;
    try { return BigInt(text).toLocaleString("zh-CN"); }
    catch (_) { return text; }
  }

  function displayValue(value) {
    if (value === null) return "null";
    if (value === undefined) return "—";
    if (typeof value === "boolean") return String(value);
    if (typeof value === "string" && /^-?\d+$/.test(value)) return formatInteger(value);
    return String(value);
  }

  // Preserve uint64 JSON values exactly; JSON.parse would round values above 2^53.
  function parseLosslessJson(text) {
    let output = "";
    let inString = false;
    let escaped = false;
    for (let i = 0; i < text.length;) {
      const character = text[i];
      if (inString) {
        output += character;
        if (escaped) escaped = false;
        else if (character === "\\") escaped = true;
        else if (character === '"') inString = false;
        i += 1;
        continue;
      }
      if (character === '"') {
        inString = true;
        output += character;
        i += 1;
        continue;
      }
      if ((character >= "0" && character <= "9") ||
          (character === "-" && text[i + 1] >= "0" && text[i + 1] <= "9")) {
        const match = text.slice(i).match(/^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/);
        if (match) {
          const token = match[0];
          output += /[.eE]/.test(token) ? token : JSON.stringify(token);
          i += token.length;
          continue;
        }
      }
      output += character;
      i += 1;
    }
    return JSON.parse(output);
  }

  async function json(path) {
    const response = await fetch(path, { cache: "no-store" });
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    return parseLosslessJson(await response.text());
  }

  function setConnection(connected) {
    const element = $("connection");
    element.className = `hw-status ${connected ? "hw-status-ok" : "hw-status-error"}`;
    element.querySelector("span").textContent = connected ? "运行正常" : "连接失败";
  }

  function setActivePage(page, updateLocation = true) {
    const selectedPage = Object.prototype.hasOwnProperty.call(pages, page) ? page : "overview";
    const previousPage = document.querySelector(".hw-view.active")?.dataset.view;
    document.querySelectorAll(".hw-view").forEach((view) => {
      const active = view.dataset.view === selectedPage;
      view.classList.toggle("active", active);
      view.setAttribute("aria-hidden", String(!active));
    });
    document.querySelectorAll(".hw-nav-item").forEach((item) => {
      const active = item.dataset.page === selectedPage;
      item.classList.toggle("active", active);
      if (active) item.setAttribute("aria-current", "page");
      else item.removeAttribute("aria-current");
    });
    $("current-page").textContent = pages[selectedPage][0];
    $("page-title").textContent = pages[selectedPage][0];
    $("page-description").textContent = pages[selectedPage][1];
    if (previousPage && previousPage !== selectedPage) {
      const content = document.querySelector(".hw-content");
      if (content) content.scrollTop = 0;
    }
    if (updateLocation && window.location.hash !== `#${selectedPage}`)
      window.location.hash = selectedPage;
    if (selectedPage === "overview") window.requestAnimationFrame(drawChart);
    if (selectedPage === "sid-acl") refreshSidAcl().catch(() => {});
  }

  function appendCell(row, value, className) {
    const cell = document.createElement("td");
    cell.textContent = displayValue(value);
    if (className) cell.className = className;
    row.appendChild(cell);
  }

  function renderTable(head, body, columns, rows, emptyText) {
    head.replaceChildren();
    body.replaceChildren();
    const heading = document.createElement("tr");
    columns.forEach((column) => {
      const cell = document.createElement("th");
      cell.textContent = column.label;
      heading.appendChild(cell);
    });
    head.appendChild(heading);
    if (!rows.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = columns.length;
      cell.className = "empty";
      cell.textContent = emptyText;
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    rows.forEach((values) => {
      const row = document.createElement("tr");
      values.forEach((value) => appendCell(row, value));
      body.appendChild(row);
    });
  }

  function renderKeyValues(container, values, labels = {}) {
    container.replaceChildren();
    Object.entries(values || {}).forEach(([key, value]) => {
      const item = document.createElement("div");
      item.className = "hw-kv-item";
      const label = document.createElement("span");
      label.textContent = labels[key] || key;
      const content = document.createElement("strong");
      content.textContent = displayValue(value);
      item.append(label, content);
      container.appendChild(item);
    });
    if (!container.childElementCount) {
      const empty = document.createElement("div");
      empty.className = "empty";
      empty.textContent = "暂无数据";
      container.appendChild(empty);
    }
  }

  function drawChart() {
    const canvas = $("traffic-chart");
    const rect = canvas.getBoundingClientRect();
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(300, Math.floor(rect.width));
    const height = Math.max(180, Math.floor(rect.height));
    canvas.width = width * ratio;
    canvas.height = height * ratio;
    const context = canvas.getContext("2d");
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, width, height);
    context.strokeStyle = "rgba(132,169,194,.16)";
    context.lineWidth = 1;
    for (let i = 1; i < 5; i += 1) {
      const y = (height * i) / 5;
      context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke();
    }
    if (state.samples.length < 2) return;
    const max = Math.max(1, ...state.samples.flatMap((sample) => [sample.rx, sample.tx]));
    const line = (key, color, fill) => {
      const points = state.samples.map((sample, index) => ({
        x: (index / (state.samples.length - 1)) * width,
        y: height - (sample[key] / max) * (height - 10) - 5
      }));
      const gradient = context.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, fill); gradient.addColorStop(1, "rgba(0,0,0,0)");
      context.beginPath(); context.moveTo(points[0].x, height);
      points.forEach((point) => context.lineTo(point.x, point.y));
      context.lineTo(points[points.length - 1].x, height); context.closePath();
      context.fillStyle = gradient; context.fill();
      context.strokeStyle = color; context.lineWidth = 2; context.beginPath();
      points.forEach((point, index) => { if (index === 0) context.moveTo(point.x, point.y); else context.lineTo(point.x, point.y); });
      context.stroke();
      const last = points[points.length - 1];
      context.fillStyle = color; context.beginPath(); context.arc(last.x, last.y, 3, 0, Math.PI * 2); context.fill();
    };
    line("rx", "#5ee4c4", "rgba(94,228,196,.18)");
    line("tx", "#87a9ff", "rgba(135,169,255,.14)");
  }

  function logFilterActive() {
    const filters = state.logFilters;
    return Boolean(filters.level || filters.module || filters.event || filters.keyword);
  }

  function filteredLogs() {
    const filters = state.logFilters;
    const keyword = filters.keyword.toLowerCase();
    return state.logs.filter((entry) => {
      if (filters.level && String(entry.level).toLowerCase() !== filters.level) return false;
      if (filters.module && String(entry.module).toLowerCase() !== filters.module) return false;
      if (filters.event && String(entry.event_code) !== filters.event) return false;
      if (keyword && !String(entry.message || "").toLowerCase().includes(keyword)) return false;
      return true;
    });
  }

  function renderLogs() {
    const body = $("log-body");
    body.replaceChildren();
    const matched = filteredLogs();
    const filtered = logFilterActive();
    const total = matched.length;
    const loaded = state.logs.length;
    const pageCount = Math.ceil(total / state.logPageSize);
    state.logPage = pageCount ? Math.min(state.logPage, pageCount) : 1;
    const start = (state.logPage - 1) * state.logPageSize;
    const visibleLogs = matched.slice().reverse().slice(start, start + state.logPageSize);
    if (!visibleLogs.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = 8;
      cell.className = "empty";
      cell.textContent = filtered ? "没有符合筛选条件的日志" : "暂无日志";
      row.appendChild(cell); body.appendChild(row);
    }
    visibleLogs.forEach((entry) => {
      const row = document.createElement("tr");
      const timestampNs = String(entry.timestamp_ns || "0");
      let readableTime = "无效时间";
      try {
        const milliseconds = Number(BigInt(timestampNs) / 1000000n);
        readableTime = new Date(milliseconds).toLocaleString();
      } catch (_) { /* 时间戳异常时保留占位文本 */ }
      const values = [entry.sequence, readableTime, entry.level,
        entry.module, entry.event_code, entry.lcore, entry.port, entry.message];
      values.forEach((value, index) => {
        const cell = document.createElement("td");
        cell.textContent = displayValue(value);
        if (index === 2) cell.className = `level level-${String(value).toLowerCase()}`;
        if (index === 7) cell.className = "log-message";
        row.appendChild(cell);
      });
      body.appendChild(row);
    });
    $("log-count").textContent = filtered ?
      `${formatInteger(total)} / ${formatInteger(loaded)}` : formatInteger(total);
    $("log-range").textContent = total ?
      `显示 ${formatInteger(start + 1)}–${formatInteger(Math.min(start + state.logPageSize, total))} / ${formatInteger(total)} 条${filtered ? `（共 ${formatInteger(loaded)} 条）` : ""}` :
      (filtered ? `0 / ${formatInteger(loaded)} 条` : "0 条");
    $("log-page-info").textContent = `第 ${pageCount ? state.logPage : 0} / ${pageCount} 页`;
    $("log-first").disabled = !pageCount || state.logPage <= 1;
    $("log-prev").disabled = !pageCount || state.logPage <= 1;
    $("log-next").disabled = !pageCount || state.logPage >= pageCount;
    $("log-last").disabled = !pageCount || state.logPage >= pageCount;
  }

  function setLogPage(page) {
    const pageCount = Math.ceil(filteredLogs().length / state.logPageSize);
    state.logPage = Math.max(1, Math.min(page, pageCount || 1));
    renderLogs();
  }

  function renderMetrics(metrics) {
    const counters = metrics.counters || {};
    $("rx-packets").textContent = displayValue(counters.rx_packets || "0");
    $("tx-packets").textContent = displayValue(counters.tx_packets || "0");
    $("tx-dropped").textContent = displayValue(counters.tx_dropped || "0");
    const filtered = BigInt(counters.ann_filtered || "0") + BigInt(counters.get_filtered || "0");
    $("filtered").textContent = formatInteger(filtered);

    // 运行指标页：总览卡片
    const rxAnomaly = BigInt(counters.rx_non_ipv4 || "0") +
      BigInt(counters.rx_non_color || "0") + BigInt(counters.rx_unsupported || "0");
    $("metric-rx-total").textContent = displayValue(counters.rx_packets || "0");
    $("metric-tx-total").textContent = displayValue(counters.tx_packets || "0");
    $("metric-tx-dropped").textContent = displayValue(counters.tx_dropped || "0");
    $("metric-rx-anomaly").textContent = formatInteger(rxAnomaly);

    // 运行指标页：按报文类型分面板显示
    const rxByType = metrics.packet_rx || {};
    const resultsByType = metrics.packet_results || {};
    metricTypes.forEach((type) => {
      $("metric-rx-" + type).textContent = displayValue(rxByType[type] || "0");
      const activeResults = Object.entries(resultsByType[type] || {})
        .filter(([, value]) => value !== "0" && value !== 0);
      renderKeyValues($("metric-results-" + type),
        Object.fromEntries(activeResults), resultLabels);
      const keys = typeCounterKeys[type];
      if (keys) {
        const typeCounters = {};
        keys.forEach((key) => { typeCounters[key] = counters[key]; });
        renderKeyValues($("metric-counters-" + type), typeCounters, metricLabels);
      }
    });

    renderKeyValues($("metric-rx-anomaly-detail"), {
      rx_non_ipv4: counters.rx_non_ipv4,
      rx_non_color: counters.rx_non_color,
      rx_unsupported: counters.rx_unsupported
    }, metricLabels);
    renderKeyValues($("metric-log"), {
      log_dropped: counters.log_dropped,
      log_suppressed: counters.log_suppressed
    }, metricLabels);

    const packetRx = metrics.packet_rx || {};
    renderTable($("packet-rx-body").closest("table").tHead, $("packet-rx-body"),
      [{ label: "报文类型" }, { label: "接收数" }],
      Object.entries(packetRx).map(([type, value]) => [packetLabels[type] || type, value]),
      "暂无报文接收数据");

    const results = metrics.packet_results || {};
    const types = Object.keys(results);
    const resultNames = types.length ? Object.keys(results[types[0]]) : [];
    renderTable($("packet-results-head"), $("packet-results-body"),
      [{ label: "报文类型" }, ...resultNames.map((name) => ({ label: name }))],
      types.map((type) => [packetLabels[type] || type, ...resultNames.map((name) => results[type][name])]),
      "暂无报文处理结果");

    const ports = metrics.ports || [];
    const portFields = ports.length ? Object.keys(ports[0]) : [
      "port_id", "link_up", "link_speed_mbps", "rx_packets", "rx_bytes",
      "rx_errors", "rx_missed", "rx_no_mbuf", "tx_packets", "tx_bytes", "tx_errors"
    ];
    renderTable($("ports-head"), $("ports-body"),
      portFields.map((field) => ({ label: fieldLabels[field] || field })),
      ports.map((port) => portFields.map((field) => port[field])), "没有检测到有效端口");

    const logStatsBody = $("log-stats-body");
    logStatsBody.replaceChildren();
    Object.entries(metrics.logs || {}).forEach(([key, value]) => {
      const row = document.createElement("tr");
      appendCell(row, fieldLabels[key] || key);
      appendCell(row, value);
      logStatsBody.appendChild(row);
    });
    if (!logStatsBody.childElementCount) {
      const row = document.createElement("tr"); const cell = document.createElement("td");
      cell.colSpan = 2; cell.className = "empty"; cell.textContent = "暂无日志统计";
      row.appendChild(cell); logStatsBody.appendChild(row);
    }

    const trace = metrics.trace || {};
    const ann = Array.isArray(trace.ann) ? trace.ann : [];
    const get = Array.isArray(trace.get) ? trace.get : [];
    const traceRows = Array.from({ length: Math.max(ann.length, get.length) }, (_, index) =>
      [index, ann[index] === undefined ? "—" : ann[index], get[index] === undefined ? "—" : get[index]]);
    renderTable($("trace-body").closest("table").tHead, $("trace-body"),
      [{ label: "AS 编号" }, { label: "ANN 计数" }, { label: "GET 计数" }],
      traceRows, "暂无追踪数据");

  }

  function showAclMessage(text, ok) {
    const message = $("acl-op-message");
    message.hidden = false;
    message.textContent = text;
    message.classList.toggle("is-ok", ok);
    message.classList.toggle("is-error", !ok);
  }

  // 原样显示的文本（SID/NID 的 hex、名称、描述）：不能走 displayValue，
  // 否则全是数字的 hex 会被当成整数加千分位
  function appendTextCell(row, text, className) {
    const cell = document.createElement("td");
    cell.textContent = text ? String(text) : "—";
    if (className) cell.className = className;
    row.appendChild(cell);
    return cell;
  }

  // 名称与描述来自终端（不可信），一律用 textContent 写入
  function appendNameCell(row, info) {
    const cell = document.createElement("td");
    cell.className = "hw-sid-text";
    if (!info || !info.name) {
      cell.textContent = "—";
    } else {
      cell.textContent = info.name;
      if (!info.name_verified) {
        const badge = document.createElement("span");
        badge.className = "level level-warn hw-sid-badge";
        badge.textContent = "未校验";
        badge.title = "SHA1(名称) 与 L_SID 不符，名称只能参考";
        cell.appendChild(badge);
      }
    }
    row.appendChild(cell);
  }

  function fillAclSid(sid) {
    const input = $("acl-new-sid");
    input.value = sid;
    input.focus();
    showAclMessage("已把 SID 填入添加规则表单", true);
  }

  function renderSidTable(sids) {
    const body = $("sid-body");
    body.replaceChildren();
    if (!sids.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = 4;
      cell.className = "empty";
      cell.textContent = "SID 列表为空";
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    sids.forEach((info, index) => {
      const row = document.createElement("tr");
      row.className = "hw-sid-row";
      row.title = "点击填入下方 ACL 表单";
      appendCell(row, index + 1);
      appendTextCell(row, info.sid, "hw-acl-sid");
      appendNameCell(row, info);
      appendTextCell(row, info.desc, "hw-sid-text");
      row.addEventListener("click", () => fillAclSid(info.sid));
      body.appendChild(row);
    });
  }

  function renderAclTable(acls, sidInfo) {
    const body = $("acl-body");
    body.replaceChildren();
    if (!acls.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = 7;
      cell.className = "empty";
      cell.textContent = "暂无 ACL 规则";
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    acls.forEach((rule) => {
      const row = document.createElement("tr");
      const action = String(rule.action || "").toLowerCase();
      appendCell(row, rule.id);
      const actionCell = document.createElement("td");
      const badge = document.createElement("span");
      badge.className = `level ${action === "allow" ? "level-allow" : "level-deny"}`;
      badge.textContent = action || "unknown";
      actionCell.appendChild(badge);
      row.appendChild(actionCell);
      const sidCell = appendTextCell(row, rule.sid, "hw-acl-sid");
      const info = sidInfo.get(String(rule.sid || "").toLowerCase());
      if (info && (info.name || info.desc)) {
        const nameLine = document.createElement("div");
        nameLine.className = "hw-sid-name";
        nameLine.textContent = [info.name, info.desc].filter(Boolean).join(" · ");
        if (info.name && !info.name_verified) {
          const badge = document.createElement("span");
          badge.className = "level level-warn hw-sid-badge";
          badge.textContent = "未校验";
          nameLine.appendChild(badge);
        }
        sidCell.appendChild(nameLine);
      }
      appendCell(row, rule.asid);
      appendTextCell(row, rule.nid_wildcard ? "任意 (通配)" : rule.nid, "hw-acl-sid");
      const hitsCell = document.createElement("td");
      hitsCell.className = "hw-acl-hits";
      hitsCell.textContent =
        `${displayValue(rule.allow_hits || 0)} / ${displayValue(rule.deny_hits || 0)}`;
      row.appendChild(hitsCell);
      const opCell = document.createElement("td");
      const deleteButton = document.createElement("button");
      deleteButton.type = "button";
      deleteButton.className = "hw-btn-danger";
      deleteButton.textContent = "删除";
      deleteButton.addEventListener("click", () => deleteAcl(rule.id));
      opCell.appendChild(deleteButton);
      row.appendChild(opCell);
      body.appendChild(row);
    });
  }

  async function refreshSidAcl() {
    const [sidData, aclData] = await Promise.all([
      json("/api/v1/sids"), json("/api/v1/acls")
    ]);
    // 每条是 {sid, name, name_verified, desc}；兼容旧版 RM 只返回 SID 字符串
    const sids = (Array.isArray(sidData.sids) ? sidData.sids : [])
      .map((item) => (typeof item === "string" ? { sid: item } : item))
      .filter((item) => item && typeof item.sid === "string");
    $("sid-count").textContent =
      displayValue(sidData.count === undefined ? sids.length : sidData.count);
    renderSidTable(sids);
    const sidInfo = new Map(sids.map((item) => [item.sid.toLowerCase(), item]));
    const acls = Array.isArray(aclData.acls) ? aclData.acls : [];
    $("acl-count").textContent =
      displayValue(aclData.count === undefined ? acls.length : aclData.count);
    renderAclTable(acls, sidInfo);
  }

  async function addAcl(event) {
    event.preventDefault();
    const sidText = $("acl-new-sid").value.trim().replace(/^0x/i, "").toLowerCase();
    const nidText = $("acl-new-nid").value.trim().replace(/^0x/i, "").toLowerCase();
    if (!sidText.length || sidText.length % 2 !== 0 || sidText.length > 72 ||
        !/^[0-9a-f]+$/.test(sidText)) {
      showAclMessage("添加失败：SID 必须是 1-36 字节的偶数位 hex", false);
      return;
    }
    if (nidText && (nidText.length % 2 !== 0 || nidText.length > 32 ||
        !/^[0-9a-f]+$/.test(nidText))) {
      showAclMessage("添加失败：NID 必须是 0-16 字节的偶数位 hex", false);
      return;
    }
    try {
      const response = await fetch("/api/v1/acls", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          id: Number($("acl-new-id").value),
          action: $("acl-new-action").value,
          sid: sidText,
          asid: Number($("acl-new-asid").value),
          nid: nidText
        })
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || result.status !== "ok")
        throw new Error(result.message || `${response.status} ${response.statusText}`);
      showAclMessage("ACL 规则已添加", true);
      $("acl-add-form").reset();
      await refreshSidAcl();
    } catch (error) {
      showAclMessage(`添加失败：${error.message}`, false);
    }
  }

  async function deleteAcl(id) {
    if (!window.confirm(`确认删除 ACL 规则 id=${displayValue(id)} 吗？`)) return;
    try {
      const response = await fetch(`/api/v1/acls?id=${encodeURIComponent(String(id))}`,
        { method: "DELETE" });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || result.status !== "ok")
        throw new Error(result.message || `${response.status} ${response.statusText}`);
      showAclMessage("ACL 规则已删除", true);
      await refreshSidAcl();
    } catch (error) {
      showAclMessage(`删除失败：${error.message}`, false);
    }
  }

  async function refresh() {
    if (state.refreshing) return;
    state.refreshing = true;
    $("refresh-now").classList.add("is-refreshing");
    try {
      const [health, metrics] = await Promise.all([json("/api/v1/health"), json("/api/v1/metrics")]);
      renderMetrics(metrics);
      const capturedAt = String(metrics.captured_at_ns || "0");
      let capturedLocal = "无效时间";
      try { capturedLocal = new Date(Number(BigInt(capturedAt) / 1000000n)).toLocaleString(); }
      catch (_) { /* 无效纳秒值时显示占位文本 */ }
      renderKeyValues($("health-fields"), {
        status: health.status,
        service: health.service,
        port: health.port,
        captured_at_local: capturedLocal
      }, {
        status: "服务状态", service: "服务名称", port: "监听端口", captured_at_local: "快照本地时间"
      });
      const counters = metrics.counters || {};
      state.samples.push({ rx: Number(counters.rx_packets || 0), tx: Number(counters.tx_packets || 0) });
      state.samples = state.samples.slice(-60);
      $("sample-state").textContent = `${state.samples.length} / 60 samples`;
      drawChart();

      let logs;
      let returnedLogs = 0;
      for (let batch = 0; batch < 3; batch += 1) {
        logs = await json(`/api/v1/logs?after=${encodeURIComponent(state.after)}&limit=256`);
        const entries = Array.isArray(logs.logs) ? logs.logs : [];
        returnedLogs += entries.length;
        if (!entries.length) break;
        state.after = String(logs.next_sequence || state.after);
        state.logs = state.logs.concat(entries).slice(-512);
        if (entries.length < 256) break;
      }
      if (returnedLogs) renderLogs();
      $("log-poll-meta").textContent = `after_sequence: ${displayValue(logs.after_sequence)} · next_sequence: ${displayValue(logs.next_sequence)} · 本轮返回: ${displayValue(returnedLogs)} 条 · 当前保留: ${displayValue(state.logs.length)} 条`;
      const portText = health.port === undefined || health.port === null ? "web" : `port ${displayValue(health.port)}`;
      $("updated-at").textContent = `${new Date().toLocaleTimeString()} · ${portText}`;
      $("snapshot-time").textContent = new Date().toLocaleTimeString();
      setConnection(health.status === "ok" && health.web_running === true);
      if (document.querySelector(".hw-view.active")?.dataset.view === "sid-acl") {
        try { await refreshSidAcl(); }
        catch (error) { console.error("RM monitor sid/acl refresh failed", error); }
      }
    } catch (error) {
      setConnection(false);
      $("updated-at").textContent = "无法读取监控数据";
      console.error("RM monitor refresh failed", error);
    } finally {
      state.refreshing = false;
      $("refresh-now").classList.remove("is-refreshing");
    }
  }

  $("endpoint").textContent = window.location.host || "127.0.0.1:8080";
  $("refresh-now").addEventListener("click", refresh);
  window.addEventListener("resize", drawChart);
  document.querySelectorAll(".hw-nav-item").forEach((item) => {
    item.addEventListener("click", () => setActivePage(item.dataset.page));
  });
  $("log-page-size").addEventListener("change", (event) => {
    state.logPageSize = Number(event.target.value) || 25;
    state.logPage = 1;
    renderLogs();
  });
  $("log-first").addEventListener("click", () => setLogPage(1));
  $("log-prev").addEventListener("click", () => setLogPage(state.logPage - 1));
  $("log-next").addEventListener("click", () => setLogPage(state.logPage + 1));
  $("log-last").addEventListener("click", () => setLogPage(Math.ceil(filteredLogs().length / state.logPageSize)));
  function applyLogFilter(key, value) {
    state.logFilters[key] = value;
    state.logPage = 1;
    renderLogs();
  }
  $("log-filter-level").addEventListener("change", (event) => applyLogFilter("level", event.target.value));
  $("log-filter-module").addEventListener("change", (event) => applyLogFilter("module", event.target.value));
  $("log-filter-event").addEventListener("input", (event) => applyLogFilter("event", event.target.value.trim()));
  $("log-filter-keyword").addEventListener("input", (event) => applyLogFilter("keyword", event.target.value.trim().toLowerCase()));
  $("log-filter-reset").addEventListener("click", () => {
    state.logFilters = { level: "", module: "", event: "", keyword: "" };
    $("log-filter-level").value = "";
    $("log-filter-module").value = "";
    $("log-filter-event").value = "";
    $("log-filter-keyword").value = "";
    state.logPage = 1;
    renderLogs();
  });
  $("acl-add-form").addEventListener("submit", addAcl);

  const sidebar = $("sidebar");
  const sidebarToggle = $("sidebar-toggle");
  function setSidebarCollapsed(collapsed) {
    sidebar.classList.toggle("is-collapsed", collapsed);
    document.body.classList.toggle("sidebar-collapsed", collapsed);
    sidebarToggle.title = collapsed ? "展开菜单" : "收起菜单";
    sidebarToggle.setAttribute("aria-label", sidebarToggle.title);
    sidebarToggle.setAttribute("aria-expanded", collapsed ? "false" : "true");
    try { window.localStorage.setItem("rm.sidebar.collapsed", collapsed ? "1" : "0"); }
    catch (_) { /* 隐私模式下无法持久化，忽略 */ }
    // 侧边栏宽度变化后重绘趋势图，避免画布被拉伸
    window.requestAnimationFrame(() => {
      if (document.querySelector(".hw-view.active")?.dataset.view === "overview") drawChart();
    });
  }
  let sidebarCollapsed = false;
  try { sidebarCollapsed = window.localStorage.getItem("rm.sidebar.collapsed") === "1"; }
  catch (_) { /* 读不到历史状态时默认展开 */ }
  setSidebarCollapsed(sidebarCollapsed);
  sidebarToggle.addEventListener("click", () => {
    setSidebarCollapsed(!sidebar.classList.contains("is-collapsed"));
  });
  window.addEventListener("hashchange", () => setActivePage(window.location.hash.slice(1), false));
  setActivePage(window.location.hash.slice(1) || "overview", false);
  renderLogs(); drawChart(); refresh();
  window.setInterval(refresh, 2000);
})();
