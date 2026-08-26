function switchTab(tab) {
  document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.getElementById(tab + '-tab').classList.add('active');
  event.target.classList.add('active');

  if (tab === 'cards') {
    loadCards();
  } else if (tab === 'logs') {
    loadLogs();
  } else if (tab === 'config') {
    loadConfig();
  } else if (tab === 'firmware') {
    loadFirmwareVersion();
  } else if (tab === 'device') {
    loadDeviceInfo();
  } else if (tab === 'wifi') {
    loadWifiStatus();
  }
}

function addCard() {
  const id = document.getElementById('cardId').value;
  fetch('/card', { method: 'PUT', body: id });
}

function simulateCardRead() {
  const cardId = Number(document.getElementById('simulateCardId').value);
  const reader = Number(document.getElementById('simulateReader').value);

  fetch('/simulate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ card: cardId, reader: reader })
  })
    .then(r => r.text())
    .then(txt => {
      if (txt === 'OK') {
        setStatus('Card simulated!', 'success');
        document.getElementById('simulateCardId').value = '';
      } else {
        setStatus('Error: ' + txt, 'error');
      }
    })
    .catch(e => setStatus('Simulate error: ' + e, 'error'));
}

function testRelay(target) {
  fetch('/test', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ target: target })
  })
    .then(r => r.text())
    .then(txt => {
      if (txt === 'OK') {
        setStatus('Test ' + target + ' OK', 'success');
      } else {
        setStatus('Error: ' + txt, 'error');
      }
    })
    .catch(e => setStatus('Test error: ' + e, 'error'));
}

function loadConfig() {
  fetch('/config')
    .then(r => r.json())
    .then(cfg => {
      document.getElementById('url_n33bec').value = cfg.url_n33bec;
      document.getElementById('cod_tema').value = cfg.cod_tema;
      document.getElementById('rex1_relay_gpio').value = cfg.rex1_relay_gpio;
      document.getElementById('rex2_relay_gpio').value = cfg.rex2_relay_gpio;
      document.getElementById('port1_relay_gpio').value = cfg.port1_relay_gpio;
      document.getElementById('port2_relay_gpio').value = cfg.port2_relay_gpio;
      document.getElementById('rex1_relay_duration_ms').value = cfg.rex1_relay_duration_ms;
      document.getElementById('rex2_relay_duration_ms').value = cfg.rex2_relay_duration_ms;
      document.getElementById('port1_relay_duration_ms').value = cfg.port1_relay_duration_ms;
      document.getElementById('port2_relay_duration_ms').value = cfg.port2_relay_duration_ms;
      document.getElementById('input_debounce_ms').value = cfg.input_debounce_ms;
      document.getElementById('device_id').value = cfg.device_id;
      document.getElementById('keep_alive_secs').value = cfg.keep_alive_secs;
      setStatus('Config loaded', 'success');
    })
    .catch(e => setStatus('Load error: ' + e, 'error'));
}

function saveConfig() {
  const cfg = {
    url_n33bec: document.getElementById('url_n33bec').value,
    cod_tema: document.getElementById('cod_tema').value,
    rex1_relay_gpio: parseInt(document.getElementById('rex1_relay_gpio').value),
    rex2_relay_gpio: parseInt(document.getElementById('rex2_relay_gpio').value),
    port1_relay_gpio: parseInt(document.getElementById('port1_relay_gpio').value),
    port2_relay_gpio: parseInt(document.getElementById('port2_relay_gpio').value),
    rex1_relay_duration_ms: parseInt(document.getElementById('rex1_relay_duration_ms').value),
    rex2_relay_duration_ms: parseInt(document.getElementById('rex2_relay_duration_ms').value),
    port1_relay_duration_ms: parseInt(document.getElementById('port1_relay_duration_ms').value),
    port2_relay_duration_ms: parseInt(document.getElementById('port2_relay_duration_ms').value),
    input_debounce_ms: parseInt(document.getElementById('input_debounce_ms').value),
    device_id: parseInt(document.getElementById('device_id').value),
    keep_alive_secs: parseInt(document.getElementById('keep_alive_secs').value)
  };

  fetch('/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(cfg)
  })
    .then(r => r.text())
    .then(txt => {
      if (txt === 'OK') {
        setStatus('Config saved!', 'success');
      } else {
        setStatus('Error: ' + txt, 'error');
      }
    })
    .catch(e => setStatus('Save error: ' + e, 'error'));
}

function formatBytes(bytes) {
  if (typeof bytes !== 'number' || bytes < 0) {
    return 'unknown';
  }

  if (bytes < 1024) {
    return bytes + ' B';
  }

  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  return value.toFixed(value >= 10 ? 1 : 0) + ' ' + units[unitIndex];
}

function loadFirmwareVersion(timeoutMs = 5000) {
  const controller = new AbortController();

  const timeoutId = setTimeout(() => {
    controller.abort();
  }, timeoutMs);

  return fetch('/version', {
    signal: controller.signal
  })
    .then(r => {
      clearTimeout(timeoutId);

      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }

      return r.json();
    })
    .then(data => {
      const versionEl = document.getElementById('firmware-version');
      const extraVersionEl = document.getElementById('extra-firmware-version');
      const fsEl = document.getElementById('filesystem-info');

      if (versionEl) {
        versionEl.innerText = 'Version: ' + data.version;
      }

      if (extraVersionEl) {
        extraVersionEl.innerText =
          'Date: ' + data.date + ' ' + data.time;
      }

      if (fsEl) {
        const total = data.fs_total_bytes || 0;
        const free = data.fs_free_bytes || 0;

        fsEl.innerText =
          'Filesystem: ' +
          formatBytes(total) +
          ' total, ' +
          formatBytes(free) +
          ' free';
      }

      return data.version;
    })
    .catch(err => {
      clearTimeout(timeoutId);

      if (err.name === 'AbortError') {
        throw new Error('Request timed out after ' + timeoutMs + ' ms');
      }

      throw err;
    });
}

function loadFirmwareVersionOld() {
  return fetch('/version')
    .then(r => {
      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }
      return r.json();
    })
    .then(data => {
      const versionEl = document.getElementById('firmware-version');
      const extraVersionEl = document.getElementById('extra-firmware-version');
      const fsEl = document.getElementById('filesystem-info');

      if (versionEl) {
        versionEl.innerText = 'Version: ' + data.version;
      }

      if (extraVersionEl) {
        extraVersionEl.innerText = 'Date: ' + data.date + ' ' + data.time; 
        // ' vesion-app-ota: '+data.version_ota;
      }

      if (fsEl) {
        const total = data.fs_total_bytes || 0;
        const free = data.fs_free_bytes || 0;
        fsEl.innerText =
          'Filesystem: ' + formatBytes(total) + ' total, ' + formatBytes(free) + ' free';
      }

      return data.version;
    });
}

function formatUptime(seconds) {
  if (typeof seconds !== 'number' || seconds < 0) {
    return 'N/A';
  }
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = Math.floor(seconds % 60);
  return days + 'd ' + hours + 'h ' + minutes + 'm ' + secs + 's';
}

function loadInfo() {
  fetch('/info')
    .then(r => {
      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }
      return r.json();
    })
    .then(data => {
      const infoContent = document.getElementById('info-content');
      if (!infoContent) return;

      let html = '<p><strong>Device ID:</strong> ' + (data.device_id !== undefined ? data.device_id : 'N/A') + '</p>';
      html += '<p><strong>WiFi IP:</strong> ' + (data.wifi_ip || 'N/A') + '</p>';
      html += '<p><strong>ETH IP:</strong> ' + (data.eth_ip || 'N/A') + '</p>';
      html += '<p><strong>Uptime:</strong> ' + formatUptime(data.uptime_sec) + '</p>';

      infoContent.innerHTML = html;
    })
    .catch(e => {
      const infoContent = document.getElementById('info-content');
      if (infoContent) {
        infoContent.innerHTML = '<p>Error loading info: ' + e.message + '</p>';
      }
    });
}

function loadDeviceInfo() {
  fetch('/device_info')
    .then(r => {
      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }
      return r.json();
    })
    .then(data => {
      const infoDiv = document.getElementById('device-info');
      if (!infoDiv) return;

      let html = '<p><strong>MAC Address:</strong> ' + (data.mac || 'N/A') + '</p>';
      html += '<p><strong>Chip Model:</strong> ' + (data.chip_model || 'N/A') + '</p>';
      html += '<p><strong>Chip Cores:</strong> ' + (data.chip_cores || 'N/A') + '</p>';
      html += '<p><strong>Chip Revision:</strong> ' + (data.chip_revision || 'N/A') + '</p>';
      html += '<p><strong>SDK Version:</strong> ' + (data.sdk_version || 'N/A') + '</p>';
      html += '<p><strong>Free Heap:</strong> ' + formatBytes(data.free_heap || 0) + '</p>';
      html += '<p><strong>Minimum Free Heap:</strong> ' + formatBytes(data.min_free_heap || 0) + '</p>';
      html += '<p><strong>Flash Size:</strong> ' + formatBytes(data.flash_size || 0) + '</p>';
      html += '<p><strong>Flash Speed:</strong> ' + (data.flash_speed || 'N/A') + ' Hz</p>';
      html += '<p><strong>Flash Mode:</strong> ' + (data.flash_mode || 'N/A') + '</p>';

      infoDiv.innerHTML = html;
    })
    .catch(e => {
      const infoDiv = document.getElementById('device-info');
      if (infoDiv) {
        infoDiv.innerHTML = '<p>Error loading device info: ' + e.message + '</p>';
      }
    });
}

function renderQrCode(uri) {
  const canvas = document.createElement('canvas');
  const qrDiv = document.getElementById('dpp-qrcode');
  qrDiv.innerHTML = '';
  qrDiv.appendChild(canvas);
  QRCode.toCanvas(canvas, uri, {
    width: 256,
    margin: 2,
    color: { dark: '#000', light: '#fff' }
  }, function (error) {
    if (error) {
      qrDiv.innerHTML = '<p>Failed to render QR code: ' + error.message + '</p>';
    }
  });
}

function statusColor(status) {
  const colors = {
    connected: 'success',
    dpp_ready: 'success',
    dpp_listening: '',
    connecting: '',
    disconnected: 'error',
    dpp_failed: 'error'
  };
  return colors[status] || '';
}

function loadWifiStatus(pollMs) {
  fetch('/wifi')
    .then(r => {
      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }
      return r.json();
    })
    .then(data => {
      console.log('WiFi status:', data);
      const statusEl = document.getElementById('wifi-status');
      const detailsEl = document.getElementById('wifi-details');
      const qrDiv = document.getElementById('dpp-qrcode');
      const uriDiv = document.getElementById('dpp-uri');
      const dppStatusDiv = document.getElementById('dpp-status');

      if (statusEl) {
        const cls = statusColor(data.status);
        statusEl.innerHTML = '<strong>Status:</strong> ' + data.status + (data.connected ? ' (connected)' : '');
        statusEl.className = 'status ' + cls;
      }

      if (detailsEl) {
        let html = '';
        if (data.ssid) {
          html += '<p><strong>SSID:</strong> ' + data.ssid + '</p>';
        }
        if (data.ip) {
          html += '<p><strong>IP:</strong> ' + data.ip + '</p>';
        }
        detailsEl.innerHTML = html;
      }

      if (qrDiv && uriDiv && dppStatusDiv) {
        console.log('DPP URI:', data.dpp_uri);
        if (data.dpp_uri) {
          renderQrCode(data.dpp_uri);
          uriDiv.textContent = data.dpp_uri;
          dppStatusDiv.innerHTML = '';
        } else {
          qrDiv.innerHTML = '<p>No DPP URI available yet. DPP enrollee is ' + (data.status === 'dpp_listening') ? 'listening for bootstrap...' : 'waiting for bootstrap.)</p>';
          uriDiv.textContent = '';
          if (data.status === 'dpp_failed') {
            dppStatusDiv.innerHTML = '<p class="status error">DPP authentication failed.</p>';
          }
        }
      }
    })
    .catch(e => {
      const statusEl = document.getElementById('wifi-status');
      if (statusEl) {
        statusEl.innerHTML = 'Error loading WiFi status: ' + e.message;
        statusEl.className = 'status error';
      }
    });

  if (pollMs) {
    setTimeout(() => loadWifiStatus(pollMs), pollMs);
  }
}

function regenerateDppBootstrap() {
  const dppStatusDiv = document.getElementById('dpp-status');
  dppStatusDiv.innerHTML = '<p class="status">Regenerating QR code...</p>';

  fetch('/dpp/bootstrap', { method: 'POST' })
    .then(r => r.text())
    .then(txt => {
      if (txt.startsWith('OK')) {
        dppStatusDiv.innerHTML = '<p class="status success">QR code regenerated. Scan the new code to provision.</p>';
        loadWifiStatus();
      } else {
        dppStatusDiv.innerHTML = '<p class="status error">Error: ' + txt + '</p>';
      }
    })
    .catch(e => {
      dppStatusDiv.innerHTML = '<p class="status error">Error: ' + e + '</p>';
    });
}

async function waitForFirmwareApplied() {
  const maxRetries = 60;

  for (let i = 0; i < maxRetries; i++) {
    try {
      const version = await loadFirmwareVersion(1500);

      setOtaStatus(
        'Firmware applied successfully. Running version: ' + version,
        'success'
      );
      return;
    } catch (e) {
      // El equipo sigue reiniciando
    }

    await new Promise(resolve => setTimeout(resolve, 1000));
  }

  setOtaStatus(
    'Timeout waiting for device to come back online.',
    'error'
  );
}

function uploadFirmware() {
  const file = document.getElementById('firmwareFile').files[0];

  if (!file) {
    setOtaStatus('Select a firmware binary first.', 'error');
    return;
  }

  setOtaStatus('Uploading firmware...', 'success');

  fetch('/ota', {
    method: 'POST',
    body: file
  })
    .then(r => r.text())
    .then(txt => {
      if (txt.startsWith('OK')) {
        setOtaStatus(
          'Firmware upload accepted. Waiting for reboot...',
          'success'
        );

        waitForFirmwareApplied();
      } else {
        setOtaStatus('Upload failed: ' + txt, 'error');
      }
    })
    .catch(e => setOtaStatus('Upload error: ' + e, 'error'));
}

function uploadStorageImage() {
  const file = document.getElementById('storageFile').files[0];
  if (!file) {
    setStorageStatus('Select a web bundle first.', 'error');
    return;
  }

  setStorageStatus('Uploading web bundle...', 'success');

  fetch('/storage', {
    method: 'POST',
    body: file
  })
    .then(r => r.text())
    .then(txt => {
      if (txt.startsWith('OK')) {
        setStorageStatus('Web bundle uploaded. Files updated.', 'success');
      } else {
        setStorageStatus('Upload failed: ' + txt, 'error');
      }
    })
    .catch(e => setStorageStatus('Upload error: ' + e, 'error'));
}

function rebootDevice() {
  setOtaStatus('Rebooting device...', 'success');
  fetch('/reboot', { method: 'POST' })
    .then(r => r.text())
    .then(txt => {
      if (txt.startsWith('OK')) {
        setOtaStatus('Reboot command sent.', 'success');
      } else {
        setOtaStatus('Reboot failed: ' + txt, 'error');
      }
    })
    .catch(e => setOtaStatus('Reboot error: ' + e, 'error'));
}

document.addEventListener('DOMContentLoaded', () => {
  loadInfo();
  loadFirmwareVersion();
  loadWifiStatus(30000);
});

function setStatus(msg, cls) {
  const st = document.getElementById('config-status');
  st.innerText = msg;
  st.className = 'status ' + cls;
}

function setOtaStatus(msg, cls) {
  const st = document.getElementById('ota-status');
  st.innerText = msg;
  st.className = 'status ' + cls;
}

function setStorageStatus(msg, cls) {
  const st = document.getElementById('storage-status');
  st.innerText = msg;
  st.className = 'status ' + cls;
}


function appendLiveEntry(card, ts, ok) {
  const ul = document.getElementById('live');
  if (!ul) return;

  const li = document.createElement('li');
  const readableTime = formatTimestamp(ts);
  const eventDisplay = ok ? 'CARD PASSED' : 'CARD DENIED';
  const valueDisplay = wiegand26ToFcCard(card);

  li.innerText = `${readableTime} - ${eventDisplay} - ${valueDisplay}`;
  ul.appendChild(li);

  while (ul.children.length > 50) {
    ul.removeChild(ul.firstChild);
  }
}

let ws = null;
let reconnectTimer = null;

function connectWebSocket() {
  if (ws && (
    ws.readyState === WebSocket.OPEN ||
    ws.readyState === WebSocket.CONNECTING
  )) {
    return;
  }

  console.log('Connecting WebSocket...');

  ws = new WebSocket(`ws://${location.host}/ws`);

  ws.onopen = () => {
    console.log('WebSocket connected');

    // Register with ESP32
    ws.send('hello');
  };

  ws.onmessage = (e) => {
    try {
      const data = JSON.parse(e.data);
      appendLiveEntry(data.card, data.ts, data.ok);
    } catch (err) {
      console.error('WebSocket message parse error:', err, e.data);
    }
  };

  ws.onclose = (event) => {
    console.log(
      `WebSocket disconnected (code=${event.code}), reconnecting in 2s...`
    );

    ws = null;

    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connectWebSocket, 2000);
  };

  ws.onerror = (err) => {
    console.error('WebSocket error:', err);
  };
}

// Start connection
connectWebSocket();

function loadCards() {
  fetch('/cards')
    .then(r => r.json())
    .then(cards => {
      const container = document.getElementById('cards-list');
      container.innerHTML = '';
      // Support both array or object format
      if (Array.isArray(cards)) {
        cards.forEach(row => {
          const div = document.createElement('div');
          div.className = 'card';
          div.innerText = row.card;
          container.appendChild(div);
        });
      } else {
        // If it's an object like {id: status}
        Object.keys(cards).forEach(key => {
          const div = document.createElement('div');
          div.className = 'card';
          div.innerText = key + ' : ' + cards[key];
          container.appendChild(div);
        });
      }
    })
    .catch(e => {
      document.getElementById('cards-list').innerText = 'Error loading cards: ' + e;
    });
}

function formatTimestamp(ts) {
  const ms = Math.floor(ts / 1000);
  const micros = ts % 1000000;

  const base = new Date(ms).toLocaleString();

  return `${base}.${String(micros).padStart(6, "0")}`;
}

function wiegand26ToFcCard(value) {
  const raw = Number(value);
  const facility = (raw >>> 17) & 0xFF;
  const card = (raw >>> 1) & 0xFFFF;
  return String(facility).padStart(3, '0') + '-' + String(card).padStart(5, '0');
}

function loadLogs() {
  fetch('/logs')
    .then(r => r.json())
    .then(logs => {
      const container = document.getElementById('logs-list');
      container.innerHTML = '';

      logs
      .sort((a, b) => b.ts - a.ts) // Mayor fecha primero
      .forEach(log => {
        const div = document.createElement('div');
        div.className = 'card';

        if (typeof log === 'object') {
          const readableTime = formatTimestamp(log.ts);
          let valueDisplay = log.value;
          let eventDisplay = "";

          if (log.event_id == 10) {
            eventDisplay = "CARD PASSED";
          } else if (log.event_id == 11) {
            eventDisplay = "CARD DENIED";
          } else if (log.event_id == 6) {
            eventDisplay = "REX";
          } else if (log.event_id == 5) {
            eventDisplay = "DOOR";
          } else if (log.event_id == 2) {
            eventDisplay = "POWER";
          } else if (log.event_id == 1) {
            eventDisplay = "SYSTEM START";
          } else {
            eventDisplay = "EVENT" + log.event_id;
          }

          if (log.event_id == 10 || log.event_id == 11) {
            valueDisplay = wiegand26ToFcCard(log.value);
          }

          div.innerText = `${readableTime} - ${eventDisplay} - Port${log.port_id} - ${valueDisplay}`;
        } else {
          div.innerText = log;
        }

        container.appendChild(div);
      });
    })
    .catch(e => {
      document.getElementById('logs-list').innerText = 'Error loading logs: ' + e;
    });
}