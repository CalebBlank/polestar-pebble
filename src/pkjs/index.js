var Clay = require('./clay');
var clay;

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
var KEY_STATE_DISTANCE_M = 13;
var KEY_SETTING_LIGHT_TEXT = 14;

var CMD_REFRESH          = 1;
var CMD_TOGGLE_LOCK      = 2;
var CMD_TOGGLE_CLIMATE   = 3;
var CMD_HONK             = 4;
var CMD_NAVIGATE         = 5;

// ── Polestar API ──────────────────────────────────────────────────────────────
// Unofficial API: https://github.com/kildahldev/unofficial-polestar-api
var API_BASE = 'https://pc-api.polestar.com/eu-north-1/mystar-v2/';
var AUTH_URL = 'https://polestarid.polestar.com/as/token.oauth2';

var s_token = null;
var s_vehicle_id = null;
var s_username = null;
var s_password = null;
var s_car_lat = 0;
var s_car_lng = 0;

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
      var street = addr.house_number && addr.road
        ? addr.house_number + ' ' + addr.road
        : (addr.road || '');
      var city  = addr.city || addr.town || addr.village || '';
      var state = addr.state || '';
      var cityState = city && state ? city + ', ' + state : (city || state);
      callback((street ? street + '\n' : '') + (cityState || data.display_name || 'Unknown'));
    } else {
      callback('Location unavailable');
    }
  };
  xhr.onerror = function() { callback('Location unavailable'); };
  xhr.send();
}

// ── Distance ──────────────────────────────────────────────────────────────────
function haversineM(lat1, lon1, lat2, lon2) {
  var R = 6371000;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return Math.round(R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)));
}

function sendDistance() {
  navigator.geolocation.getCurrentPosition(function(pos) {
    if (s_car_lat || s_car_lng) {
      var d = haversineM(pos.coords.latitude, pos.coords.longitude, s_car_lat, s_car_lng);
      var msg = {};
      msg[KEY_STATE_DISTANCE_M] = d;
      Pebble.sendAppMessage(msg, function() {}, function() {});
    }
  }, function() {}, { timeout: 10000, maximumAge: 60000 });
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
        s_car_lat = lat;
        s_car_lng = lon;

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
          sendDistance();
          if (is_charging && charge_min > 0) {
            pushChargePin(charge_min, charge_pct);
          } else {
            deleteChargePin();
          }
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
  if (cmd === CMD_NAVIGATE) {
    if (s_car_lat !== 0 || s_car_lng !== 0) {
      Pebble.openURL('https://maps.google.com/?q=' + s_car_lat + ',' + s_car_lng);
    }
    return;
  }

  if (cmd === CMD_REFRESH) {
    getStoredCreds();
    if (!s_username || !s_password) { sendMockData(); return; }
    fetchAndSend();
    return;
  }

  getStoredCreds();
  authenticate(function(authErr) {
    if (authErr) { sendError(); return; }
    getVehicleId(function(idErr, vid) {
      if (idErr) { sendError(); return; }

      if (cmd === CMD_TOGGLE_LOCK) {
        apiPost('vehicles/' + vid + '/lock', null, function(err) {
          if (!err) fetchAndSend();
        });
      } else if (cmd === CMD_TOGGLE_CLIMATE) {
        apiPost('vehicles/' + vid + '/climate/toggle', null, function(err) {
          if (!err) fetchAndSend();
        });
      } else if (cmd === CMD_HONK) {
        apiPost('vehicles/' + vid + '/honk-blink', null, function() {});
      }
    });
  });
}

// ── Mock data (shown when no credentials are configured) ──────────────────────
function sendMockData() {
  s_car_lat = 42.1148;
  s_car_lng = -88.0039;
  var msg = {};
  msg[KEY_STATE_LOCKED]      = 1;
  msg[KEY_STATE_CLIMATE]     = 0;
  msg[KEY_STATE_IS_CHARGING] = 1;
  msg[KEY_STATE_CHARGE_MIN]  = 25;
  msg[KEY_STATE_CHARGE_PCT]  = 80;
  msg[KEY_STATE_RANGE_KM]    = 180;
  msg[KEY_STATE_ODO_KM]      = 12305;
  msg[KEY_STATE_LOCATION]    = '136 S Ash St\nPalatine, IL';
  msg[KEY_STATE_OUTSIDE_TEMP]= 60;
  var metric = localStorage.getItem('use_metric') !== 'false';
  var light  = localStorage.getItem('light_text') === 'true';
  msg[KEY_SETTING_UNITS]      = metric ? 1 : 0;
  msg[KEY_SETTING_LIGHT_TEXT] = light  ? 1 : 0;
  msg[KEY_STATE_DISTANCE_M]  = 2200;
  Pebble.sendAppMessage(msg, function() {}, function() {});
}

// ── Timeline pins ─────────────────────────────────────────────────────────────
var TIMELINE_API = 'https://timeline-api.rebble.io/v1/user/pins/';
var CHARGE_PIN_ID = 'polestar-charge-complete';

function pushChargePin(charge_min, charge_pct) {
  Pebble.getTimelineToken(function(token) {
    var done = new Date(Date.now() + charge_min * 60 * 1000);
    var pin = {
      id: CHARGE_PIN_ID,
      time: done.toISOString(),
      duration: 1,
      layout: {
        type: 'genericPin',
        title: 'Polestar fully charged',
        subtitle: charge_pct + '% battery',
        tinyIcon: 'system://images/GENERIC_CONFIRMATION'
      },
      reminders: [
        {
          time: new Date(done.getTime() - 10 * 60 * 1000).toISOString(),
          layout: {
            type: 'genericReminder',
            title: 'Polestar charges in 10 min'
          }
        }
      ]
    };
    var xhr = new XMLHttpRequest();
    xhr.open('PUT', TIMELINE_API + CHARGE_PIN_ID, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.setRequestHeader('X-User-Token', token);
    xhr.onload = function() {
      console.log('Timeline pin: ' + xhr.status);
    };
    xhr.send(JSON.stringify(pin));
  }, function(err) {
    console.log('Timeline token error: ' + err);
  });
}

function deleteChargePin() {
  Pebble.getTimelineToken(function(token) {
    var xhr = new XMLHttpRequest();
    xhr.open('DELETE', TIMELINE_API + CHARGE_PIN_ID, true);
    xhr.setRequestHeader('X-User-Token', token);
    xhr.send();
  }, function() {});
}

// ── Pebble events ─────────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  getStoredCreds();
  setTimeout(function() {
    var metric = localStorage.getItem('use_metric') !== 'false';
    var light  = localStorage.getItem('light_text') === 'true';
    var settingsMsg = {};
    settingsMsg[KEY_SETTING_UNITS]      = metric ? 1 : 0;
    settingsMsg[KEY_SETTING_LIGHT_TEXT] = light  ? 1 : 0;
    // Send settings first, then fetch car data in the ACK callback so
    // the two sendAppMessage calls don't collide.
    Pebble.sendAppMessage(settingsMsg, function() {
      if (!s_username || !s_password) { sendMockData(); return; }
      fetchAndSend();
    }, function() {
      if (!s_username || !s_password) { sendMockData(); return; }
      fetchAndSend();
    });
  }, 500);
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;
  if (payload[KEY_CMD] !== undefined) {
    handleCmd(payload[KEY_CMD]);
  }
});

// ── Clay configuration ────────────────────────────────────────────────────────
// Builds the Clay config dynamically so saved credentials/settings are pre-filled.
function buildClayConfig() {
  var creds = {};
  try { creds = JSON.parse(localStorage.getItem('polestar_creds') || '{}'); } catch(e2) {}
  var metric = localStorage.getItem('use_metric') !== 'false';
  var light  = localStorage.getItem('light_text') === 'true';
  return [
    { 'type': 'heading', 'defaultValue': 'Polestar' },
    { 'type': 'section', 'items': [
      { 'type': 'heading', 'defaultValue': 'Account' },
      { 'type': 'input', 'id': 'email', 'messageKey': null,
        'label': 'Polestar email', 'defaultValue': creds.username || '',
        'attributes': { 'type': 'email', 'placeholder': 'you@example.com' } },
      { 'type': 'input', 'id': 'password', 'messageKey': null,
        'label': 'Password', 'defaultValue': creds.password || '',
        'attributes': { 'type': 'password' } }
    ]},
    { 'type': 'section', 'items': [
      { 'type': 'heading', 'defaultValue': 'Units' },
      { 'type': 'toggle', 'messageKey': 'SETTING_UNITS',
        'label': 'Metric (km, °C)', 'defaultValue': metric }
    ]},
    { 'type': 'section', 'items': [
      { 'type': 'heading', 'defaultValue': 'Display' },
      { 'type': 'toggle', 'messageKey': 'SETTING_LIGHT_TEXT',
        'label': 'Black text (light mode)', 'defaultValue': light }
    ]},
    { 'type': 'submit', 'defaultValue': 'Save & Refresh' }
  ];
}

Pebble.addEventListener('showConfiguration', function() {
  clay = new Clay(buildClayConfig(), null, { autoHandleEvents: false });
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  try {
    var settings = clay.getSettings(e.response);

    // Credentials: Clay fields with messageKey=null are keyed by id
    var email    = settings.email    ? String(settings.email.value    || '').trim() : '';
    var password = settings.password ? String(settings.password.value || '')         : '';
    if (email || password) {
      var existing = {};
      try { existing = JSON.parse(localStorage.getItem('polestar_creds') || '{}'); } catch(e3) {}
      if (email)    existing.username = email;
      if (password) existing.password = password;
      localStorage.setItem('polestar_creds', JSON.stringify(existing));
      getStoredCreds();
    }

    // Settings: sent to watch automatically by Clay (via messageKey mapping),
    // but also persist locally for re-injection at next showConfiguration.
    var metric = settings.SETTING_UNITS      ? !!settings.SETTING_UNITS.value      : true;
    var light  = settings.SETTING_LIGHT_TEXT ? !!settings.SETTING_LIGHT_TEXT.value : false;
    localStorage.setItem('use_metric', metric ? 'true' : 'false');
    localStorage.setItem('light_text', light  ? 'true' : 'false');

    var msg = {};
    msg[KEY_SETTING_UNITS]      = metric ? 1 : 0;
    msg[KEY_SETTING_LIGHT_TEXT] = light  ? 1 : 0;
    // Fetch car data after settings ACK so the two messages don't collide
    Pebble.sendAppMessage(msg, function() {
      handleCmd(CMD_REFRESH);
    }, function() {
      handleCmd(CMD_REFRESH);
    });
  } catch(e4) {
    console.log('webviewclosed error: ' + e4);
  }
});
