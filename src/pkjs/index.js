// Phone side of Find iPhone. Sends a loud "critical" push to your iPhone through the
// Home Assistant app, so it dings even on silent. No Apple ID involved.

// The settings page is the same one your TV remote uses (it already has the Home Assistant
// address, token and notify-service fields).
var CONFIG_URL = 'https://eddieatjollyroger.github.io/PebblePingMyiPhone/';

var PING_GAP_MS = 5000;   // time between dings when "keep ringing"
var RING_MAX = 15;        // stop by itself after this many (about a minute)

var ringing = false;
var ringCount = 0;
var ringTimer = null;

function themeValue() {
  return localStorage.getItem('theme') === 'light' ? 1 : 0;
}

/* ---------- Talking to the watch ---------- */

function send(payload, tries) {
  Pebble.sendAppMessage(payload, null, function() {
    if (tries < 2) setTimeout(function() { send(payload, tries + 1); }, 800);
  });
}

function report(text, isRinging) {
  send({ '1': text, '3': isRinging ? 1 : 0 }, 0);
}

/* ---------- Home Assistant ---------- */

function haBase() {
  var url = localStorage.getItem('ha_url');
  var token = localStorage.getItem('ha_token');
  if (!url || !token) return null;
  if (url.charAt(url.length - 1) === '/') url = url.slice(0, -1);
  return { url: url, token: token };
}

// The phone's notify service, like "mobile_app_my_iphone". From the settings page if set,
// otherwise every notify.mobile_app_* service Home Assistant lists.
function phoneTargets(done) {
  var saved = localStorage.getItem('notify_service');
  if (saved) { done([saved]); return; }

  var ha = haBase();
  if (!ha) { done([]); return; }

  var xhr = new XMLHttpRequest();
  xhr.open('GET', ha.url + '/api/services', true);
  xhr.setRequestHeader("Authorization", "Bearer " + ha.token);
  xhr.onload = function() {
    var found = [];
    if (xhr.status === 200) {
      try {
        JSON.parse(xhr.responseText).forEach(function(d) {
          if (d.domain !== 'notify') return;
          Object.keys(d.services || {}).forEach(function(n) {
            if (n.indexOf('mobile_app_') === 0) found.push(n);
          });
        });
      } catch (e) { console.log('service list parse error: ' + e); }
    }
    done(found);
  };
  xhr.onerror = function() { done([]); };
  xhr.send();
}

function sendPing(service, cb) {
  var ha = haBase();
  var xhr = new XMLHttpRequest();
  xhr.open('POST', ha.url + '/api/services/notify/' + service, true);
  xhr.setRequestHeader("Authorization", "Bearer " + ha.token);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) cb(true, '');
    else {
      console.log("HA error " + xhr.status + ": " + xhr.responseText);
      cb(false, 'error ' + xhr.status);
    }
  };
  xhr.onerror = function() { cb(false, 'no connection'); };
  xhr.send(JSON.stringify({
    "title": "Find my iPhone",
    "message": "Ping from your Pebble",
    "data": {
      "push": {
        "sound": { "name": "default", "critical": 1, "volume": 1.0 },
        "interruption-level": "critical"
      }
    }
  }));
}

function clock() {
  var d = new Date();
  var m = d.getMinutes();
  return d.getHours() + ':' + (m < 10 ? '0' + m : m);
}

// Pings every matching phone once. done(text) gets "pinged 20:41" or what went wrong.
function doPing(done) {
  if (!haBase()) { done('no HA settings'); return; }
  phoneTargets(function(targets) {
    if (!targets.length) { done('no phone found'); return; }
    var left = targets.length, anyOk = false, lastError = '';
    targets.forEach(function(t) {
      sendPing(t, function(ok, err) {
        if (ok) anyOk = true; else lastError = err;
        left--;
        if (left === 0) done(anyOk ? 'pinged ' + clock() : lastError);
      });
    });
  });
}

/* ---------- Ringing ---------- */

function stopRing(text) {
  ringing = false;
  if (ringTimer) clearTimeout(ringTimer);
  ringTimer = null;
  report(text || 'stopped', false);
}

function ringTick() {
  if (!ringing) return;
  ringCount++;
  var n = ringCount;
  doPing(function(result) {
    if (result.indexOf('pinged') !== 0) { stopRing(result); return; }   // something's wrong: stop
    if (ringing) report('ringing (' + n + ')', true);
  });
  if (ringCount >= RING_MAX) {
    ringTimer = setTimeout(function() { stopRing('stopped (time limit)'); }, PING_GAP_MS);
  } else {
    ringTimer = setTimeout(ringTick, PING_GAP_MS);
  }
}

function startRing() {
  if (ringing) return;
  ringing = true;
  ringCount = 0;
  report('ringing...', true);
  ringTick();
}

/* ---------- Events ---------- */

Pebble.addEventListener('ready', function() {
  send({ '2': themeValue(), '1': 'ready', '3': 0 }, 0);
});

Pebble.addEventListener('appmessage', function(e) {
  if (!e.payload || e.payload['0'] === undefined) return;
  var cmd = e.payload['0'];
  if (cmd === 1) {                       // ping once
    doPing(function(result) { report(result, false); });
  } else if (cmd === 2) {                // keep ringing
    startRing();
  } else if (cmd === 3) {                // stop
    stopRing('stopped');
  }
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(CONFIG_URL + '?title=' + encodeURIComponent('Find iPhone') +
    '&url=' + encodeURIComponent(localStorage.getItem('ha_url') || '') +
    '&token=' + encodeURIComponent(localStorage.getItem('ha_token') || '') +
    '&notify=' + encodeURIComponent(localStorage.getItem('notify_service') || '') +
    '&theme=' + (themeValue() ? 'light' : 'dark') +
    '&return_to=' + encodeURIComponent('pebblejs://close#'));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response) return;
  try {
    var cfg = JSON.parse(decodeURIComponent(e.response));
    if (cfg.ha_url) localStorage.setItem('ha_url', cfg.ha_url);
    if (cfg.ha_token) localStorage.setItem('ha_token', cfg.ha_token);
    if (typeof cfg.notify_service === 'string') {
      // keep just the service name, e.g. "notify.mobile_app_x" -> "mobile_app_x"
      localStorage.setItem('notify_service', cfg.notify_service.toLowerCase().replace(/^notify\./, '').replace(/[^a-z0-9_]/g, ''));
    }
    if (cfg.theme === 'light' || cfg.theme === 'dark') localStorage.setItem('theme', cfg.theme);
    send({ '2': themeValue(), '1': 'saved', '3': 0 }, 0);
  } catch (err) {
    console.log('settings parse error: ' + err);
  }
});
