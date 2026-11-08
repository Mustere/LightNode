const $ = id => document.getElementById(id);
const message = text => { $('message').textContent = text; };
let lightSaveTimer;
let lightSaveInProgress = false;
let lightSaveQueued = false;

const SOFT_COLORS = ['#FF7715', '#001525', '#00331A', '#1A0033', '#2A1A4A', '#1A332A'];
let favoriteColors = [];

function colorToHex(color) {
  return '#' + Number(color || 0).toString(16).padStart(6, '0').slice(-6);
}

async function loadConfig() {
  const response = await fetch('/api/get-config');
  if (!response.ok) throw new Error('Не удалось загрузить настройки');
  const config = await response.json();
  const light = config.light || {};
  $('enabled').checked = Boolean(light.enabled);
  $('brightness').value = Number(light.brightness ?? 192);
  $('brightness-value').textContent = $('brightness').value;
  $('color').value = colorToHex(light.color ?? 0xffa000);
  $('effect').value = String(light.effect ?? 0);
  favoriteColors = (Array.isArray(light.favorite_colors) ? light.favorite_colors : []).map(c => Number(c) || 0);
  while (favoriteColors.length < 6) favoriteColors.push(0);
  favoriteColors = favoriteColors.slice(0, 6);
  $('alarm-enabled').checked = Boolean(light.alarm_enabled);
  document.querySelectorAll('.alarm').forEach((input, index) => {
    const minutes = Number(light.alarm_minutes?.[index] ?? 480);
    input.value = `${String(Math.floor(minutes / 60)).padStart(2, '0')}:${String(minutes % 60).padStart(2, '0')}`;
  });
  $('wifi-mode').value = config.wifi_mode || 'AP';
  $('wifi-ssid').value = config.ssid || '';
  $('wifi-password').value = config.password || '';
  $('mqtt-token').value = light.mqtt_token || '';
}

async function saveLightSettings() {
  if (lightSaveInProgress) {
    lightSaveQueued = true;
    return;
  }
  lightSaveInProgress = true;
  lightSaveQueued = false;
  $('light-save-status').textContent = 'Сохранение...';
  const payload = {
    enabled: $('enabled').checked,
    brightness: Number($('brightness').value),
    color: parseInt($('color').value.slice(1), 16),
    effect: Number($('effect').value),
    alarm_enabled: $('alarm-enabled').checked,
    alarm_minutes: [...document.querySelectorAll('.alarm')].map(input => {
      const [hours, minutes] = input.value.split(':').map(Number);
      return hours * 60 + minutes;
    })
  };
  const response = await fetch('/api/save-light', {
    method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(payload)
  });
  lightSaveInProgress = false;
  $('light-save-status').textContent = response.ok ? 'Сохранено' : 'Ошибка сохранения';
  if (lightSaveQueued) scheduleLightSave();
}

function scheduleLightSave() {
  clearTimeout(lightSaveTimer);
  $('light-save-status').textContent = 'Изменения...';
  lightSaveTimer = setTimeout(() => {
    saveLightSettings().catch(() => {
      lightSaveInProgress = false;
      $('light-save-status').textContent = 'Ошибка соединения';
    });
  }, 50);
}

document.querySelectorAll('#light-form input, #light-form select').forEach(control => {
  if (control.id === 'enabled') return;
  control.addEventListener('change', scheduleLightSave);
  control.addEventListener('input', () => {
    if (control.id === 'brightness') $('brightness-value').textContent = control.value;
    scheduleLightSave();
  });
});

$('enabled').addEventListener('change', async () => {
  try {
    const response = await fetch('/api/set-light-enabled', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({enabled: $('enabled').checked})
    });
    if (!response.ok) throw new Error('Ошибка применения состояния');
    $('light-save-status').textContent = 'Сохранено';
  } catch (error) {
    $('light-save-status').textContent = error.message;
  }
});

$('light-form').addEventListener('submit', event => event.preventDefault());

document.querySelectorAll('.toggle-secret').forEach(button => {
  button.addEventListener('click', () => {
    const input = $(button.dataset.target);
    const isHidden = input.type === 'password';
    input.type = isHidden ? 'text' : 'password';
    button.textContent = isHidden ? 'Скрыть' : 'Показать';
    button.setAttribute('aria-label', `${isHidden ? 'Скрыть' : 'Показать'} секретное значение`);
  });
});

function applyColor(hex) {
  $('color').value = hex;
  $('color').dispatchEvent(new Event('input', { bubbles: true }));
}

function renderPresets() {
  const grid = $('preset-grid');
  grid.innerHTML = SOFT_COLORS.map(hex =>
    `<button type="button" class="preset-btn" style="background-color:${hex}" data-color="${hex}" title="${hex}"></button>`
  ).join('');
  grid.querySelectorAll('.preset-btn').forEach(btn => {
    btn.addEventListener('click', () => applyColor(btn.dataset.color));
  });
}

function renderFavorites() {
  const grid = $('favorite-grid');
  const hint = $('favorite-empty-hint');
  const hasAny = favoriteColors.some(c => c !== 0);
  hint.style.display = hasAny ? 'none' : 'block';
  grid.innerHTML = favoriteColors.map((color, index) => {
    const hex = colorToHex(color);
    const empty = color === 0;
    return `
      <div class="favorite-slot${empty ? ' empty' : ''}">
        ${empty
          ? `<button type="button" class="favorite-chip empty" disabled></button>
             <button type="button" class="fav-delete" disabled data-index="${index}">—</button>`
          : `<button type="button" class="favorite-chip" style="background-color:${hex}" data-index="${index}" data-color="${hex}" title="Применить ${hex}"></button>
             <button type="button" class="fav-delete" data-index="${index}">✕</button>`}
      </div>`;
  }).join('');
  grid.querySelectorAll('.favorite-chip:not(.empty)').forEach(btn => {
    btn.addEventListener('click', () => applyColor(btn.dataset.color));
  });
  grid.querySelectorAll('.fav-delete:not([disabled])').forEach(btn => {
    btn.addEventListener('click', () => removeFavorite(Number(btn.dataset.index)));
  });
}

async function saveToFavorites() {
  const color = parseInt($('color').value.slice(1), 16);
  if (color === 0) {
    message('Нельзя сохранить чёрный цвет');
    return;
  }
  const index = favoriteColors.findIndex(c => c === 0);
  if (index < 0) {
    message('Банк избранного заполнен');
    return;
  }
  try {
    const response = await fetch('/api/save-favorite', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({index, color})
    });
    if (!response.ok) throw new Error('Ошибка сохранения в избранное');
    favoriteColors[index] = color;
    renderFavorites();
    $('light-save-status').textContent = 'Сохранено в избранное';
  } catch (error) {
    message(error.message);
  }
}

async function removeFavorite(index) {
  try {
    const response = await fetch('/api/remove-favorite', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({index})
    });
    if (!response.ok) throw new Error('Ошибка удаления из избранного');
    favoriteColors[index] = 0;
    renderFavorites();
    $('light-save-status').textContent = 'Удалено из избранного';
  } catch (error) {
    message(error.message);
  }
}

$('save-favorite').addEventListener('click', saveToFavorites);

$('sunrise').addEventListener('click', async () => {
  const response = await fetch('/api/test-sunrise', {method: 'POST'});
  message(response.ok ? 'Тест рассвета запущен' : 'Ошибка запуска');
});

$('wifi-form').addEventListener('submit', async event => {
  event.preventDefault();
  const response = await fetch('/api/save-wifi', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      wifi_mode: $('wifi-mode').value, ssid: $('wifi-ssid').value,
      password: $('wifi-password').value,
      mqtt_token: $('mqtt-token').value
    })
  });
  message(response.ok ? 'Настройки сохранены, устройство перезагрузится' : 'Ошибка сохранения Wi-Fi');
});

new EventSource('/events').addEventListener('status_tick', event => {
  const [time, mode] = event.data.split('|');
  $('clock').textContent = time;
  $('net-status').textContent = mode;
});

loadConfig().then(() => {
  renderPresets();
  renderFavorites();
}).catch(error => message(error.message));

document.addEventListener('DOMContentLoaded', async () => {
    const titleEl = document.querySelector('header h1');
    if (titleEl) {
        titleEl.textContent = 'LightNode Control';
    }

    const section = document.querySelector('.tab-content.active');
    const form = section?.querySelector('.tab-twice');
    const btnCollapse = section?.querySelector('.btn-collapse');

    function toggleCollapse(isCollapsed) {
    if (!section || !form || !btnCollapse) return;

    if (isCollapsed) {
        form.style.maxHeight = '0px';
        section.classList.add('collapsed');
        btnCollapse.textContent = '↓';
    } else {
        section.classList.remove('collapsed');
        btnCollapse.textContent = '↑';

        form.style.maxHeight = 'none'; 

        const fullHeight = form.scrollHeight; 
        form.style.maxHeight = '0px'; 
        form.offsetHeight;
        form.style.maxHeight = fullHeight + 'px';
    }
    }

    const savedStatus = localStorage.getItem('sys_control_collapsed');
    const isCollapsed = savedStatus !== null ? savedStatus === 'true' : true;

    setTimeout(() => toggleCollapse(isCollapsed), 50);

    if (btnCollapse) {
    btnCollapse.addEventListener('click', () => {
        const currentlyCollapsed = section.classList.contains('collapsed');
        const nextState = !currentlyCollapsed;
        toggleCollapse(nextState);
        localStorage.setItem('sys_control_collapsed', String(nextState));
    });
    }
});