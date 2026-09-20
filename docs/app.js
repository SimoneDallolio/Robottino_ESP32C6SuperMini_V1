const SERVICE_UUID = '7f7b0001-8b3a-4f65-9d39-2c8c5a7e1001';
const CONFIG_UUID = '7f7b0002-8b3a-4f65-9d39-2c8c5a7e1001';
const STATUS_UUID = '7f7b0003-8b3a-4f65-9d39-2c8c5a7e1001';

const $ = (id) => document.getElementById(id);
const state = {
  device: null,
  characteristic: null,
  statusCharacteristic: null,
  firstBoot: false,
  pin: '',
  latitude: null,
  longitude: null,
  epoch: Date.now(),
  responseWaiter: null,
  gattQueue: Promise.resolve(),
  logRefreshTimer: null,
  logsRefreshInProgress: false
};

const wait = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

// Web Bluetooth consente una sola operazione GATT alla volta. La coda evita
// che un aggiornamento manuale dei log interferisca con quello automatico.
function queueGatt(operation) {
  const queued = state.gattQueue.then(operation, operation);
  state.gattQueue = queued.catch(() => undefined);
  return queued;
}

function showFeedback(elementId, message, error = false) {
  const element = $(elementId);
  // La schermata di configurazione non usa piu' un messaggio verde generale.
  if (!element) return;
  element.textContent = message;
  element.classList.toggle('error', error);
}

async function connectGatt() {
  if (!state.device) throw new Error('Nessun Robottino selezionato.');
  const server = state.device.gatt.connected
    ? state.device.gatt
    : await state.device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE_UUID);
  state.characteristic = await service.getCharacteristic(CONFIG_UUID);
  state.statusCharacteristic = await service.getCharacteristic(STATUS_UUID);
  await state.characteristic.startNotifications();
  state.characteristic.addEventListener('characteristicvaluechanged', handleResponse);
}

async function connectToRobot() {
  try {
    if (!('bluetooth' in navigator)) throw new Error('Web Bluetooth non supportato da questo browser.');
    showFeedback('welcomeFeedback', 'Seleziona il tuo Robottino...');
    state.device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'Robottino-' }],
      optionalServices: [SERVICE_UUID]
    });
    await connectGatt();
    const statusBytes = await state.statusCharacteristic.readValue();
    const status = JSON.parse(new TextDecoder().decode(statusBytes));
    state.firstBoot = status.firstBoot === true;
    // Lascia terminare sottoscrizione e lettura GATT prima del primo comando.
    await wait(500);
    // Carica subito il registro circolare, prima ancora dell'accesso al pannello.
    // I messaggi saranno gia disponibili non appena l'utente inserisce il PIN.
    await loadLogs();
    $('welcomeScreen').hidden = true;
    $('appHeader').hidden = false;
    $('accessPanel').hidden = false;
    $('appFooter').hidden = false;
    document.body.classList.add('app-mode', 'access-mode');
    $('firstBootPanel').hidden = !state.firstBoot;
    showFeedback('accessFeedback', state.firstBoot
      ? 'Primo avvio: il PIN iniziale è 0000 e devi sostituirlo.'
      : 'Inserisci il PIN per continuare.');
  } catch (error) {
    showFeedback('welcomeFeedback', error.message || 'Connessione BLE fallita.', true);
  }
}

function handleResponse(event) {
  const message = new TextDecoder().decode(event.target.value);
  try {
    const result = JSON.parse(message);
    // I log devono finire sempre nella loro card, anche se la risposta arriva
    // dopo il timeout di una precedente operazione Wi-Fi.
    if (Array.isArray(result.logs)) {
      $('logBuffer').textContent = result.logs.join('\n') || 'Nessun messaggio ricevuto.';
      if (state.responseWaiter) {
        const waiter = state.responseWaiter;
        state.responseWaiter = null;
        waiter(result);
      }
      return;
    }
    if (state.responseWaiter) {
      const waiter = state.responseWaiter;
      state.responseWaiter = null;
      waiter(result);
      return;
    }
    if (result.ok) {
      showFeedback('feedback', 'Configurazione salvata. BLE disattivato.');
    } else {
      showFeedback('accessFeedback', result.error || 'PIN non valido.', true);
      showFeedback('feedback', result.error || 'Configurazione rifiutata.', true);
    }
  } catch {
    showFeedback('feedback', message);
  }
}

async function writePayload(payload) {
  const bytes = new TextEncoder().encode(JSON.stringify(payload));
  try {
    await state.characteristic.writeValueWithResponse(bytes);
  } catch (error) {
    if (!state.device || state.device.gatt.connected) throw error;
    await connectGatt();
    await state.characteristic.writeValueWithResponse(bytes);
  }
}

function writeAndWait(payload, timeoutMs = 3000) {
  return new Promise(async (resolve, reject) => {
    state.responseWaiter = resolve;
    try {
      await writePayload(payload);
      setTimeout(() => {
        if (!state.responseWaiter) return;
        state.responseWaiter = null;
        reject(new Error('Nessuna risposta dal Robottino.'));
      }, timeoutMs);
    } catch (error) {
      state.responseWaiter = null;
      reject(error);
    }
  });
}

async function requestRobot(action, details = {}, timeoutMs = 3000) {
  return queueGatt(async () => {
    const result = await writeAndWait({ action, ...details }, timeoutMs);
    if (!result.ok) throw new Error(result.error || 'Il Robottino non ha completato la richiesta.');
    return result;
  });
}

function renderNetworks(elementId, networks, nearby = false) {
  const container = $(elementId);
  container.replaceChildren();
  if (!networks.length) {
    container.textContent = nearby ? 'Nessuna rete trovata.' : 'Nessuna rete salvata.';
    return;
  }
  networks.forEach((network) => {
    const ssid = typeof network === 'string' ? network : network.ssid;
    if (!ssid) return;
    const button = document.createElement('button');
    button.className = 'network-item';
    button.type = 'button';
    button.textContent = nearby ? `${ssid}  (${network.rssi} dBm${network.secure ? ', protetta' : ''})` : ssid;
    button.addEventListener('click', () => {
      $('ssid').value = ssid;
      showFeedback('feedback', `Rete selezionata: ${ssid}`);
    });
    if (nearby) {
      container.append(button);
      return;
    }
    const row = document.createElement('div');
    row.className = 'saved-network-row';
    const remove = document.createElement('button');
    remove.className = 'remove-network';
    remove.type = 'button';
    remove.textContent = 'Elimina';
    remove.addEventListener('click', () => deleteSavedNetwork(ssid));
    row.append(button, remove);
    container.append(row);
  });
}

async function loadSavedNetworks() {
  const result = await requestRobot('savedNetworks');
  renderNetworks('savedNetworks', result.networks || []);
}

async function deleteSavedNetwork(ssid) {
  try {
    await requestRobot('deleteWifi', { pin: state.pin, ssid });
    showFeedback('feedback', `Rete eliminata: ${ssid}`);
    await loadSavedNetworks();
  } catch (error) { showFeedback('feedback', error.message, true); }
}

async function scanWifi() {
  try {
    $('nearbyNetworks').textContent = 'Scansione in corso... attendere (potrebbe richiedere qualche secondo).';
    const result = await requestRobot('scanWifi', {}, 12000);
    renderNetworks('nearbyNetworks', result.networks || [], true);
  } catch (error) { 
    $('nearbyNetworks').textContent = error.message; 
  }
}

async function retryWifi() {
  try {
    // Il firmware prova ogni rete fino a tre volte: questa operazione puo'
    // richiedere alcuni minuti senza che la connessione BLE sia guasta.
    await requestRobot('retryWifi', {}, 300000);
    await loadLogs();
  } catch (error) { await loadLogs(); }
}

async function loadLogs() {
  if (state.logsRefreshInProgress) return;
  state.logsRefreshInProgress = true;
  try {
    const allLogs = [];
    let offset = 0;
    let done = false;
    let total = 0;
    while (!done) {
      const result = await requestRobot('logs', { offset });
      allLogs.push(...(result.logs || []));
      done = result.done === true;
      offset = result.nextOffset;
      total = result.total;
      if (!Number.isInteger(offset) || offset > 20) throw new Error('Risposta log non valida.');
      if (!done) await wait(80);
    }
    $('logBuffer').textContent = allLogs.reverse().join('\n') || 'Nessun messaggio ricevuto.';
    $('logCount').textContent = `(${total}/20)`;
  } catch (error) {
    $('logBuffer').textContent = `Impossibile leggere i messaggi: ${error.message}`;
    $('logCount').textContent = '';
  } finally {
    state.logsRefreshInProgress = false;
  }
}

function startLogAutoRefresh() {
  clearInterval(state.logRefreshTimer);
  state.logRefreshTimer = setInterval(() => {
    if (state.device?.gatt?.connected) loadLogs();
  }, 5000);
}

async function stopBle() {
  try {
    const result = await requestRobot('stopBle');
    showFeedback('feedback', result.message || 'BLE disattivato.');
    setTimeout(() => {
      $('setupContent').hidden = true;
      $('appHeader').hidden = true;
      $('appFooter').hidden = true;
      $('welcomeScreen').hidden = false;
    }, 600);
  } catch (error) { showFeedback('feedback', error.message, true); }
}

async function verifyAccess() {
  const pin = $('pin').value.trim();
  if (!/^\d{4}$/.test(pin)) throw new Error('Il PIN deve contenere 4 cifre.');

  if (state.firstBoot) {
    const newPin = $('newPin').value.trim();
    if (pin !== '0000') throw new Error('Al primo avvio devi usare il PIN iniziale 0000.');
    if (!/^\d{4}$/.test(newPin) || newPin === '0000') throw new Error('Scegli un nuovo PIN di 4 cifre diverso da 0000.');
    state.pin = pin;
  } else {
    const result = await writeAndWait({ action: 'auth', pin });
    if (!result.ok) throw new Error(result.error || 'PIN non valido.');
    state.pin = pin;
  }

  $('accessPanel').hidden = true;
  $('setupContent').hidden = false;
  document.body.classList.remove('access-mode');
  document.body.classList.add('setup-mode');
  $('appHeader').scrollIntoView({ behavior: 'smooth', block: 'start' });
  showFeedback('feedback', state.firstBoot ? 'PIN iniziale verificato: completa la nuova configurazione.' : 'Accesso verificato.');
  await loadSavedNetworks();
  await loadLogs();
  startLogAutoRefresh();
}

function detectLocation() {
  if (!navigator.geolocation) return showFeedback('feedback', 'Geolocalizzazione non disponibile.', true);
  navigator.geolocation.getCurrentPosition((position) => {
    state.latitude = position.coords.latitude;
    state.longitude = position.coords.longitude;
    $('locationText').textContent = `${state.latitude.toFixed(5)}, ${state.longitude.toFixed(5)}`;
  }, () => showFeedback('feedback', 'Permesso GPS negato o posizione non disponibile.', true), { enableHighAccuracy: true, timeout: 10000 });
}

function syncTime() {
  state.epoch = Date.now();
  $('timeText').textContent = new Date(state.epoch).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' });
}

async function sendConfiguration() {
  try {
    const ssid = $('ssid').value.trim();
    const pass = $('wifiPassword').value;
    if (!ssid) throw new Error('Inserisci il nome della rete Wi-Fi.');
    if (!state.firstBoot) {
      const result = await requestRobot('saveWifi', { pin: state.pin, ssid, pass });
      showFeedback('feedback', result.message || 'Rete Wi-Fi salvata.');
      await loadSavedNetworks();
      return;
    }
    if (state.latitude === null || state.longitude === null) throw new Error('Al primo avvio rileva prima la posizione GPS.');
    const payload = {
      pin: state.pin,
      newPin: state.firstBoot ? $('newPin').value.trim() : '',
      ssid,
      pass,
      time: Math.floor(state.epoch / 1000),
      lat: state.latitude,
      lon: state.longitude
    };
    await writePayload(payload);
    showFeedback('feedback', 'Configurazione inviata, attendo conferma...');
  } catch (error) {
    showFeedback('feedback', error.message || 'Errore BLE.', true);
  }
}

$('connectButton').addEventListener('click', connectToRobot);
$('unlockButton').addEventListener('click', () => verifyAccess().catch((error) => showFeedback('accessFeedback', error.message, true)));
$('locationButton').addEventListener('click', detectLocation);
$('timeButton').addEventListener('click', syncTime);
$('sendButton').addEventListener('click', sendConfiguration);
$('scanWifiButton').addEventListener('click', scanWifi);
$('retryWifiButton').addEventListener('click', retryWifi);
$('refreshLogsButton').addEventListener('click', loadLogs);
$('stopBleButton').addEventListener('click', stopBle);
$('accessPanel').hidden = true;
$('setupContent').hidden = true;
syncTime();

if ('serviceWorker' in navigator) window.addEventListener('load', () => navigator.serviceWorker.register('sw.js'));
