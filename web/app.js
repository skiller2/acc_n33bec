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
  }
}

function addCard() {
  const id = document.getElementById('cardId').value;
  fetch('/card', {method:'PUT', body: id});
}

function loadConfig() {
  fetch('/config')
    .then(r => r.json())
    .then(cfg => {
      document.getElementById('rex1_relay_gpio').value = cfg.rex1_relay_gpio;
      document.getElementById('rex2_relay_gpio').value = cfg.rex2_relay_gpio;
      document.getElementById('reader1_relay_gpio').value = cfg.reader1_relay_gpio;
      document.getElementById('reader2_relay_gpio').value = cfg.reader2_relay_gpio;
      document.getElementById('rex1_relay_duration_ms').value = cfg.rex1_relay_duration_ms;
      document.getElementById('rex2_relay_duration_ms').value = cfg.rex2_relay_duration_ms;
      document.getElementById('reader1_relay_duration_ms').value = cfg.reader1_relay_duration_ms;
      document.getElementById('reader2_relay_duration_ms').value = cfg.reader2_relay_duration_ms;
      document.getElementById('input_debounce_ms').value = cfg.input_debounce_ms;
      setStatus('Config loaded', 'success');
    })
    .catch(e => setStatus('Load error: ' + e, 'error'));
}

function saveConfig() {
  const cfg = {
    rex1_relay_gpio: parseInt(document.getElementById('rex1_relay_gpio').value),
    rex2_relay_gpio: parseInt(document.getElementById('rex2_relay_gpio').value),
    reader1_relay_gpio: parseInt(document.getElementById('reader1_relay_gpio').value),
    reader2_relay_gpio: parseInt(document.getElementById('reader2_relay_gpio').value),
    rex1_relay_duration_ms: parseInt(document.getElementById('rex1_relay_duration_ms').value),
    rex2_relay_duration_ms: parseInt(document.getElementById('rex2_relay_duration_ms').value),
    reader1_relay_duration_ms: parseInt(document.getElementById('reader1_relay_duration_ms').value),
    reader2_relay_duration_ms: parseInt(document.getElementById('reader2_relay_duration_ms').value),
    input_debounce_ms: parseInt(document.getElementById('input_debounce_ms').value)
  };
  
  fetch('/config', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
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

function setStatus(msg, cls) {
  const st = document.getElementById('config-status');
  st.innerText = msg;
  st.className = 'status ' + cls;
}

const ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (e)=>{
  const data = JSON.parse(e.data);
  const li = document.createElement('li');
  li.innerText = data.card + ' ' + (data.ok?'OK':'DENIED');
  document.getElementById('live').appendChild(li);
};
