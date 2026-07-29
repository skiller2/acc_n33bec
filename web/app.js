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
    device_id: parseInt(document.getElementById('device_id').value)
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

function loadFirmwareVersion() {
  return fetch('/version')
    .then(r => {
      if (!r.ok) {
        throw new Error('HTTP ' + r.status);
      }
      return r.json();
    })
    .then(data => {
      const versionEl = document.getElementById('firmware-version');
      const fsEl = document.getElementById('filesystem-info');

      if (versionEl) {
        versionEl.innerText = 'Version: ' + data.version;
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

async function waitForFirmwareApplied() {
  const maxRetries = 60;

  for (let i = 0; i < maxRetries; i++) {
    try {
      const version = await loadFirmwareVersion();

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
  loadFirmwareVersion();
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

const ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (e) => {
  const data = JSON.parse(e.data);
  const li = document.createElement('li');
  li.innerText = data.card + ' ' + (data.ok ? 'OK' : 'DENIED');
  document.getElementById('live').appendChild(li);
};

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
          const  valueDisplay = log.value;
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