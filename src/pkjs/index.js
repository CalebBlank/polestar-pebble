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

// ── Polestar API (PKCE auth + GraphQL) ───────────────────────────────────────
var AUTH_BASE     = 'https://polestarid.eu.polestar.com';
var AUTH_ENDPOINT = AUTH_BASE + '/as/authorization.oauth2';
var TOKEN_ENDPOINT= AUTH_BASE + '/as/token.oauth2';
var CLIENT_ID     = 'l3oopkc_10';
var REDIRECT_URI  = 'https://www.polestar.com/sign-in-callback';
var API_GRAPHQL   = 'https://pc-api.polestar.com/eu-north-1/mystar-v2/';
var API_COMMANDS  = 'https://pc-api.polestar.com/eu-north-1/mystar-v2/';

var s_token = null;
var s_vin = null;
var s_username = null;
var s_password = null;
var s_car_lat = 0;
var s_car_lng = 0;

// ── PKCE / crypto helpers ─────────────────────────────────────────────────────
function randomBytes(n) {
  var bytes = [];
  for (var i = 0; i < n; i++) bytes.push(Math.floor(Math.random() * 256));
  return bytes;
}

function base64url(bytes) {
  var C = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var out = '', i = 0, n;
  for (; i + 2 < bytes.length; i += 3) {
    n = (bytes[i] << 16) | (bytes[i+1] << 8) | bytes[i+2];
    out += C[(n>>18)&63] + C[(n>>12)&63] + C[(n>>6)&63] + C[n&63];
  }
  if (i + 1 === bytes.length) {
    n = bytes[i] << 16;
    out += C[(n>>18)&63] + C[(n>>12)&63] + '==';
  } else if (i + 2 === bytes.length) {
    n = (bytes[i] << 16) | (bytes[i+1] << 8);
    out += C[(n>>18)&63] + C[(n>>12)&63] + C[(n>>6)&63] + '=';
  }
  return out.replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '');
}

// Compact SHA-256 returning byte array
function sha256(str) {
  var K = [
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  ];
  var msg = [];
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c < 0x80) { msg.push(c); }
    else if (c < 0x800) { msg.push(0xc0|(c>>6)); msg.push(0x80|(c&0x3f)); }
    else { msg.push(0xe0|(c>>12)); msg.push(0x80|((c>>6)&0x3f)); msg.push(0x80|(c&0x3f)); }
  }
  var L = msg.length * 8;
  msg.push(0x80);
  while ((msg.length % 64) !== 56) msg.push(0);
  msg.push(0,0,0,0, (L>>>24)&0xff, (L>>>16)&0xff, (L>>>8)&0xff, L&0xff);
  var H = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
  function ror(x,n){ return (x>>>n)|(x<<(32-n)); }
  function add(a,b){ return (a+b)|0; }
  for (var blk = 0; blk < msg.length; blk += 64) {
    var W = [];
    for (var t = 0; t < 16; t++)
      W[t] = (msg[blk+t*4]<<24)|(msg[blk+t*4+1]<<16)|(msg[blk+t*4+2]<<8)|msg[blk+t*4+3];
    for (var t = 16; t < 64; t++) {
      var s0 = ror(W[t-15],7)^ror(W[t-15],18)^(W[t-15]>>>3);
      var s1 = ror(W[t-2],17)^ror(W[t-2],19)^(W[t-2]>>>10);
      W[t] = add(add(add(W[t-16],s0),W[t-7]),s1);
    }
    var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
    for (var t = 0; t < 64; t++) {
      var S1 = ror(e,6)^ror(e,11)^ror(e,25);
      var ch = (e&f)^(~e&g);
      var t1 = add(add(add(add(h,S1),ch),K[t]),W[t]);
      var S0 = ror(a,2)^ror(a,13)^ror(a,22);
      var maj= (a&b)^(a&c)^(b&c);
      var t2 = add(S0,maj);
      h=g; g=f; f=e; e=add(d,t1); d=c; c=b; b=a; a=add(t1,t2);
    }
    H[0]=add(H[0],a); H[1]=add(H[1],b); H[2]=add(H[2],c); H[3]=add(H[3],d);
    H[4]=add(H[4],e); H[5]=add(H[5],f); H[6]=add(H[6],g); H[7]=add(H[7],h);
  }
  var out = [];
  for (var i = 0; i < 8; i++) {
    out.push((H[i]>>>24)&0xff, (H[i]>>>16)&0xff, (H[i]>>>8)&0xff, H[i]&0xff);
  }
  return out;
}

// ── Credentials ───────────────────────────────────────────────────────────────
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

// ── PKCE Authentication ───────────────────────────────────────────────────────
function authenticate(callback) {
  if (!s_username || !s_password) { callback(new Error('No credentials')); return; }

  var verifier   = base64url(randomBytes(32));
  var challenge  = base64url(sha256(verifier));
  var state      = base64url(randomBytes(8));

  // Step 1: GET auth endpoint → PingFederate login page
  var authUrl = AUTH_ENDPOINT
    + '?response_type=code'
    + '&client_id=' + CLIENT_ID
    + '&redirect_uri=' + encodeURIComponent(REDIRECT_URI)
    + '&scope=' + encodeURIComponent('openid profile email')
    + '&state=' + state
    + '&code_challenge=' + challenge
    + '&code_challenge_method=S256';

  var xhr1 = new XMLHttpRequest();
  xhr1.open('GET', authUrl, true);
  xhr1.onload = function() {
    var html = xhr1.responseText;
    // Extract PingFederate resume/form action path
    var match = html.match(/(?:url|action):\s*"([^"]+)"/);
    if (!match) { callback(new Error('Auth: no resume path')); return; }
    var resumePath = match[1];
    if (resumePath.charAt(0) === '/') resumePath = AUTH_BASE + resumePath;

    // Step 2: POST credentials
    var body = 'pf.username=' + encodeURIComponent(s_username)
             + '&pf.pass='    + encodeURIComponent(s_password)
             + '&client_id='  + CLIENT_ID;
    var xhr2 = new XMLHttpRequest();
    xhr2.open('POST', resumePath, true);
    xhr2.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr2.onload = function() {
      // After redirects, responseURL ends up at REDIRECT_URI?code=...
      var finalUrl = xhr2.responseURL || '';
      var codeMatch = finalUrl.match(/[?&]code=([^&#]+)/);
      if (!codeMatch) { callback(new Error('Auth: no code in redirect')); return; }
      var code = codeMatch[1];

      // Step 3: Exchange code for token
      var tokenBody = 'grant_type=authorization_code'
        + '&code='          + encodeURIComponent(code)
        + '&code_verifier=' + verifier
        + '&client_id='     + CLIENT_ID
        + '&redirect_uri='  + encodeURIComponent(REDIRECT_URI);
      var xhr3 = new XMLHttpRequest();
      xhr3.open('POST', TOKEN_ENDPOINT, true);
      xhr3.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
      xhr3.onload = function() {
        if (xhr3.status === 200) {
          try {
            var data = JSON.parse(xhr3.responseText);
            s_token = data.access_token;
            callback(null);
          } catch (e) { callback(new Error('Token parse error')); }
        } else {
          callback(new Error('Token error: ' + xhr3.status));
        }
      };
      xhr3.onerror = function() { callback(new Error('Network error (token)')); };
      xhr3.send(tokenBody);
    };
    xhr2.onerror = function() { callback(new Error('Network error (creds)')); };
    xhr2.send(body);
  };
  xhr1.onerror = function() { callback(new Error('Network error (auth)')); };
  xhr1.send();
}

// ── GraphQL helper ────────────────────────────────────────────────────────────
function graphqlQuery(queryStr, variables, callback) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', API_GRAPHQL, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + s_token);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function() {
    if (xhr.status === 200) {
      try { callback(null, JSON.parse(xhr.responseText)); }
      catch (e) { callback(new Error('JSON parse error')); }
    } else { callback(new Error('GraphQL error: ' + xhr.status)); }
  };
  xhr.onerror = function() { callback(new Error('Network error (graphql)')); };
  xhr.send(JSON.stringify({query: queryStr, variables: variables || {}}));
}

function apiPost(path, body, callback) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', API_COMMANDS + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + s_token);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function() {
    if (xhr.status === 200 || xhr.status === 204) { callback(null); }
    else { callback(new Error('API error: ' + xhr.status)); }
  };
  xhr.onerror = function() { callback(new Error('Network error')); };
  xhr.send(body ? JSON.stringify(body) : null);
}

// ── GraphQL queries ───────────────────────────────────────────────────────────
var Q_CARS = 'query getCars { getConsumerCarsV2 { vin internalVehicleIdentifier } }';
var Q_TELEM = 'query CarTelematicsV2($vins:[String!]!) { carTelematicsV2(vins:$vins) {'
  + ' battery { batteryChargeLevelPercentage chargingStatus'
  + ' estimatedChargingTimeToFullMinutes estimatedDistanceToEmptyKm }'
  + ' odometer { odometerMeters } } }';

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
    } else { callback('Location unavailable'); }
  };
  xhr.onerror = function() { callback('Location unavailable'); };
  xhr.send();
}

// ── Distance ──────────────────────────────────────────────────────────────────
function haversineM(lat1, lon1, lat2, lon2) {
  var R = 6371000;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat/2)*Math.sin(dLat/2)
        + Math.cos(lat1*Math.PI/180)*Math.cos(lat2*Math.PI/180)
        * Math.sin(dLon/2)*Math.sin(dLon/2);
  return Math.round(R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a)));
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
    graphqlQuery(Q_CARS, null, function(err, carsData) {
      if (err) { sendError(); return; }
      var cars = (carsData && carsData.data && carsData.data.getConsumerCarsV2) || [];
      if (cars.length === 0) { sendError(); return; }
      s_vin = cars[0].vin;
      graphqlQuery(Q_TELEM, {vins: [s_vin]}, function(err2, telemData) {
        if (err2) { sendError(); return; }
        var telem   = (telemData && telemData.data && telemData.data.carTelematicsV2) || {};
        var battArr = telem.battery  || [];
        var odoArr  = telem.odometer || [];
        var batt = battArr[0] || {};
        var odo  = odoArr[0]  || {};

        var is_charging = batt.chargingStatus === 'CHARGING_STATUS_CHARGING';
        var charge_min  = is_charging ? (batt.estimatedChargingTimeToFullMinutes || -1) : -1;
        var charge_pct  = Math.round(batt.batteryChargeLevelPercentage || 0);
        var range_km    = Math.round(batt.estimatedDistanceToEmptyKm   || 0);
        var odo_km      = Math.round((odo.odometerMeters || 0) / 1000);

        var msg = {};
        msg[KEY_STATE_IS_CHARGING]  = is_charging ? 1 : 0;
        msg[KEY_STATE_CHARGE_MIN]   = charge_min;
        msg[KEY_STATE_CHARGE_PCT]   = charge_pct;
        msg[KEY_STATE_RANGE_KM]     = range_km;
        msg[KEY_STATE_ODO_KM]       = odo_km;
        msg[KEY_STATE_LOCATION]     = 'Location unavailable';
        Pebble.sendAppMessage(msg, function() {}, function() {});

        if (is_charging && charge_min > 0) {
          pushChargePin(charge_min, charge_pct);
        } else {
          deleteChargePin();
        }
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
      Pebble.openURL('geo:' + s_car_lat + ',' + s_car_lng + '?q=' + s_car_lat + ',' + s_car_lng);
    }
    return;
  }

  if (cmd === CMD_REFRESH) {
    getStoredCreds();
    var debugMock = localStorage.getItem('debug_mock') === 'true';
    if (debugMock || !s_username || !s_password) { sendMockData(); return; }
    fetchAndSend();
    return;
  }

  getStoredCreds();
  authenticate(function(authErr) {
    if (authErr) { sendError(); return; }
    if (!s_vin) { sendError(); return; }
    if (cmd === CMD_TOGGLE_LOCK) {
      apiPost('vehicles/' + s_vin + '/lock', null, function(err) {
        if (!err) fetchAndSend();
      });
    } else if (cmd === CMD_TOGGLE_CLIMATE) {
      apiPost('vehicles/' + s_vin + '/climate/toggle', null, function(err) {
        if (!err) fetchAndSend();
      });
    } else if (cmd === CMD_HONK) {
      apiPost('vehicles/' + s_vin + '/honk-blink', null, function() {});
    }
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
      reminders: [{
        time: new Date(done.getTime() - 10 * 60 * 1000).toISOString(),
        layout: { type: 'genericReminder', title: 'Polestar charges in 10 min' }
      }]
    };
    var xhr = new XMLHttpRequest();
    xhr.open('PUT', TIMELINE_API + CHARGE_PIN_ID, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.setRequestHeader('X-User-Token', token);
    xhr.onload = function() { console.log('Timeline pin: ' + xhr.status); };
    xhr.send(JSON.stringify(pin));
  }, function(err) { console.log('Timeline token error: ' + err); });
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
    var debugMock = localStorage.getItem('debug_mock') === 'true';
    Pebble.sendAppMessage(settingsMsg, function() {
      if (debugMock || !s_username || !s_password) { sendMockData(); return; }
      fetchAndSend();
    }, function() {
      if (debugMock || !s_username || !s_password) { sendMockData(); return; }
      fetchAndSend();
    });
  }, 500);
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;
  if (payload[KEY_CMD] !== undefined) handleCmd(payload[KEY_CMD]);
});

// ── Clay configuration ────────────────────────────────────────────────────────
function buildClayConfig() {
  var creds = {};
  try { creds = JSON.parse(localStorage.getItem('polestar_creds') || '{}'); } catch(e2) {}
  var metric     = localStorage.getItem('use_metric') !== 'false';
  var light      = localStorage.getItem('light_text') === 'true';
  var debug_mock = localStorage.getItem('debug_mock') === 'true';
  return [
    { 'type': 'heading', 'defaultValue': 'Polestar' },
    { 'type': 'section', 'items': [
      { 'type': 'heading', 'defaultValue': 'Account' },
      { 'type': 'input', 'id': 'email', 'messageKey': 'LOCAL_EMAIL',
        'label': 'Polestar email', 'defaultValue': creds.username || '',
        'description': creds.username ? 'Saved: ' + creds.username : 'Not saved',
        'attributes': { 'type': 'email', 'placeholder': 'you@example.com' } },
      { 'type': 'input', 'id': 'password', 'messageKey': 'LOCAL_PASSWORD',
        'label': 'Password', 'defaultValue': creds.password || '',
        'description': creds.password ? 'Password saved' : 'Not saved',
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
        'label': 'Black text', 'defaultValue': light }
    ]},
    { 'type': 'section', 'items': [
      { 'type': 'heading', 'defaultValue': 'Debug' },
      { 'type': 'toggle', 'id': 'debug_mock', 'messageKey': 'LOCAL_DEBUG_MOCK',
        'label': 'Use placeholder data', 'defaultValue': debug_mock }
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
    var settings = clay.getSettings(e.response, false);

    var email    = settings.LOCAL_EMAIL    ? String(settings.LOCAL_EMAIL.value    || '').trim() : '';
    var password = settings.LOCAL_PASSWORD ? String(settings.LOCAL_PASSWORD.value || '')         : '';
    if (email || password) {
      var existing = {};
      try { existing = JSON.parse(localStorage.getItem('polestar_creds') || '{}'); } catch(e3) {}
      if (email)    existing.username = email;
      if (password) existing.password = password;
      localStorage.setItem('polestar_creds', JSON.stringify(existing));
      getStoredCreds();
    }

    var metric        = settings.SETTING_UNITS      ? !!settings.SETTING_UNITS.value      : true;
    var light         = settings.SETTING_LIGHT_TEXT ? !!settings.SETTING_LIGHT_TEXT.value : false;
    var debug_mock    = settings.LOCAL_DEBUG_MOCK    ? !!settings.LOCAL_DEBUG_MOCK.value    : false;
    var prevDebugMock = localStorage.getItem('debug_mock') === 'true';
    localStorage.setItem('use_metric',  metric     ? 'true' : 'false');
    localStorage.setItem('light_text',  light      ? 'true' : 'false');
    localStorage.setItem('debug_mock',  debug_mock ? 'true' : 'false');

    var msg = {};
    msg[KEY_SETTING_UNITS]      = metric ? 1 : 0;
    msg[KEY_SETTING_LIGHT_TEXT] = light  ? 1 : 0;

    var credentialsChanged = !!(email || password);
    var debugChanged       = debug_mock !== prevDebugMock;
    var shouldRefresh      = credentialsChanged || debugChanged;
    Pebble.sendAppMessage(msg, function() {
      if (shouldRefresh) handleCmd(CMD_REFRESH);
    }, function() {
      if (shouldRefresh) handleCmd(CMD_REFRESH);
    });
  } catch(e4) {
    console.log('webviewclosed error: ' + e4);
  }
});
