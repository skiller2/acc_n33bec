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

function loadCards() {
  fetch('/cards')
    .then(r => r.json())
    .then(cards => {
      const container = document.getElementById('cards-list');
      container.innerHTML = '';
console.log('cards',cards)
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

function loadLogs() {
  fetch('/logs')
    .then(r => r.json())
    .then(logs => {
      const container = document.getElementById('logs-list');
      container.innerHTML = '';

      logs.forEach(log => {
        const div = document.createElement('div');
        div.className = 'card';

        // Adjust to your JSON structure
        if (typeof log === 'object') {
//          div.innerText = JSON.stringify(log);
          div.innerText = `${log.time} - ${log.card} - ${log.ok ? 'OK' : 'DENIED'}`;
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