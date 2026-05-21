var Settings = require('settings');

// ── Constants ─────────────────────────────────────────────────────────────────
var KEY_CMD              = 0;
var KEY_STATE_LOCKED     = 1;
var KEY_STATE_CLIMATE    = 2;
var KEY_STATE_CHARGE_MIN = 3;
var KEY_STATE_CHARGE_PCT = 4;
var KEY_STATE_RANGE_KM   = 5;
var KEY_STATE_ODO_KM     = 6;
var KEY_STATE_LOCATION   = 7;
var KEY_STATE_OUTSIDE_TEMP = 8;
var KEY_STATE_IS_CHARGING  = 9;
var KEY_SETTING_UNITS    = 10;
var KEY_SETTING_API_KEY  = 11;
var KEY_ERROR            = 12;

var CMD_REFRESH          = 1;
var CMD_TOGGLE_LOCK      = 2;
var CMD_TOGGLE_CLIMATE   = 3;
var CMD_HONK             = 4;

// ── Polestar API ──────────────────────────────────────────────────────────────
// Unofficial API: https://github.com/kildahldev/unofficial-polestar-api
var API_BASE = 'https://pc-api.polestar.com/eu-north-1/mystar-v2/';
var AUTH_URL = 'https://polestarid.polestar.com/as/token.oauth2';

var s_token = null;
var s_vehicle_id = null;
var s_username = null;
var s_password = null;

function getStoredCreds() {
  var raw = localStorage.getItem('polestar_creds');
  if (raw) {
    try {
      var creds = JSON.parse(raw);
      s_username = creds.username;
      s_password = creds.password;
    } catch (e) {}
  }
}

function authenticate(callback) {
  if (!s_username || !s_password) {
    callback(new Error('No credentials'));
    return;
  }
  var body = 'grant_type=password'
    + '&username=' + encodeURIComponent(s_username)
    + '&password=' + encodeURIComponent(s_password)
    + '&client_id=polaris-android'
    + '&scope=openid+profile+email+customer:attributes';

  var xhr = new XMLHttpRequest();
  xhr.open('POST', AUTH_URL, true);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  xhr.onload = function() {
    if (xhr.status === 200) {
      var data = JSON.parse(xhr.responseText);
      s_token = data.access_token;
      callback(null);
    } else {
      callback(new Error('Auth failed: ' + xhr.status));
    }
  };
  xhr.onerror = function() { callback(new Error('Network error')); };
  xhr.send(body);
}

function apiGet(path, callback) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', API_BASE + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + s_token);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function() {
    if (xhr.status === 200) {
      callback(null, JSON.parse(xhr.responseText));
    } else {
      callback(new Error('API error: ' + xhr.status));
    }
  };
  xhr.onerror = function() { callback(new Error('Network error')); };
  xhr.send();
}

function apiPost(path, body, callback) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', API_BASE + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + s_token);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function() {
    if (xhr.status === 200 || xhr.status === 204) {
      callback(null);
    } else {
      callback(new Error('API error: ' + xhr.status));
    }
  };
  xhr.onerror = function() { callback(new Error('Network error')); };
  xhr.send(body ? JSON.stringify(body) : null);
}

function getVehicleId(callback) {
  if (s_vehicle_id) { callback(null, s_vehicle_id); return; }
  apiGet('vehicles', function(err, data) {
    if (err) { callback(err); return; }
    var vehicles = (data && data.data && data.data.getConsumerCarsV2) || [];
    if (vehicles.length === 0) { callback(new Error('No vehicle found')); return; }
    s_vehicle_id = vehicles[0].id;
    callback(null, s_vehicle_id);
  });
}

// ── Reverse geocode ───────────────────────────────────────────────────────────
function reverseGeocode(lat, lon, callback) {
  var url = 'https://nominatim.openstreetmap.org/reverse?format=json&lat='
    + lat + '&lon=' + lon;
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.setRequestHeader('User-Agent', 'PolestarPebble/1.0');
  xhr.onload = function() {
    if (xhr.status === 200) {
      var data = JSON.parse(xhr.responseText);
      var addr = data.address || {};
      var parts = [];
      if (addr.house_number && addr.road)
        parts.push(addr.house_number + ' ' + addr.road);
      else if (addr.road)
        parts.push(addr.road);
      if (addr.city || addr.town || addr.village)
        parts.push(addr.city || addr.town || addr.village);
      if (addr.state && addr.postcode)
        parts.push(addr.state + ' ' + addr.postcode);
      callback(parts.join(', ') || data.display_name || 'Unknown');
    } else {
      callback('Location unavailable');
    }
  };
  xhr.onerror = function() { callback('Location unavailable'); };
  xhr.send();
}

// ── Fetch all state ───────────────────────────────────────────────────────────
function fetchAndSend() {
  getStoredCreds();
  authenticate(function(authErr) {
    if (authErr) { sendError(); return; }
    getVehicleId(function(idErr, vid) {
      if (idErr) { sendError(); return; }

      var battery = null, odometer = null, location_data = null;
      var done = 0;
      var total = 3;

      function tryFinish() {
        done++;
        if (done < total) return;
        if (!battery || !odometer) { sendError(); return; }

        var battData = (battery.data && battery.data.getBatteryData) || {};
        var odoData  = (odometer.data && odometer.data.getOdometerData) || {};
        var locData  = (location_data && location_data.data && location_data.data.getCarLocationData) || {};

        var is_charging = battData.chargingStatus === 'CHARGING_STATUS_CHARGING';
        var charge_min  = is_charging ? (battData.estimatedChargingTimeMinutesToTargetDistance || -1) : -1;
        var charge_pct  = Math.round((battData.batteryChargeLevelPercentage || 0));
        var range_km    = Math.round((battData.estimatedDistanceToEmptyKm || 0));
        var odo_km      = Math.round((odoData.odometerMeters || 0) / 1000);
        var lat         = locData.latitude || 0;
        var lon         = locData.longitude || 0;

        function sendAll(location_str) {
          var msg = {};
          msg[KEY_STATE_IS_CHARGING]  = is_charging ? 1 : 0;
          msg[KEY_STATE_CHARGE_MIN]   = charge_min;
          msg[KEY_STATE_CHARGE_PCT]   = charge_pct;
          msg[KEY_STATE_RANGE_KM]     = range_km;
          msg[KEY_STATE_ODO_KM]       = odo_km;
          msg[KEY_STATE_LOCATION]     = location_str;
          // outside temp comes from climate data if available
          Pebble.sendAppMessage(msg, function() {}, function() {});
        }

        if (lat && lon) {
          reverseGeocode(lat, lon, sendAll);
        } else {
          sendAll('Location unavailable');
        }
      }

      apiGet('vehicles/' + vid + '/battery', function(err, data) {
        battery = err ? {} : data; tryFinish();
      });
      apiGet('vehicles/' + vid + '/odometer', function(err, data) {
        odometer = err ? {} : data; tryFinish();
      });
      apiGet('vehicles/' + vid + '/location', function(err, data) {
        location_data = err ? {} : data; tryFinish();
      });
    });
  });
}

function sendError() {
  var msg = {};
  msg[KEY_ERROR] = 1;
  Pebble.sendAppMessage(msg, function() {}, function() {});
}

// ── Command handlers ──────────────────────────────────────────────────────────
function handleCmd(cmd) {
  if (cmd === CMD_REFRESH) {
    fetchAndSend();
    return;
  }

  getStoredCreds();
  authenticate(function(authErr) {
    if (authErr) { sendError(); return; }
    getVehicleId(function(idErr, vid) {
      if (idErr) { sendError(); return; }

      if (cmd === CMD_TOGGLE_LOCK) {
        // POST lock or unlock based on last known state
        apiPost('vehicles/' + vid + '/lock', null, function(err) {
          if (!err) fetchAndSend();
        });
      } else if (cmd === CMD_TOGGLE_CLIMATE) {
        apiPost('vehicles/' + vid + '/climate/toggle', null, function(err) {
          if (!err) fetchAndSend();
        });
      } else if (cmd === CMD_HONK) {
        apiPost('vehicles/' + vid + '/honk-blink', null, function() {
          // fire and forget — no state update needed
        });
      }
    });
  });
}

// ── Pebble events ─────────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  getStoredCreds();
  if (!s_username || !s_password) {
    Pebble.openURL('pebblejs://close');
    return;
  }
  fetchAndSend();
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;
  if (payload[KEY_CMD] !== undefined) {
    handleCmd(payload[KEY_CMD]);
  }
});

var CONFIG_HTML = [
  '<!DOCTYPE html><html lang="en"><head>',
  '<meta charset="UTF-8"/>',
  '<meta name="viewport" content="width=device-width,initial-scale=1.0"/>',
  '<title>Polestar</title>',
  '<style>',
  '*{box-sizing:border-box;margin:0;padding:0}',
  'body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#111;color:#fff;padding:24px 16px 40px}',
  'h1{font-size:22px;font-weight:700;margin-bottom:6px;color:#F5820A}',
  'p.sub{font-size:13px;color:#888;margin-bottom:28px}',
  '.section{background:#1e1e1e;border-radius:12px;padding:16px;margin-bottom:16px}',
  '.section h2{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:#888;margin-bottom:12px}',
  'label{display:block;font-size:15px;margin-bottom:4px;color:#ccc}',
  'input[type=email],input[type=password]{width:100%;padding:10px 12px;background:#2a2a2a;border:1px solid #333;border-radius:8px;color:#fff;font-size:16px;margin-bottom:14px;outline:none}',
  'input:focus{border-color:#F5820A}',
  '.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0}',
  '.row span{font-size:15px}',
  'input[type=checkbox]{width:42px;height:24px;accent-color:#F5820A;cursor:pointer}',
  'button{width:100%;padding:14px;background:#F5820A;color:#fff;border:none;border-radius:12px;font-size:17px;font-weight:700;cursor:pointer;margin-top:8px}',
  'button:active{background:#d46e08}',
  '.err{color:#ff5555;font-size:13px;margin-top:6px;display:none}',
  '</style></head><body>',
  '<h1>Polestar</h1>',
  '<p class="sub">Connect your Polestar account to your watch.</p>',
  '<div class="section"><h2>Account</h2>',
  '<label for="e">Polestar email</label>',
  '<input type="email" id="e" placeholder="you@example.com"/>',
  '<label for="p">Password</label>',
  '<input type="password" id="p" placeholder="••••••••"/>',
  '<div class="err" id="err">Email and password are required.</div>',
  '</div>',
  '<div class="section"><h2>Units</h2>',
  '<div class="row"><span>Use metric (km, °C)</span>',
  '<input type="checkbox" id="m" checked/></div></div>',
  '<button onclick="save()">Save to Watch</button>',
  '<script>',
  'try{var s=JSON.parse(localStorage.getItem("polestar_creds")||"{}");',
  'if(s.username)document.getElementById("e").value=s.username;}catch(ex){}',
  'if(localStorage.getItem("use_metric")==="false")document.getElementById("m").checked=false;',
  'function save(){',
  'var e=document.getElementById("e").value.trim(),',
  'p=document.getElementById("p").value,',
  'm=document.getElementById("m").checked,',
  'err=document.getElementById("err");',
  'if(!e||!p){err.style.display="block";return;}',
  'err.style.display="none";',
  'localStorage.setItem("polestar_creds",JSON.stringify({username:e,password:p}));',
  'localStorage.setItem("use_metric",m?"true":"false");',
  'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({metric:m}));}',
  '<\/script></body></html>'
].join('');

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html,' + encodeURIComponent(CONFIG_HTML));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response) {
    try {
      var data = JSON.parse(decodeURIComponent(e.response));
      var use_metric = data.metric !== false ? 1 : 0;
      localStorage.setItem('use_metric', use_metric ? 'true' : 'false');
      var msg = {};
      msg[KEY_SETTING_UNITS] = use_metric;
      Pebble.sendAppMessage(msg, function() {}, function() {});
    } catch (err) {}
  }
});
