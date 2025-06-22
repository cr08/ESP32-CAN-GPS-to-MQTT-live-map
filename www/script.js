let map;
let carMarker;
let currentLat = null;
let currentLon = null;
let lastUpdate = 0;
let gpsReady = false;

const overlay = document.createElement('div');
overlay.id = 'gps-info-overlay';
overlay.style.position = 'absolute';
overlay.style.top = '10px';
overlay.style.left = '10px';
overlay.style.backgroundColor = 'rgba(0, 0, 0, 0.5)';
overlay.style.color = 'white';
overlay.style.padding = '5px 10px';
overlay.style.fontFamily = 'monospace';
overlay.style.fontSize = '14px';
overlay.style.borderRadius = '5px';
overlay.style.zIndex = '5';
document.body.appendChild(overlay);

const fixIcon = document.createElement('div');
fixIcon.id = 'fix-icon';
fixIcon.style.position = 'absolute';
fixIcon.style.top = '10px';
fixIcon.style.right = '10px';
fixIcon.style.fontSize = '24px';
fixIcon.style.zIndex = '5';
document.body.appendChild(fixIcon);

const loadingScreen = document.createElement('div');
loadingScreen.id = 'loading-screen';
loadingScreen.style.position = 'absolute';
loadingScreen.style.top = '0';
loadingScreen.style.left = '0';
loadingScreen.style.width = '100vw';
loadingScreen.style.height = '100vh';
loadingScreen.style.backgroundColor = '#222';
loadingScreen.style.display = 'flex';
loadingScreen.style.alignItems = 'center';
loadingScreen.style.justifyContent = 'center';
loadingScreen.style.fontSize = '80px';
loadingScreen.style.color = '#0f0';
loadingScreen.style.zIndex = '10';
loadingScreen.textContent = '📡';
document.body.appendChild(loadingScreen);

function getMapTypeFromURL() {
  const params = new URLSearchParams(window.location.search);
  const type = params.get("map");
  return ["roadmap", "satellite", "hybrid", "terrain"].includes(type) ? type : "terrain";
}

function initMap() {
  console.log("initMap() called");

  const mapType = getMapTypeFromURL();

  map = new google.maps.Map(document.getElementById("map"), {
    zoom: 18,
    center: { lat: 39.8283, lng: -98.5795 },
    mapTypeId: mapType,
    disableDefaultUI: true,
  });

  startMqttGpsTracking(map);
}

window.initMap = initMap;

function colorByDop(dop) {
  if (dop <= 1.5) return "#0f0";
  if (dop <= 3.0) return "#ff0";
  return "#f00";
}

function updateOverlay(speed, hdop, vdop, altitude, sats, fix) {
  const hdopColor = colorByDop(hdop);
  const vdopColor = colorByDop(vdop);
  overlay.innerHTML = `
    Speed: ${speed.toFixed(1)} mph |
    <span style="color:${hdopColor}">HDOP: ${hdop}</span> |
    <span style="color:${vdopColor}">VDOP: ${vdop}</span> |
    ALT: ${altitude.toFixed(0)} ft |
    SATS: ${sats} |
    FIX: ${fix}`;
}

function updateFixIcon(actual) {
  fixIcon.textContent = actual === 'true' ? '📡' : '🚗';
}

function startMqttGpsTracking(map) {
  const client = mqtt.connect('wss://mqtt-ws-address/');

  client.on('connect', function () {
    console.log("Connected to MQTT broker");
    client.subscribe('vehicle/gps');
  });

  client.on('message', function (topic, message) {
    try {
      const data = JSON.parse(message.toString());
      console.log("Received GPS data:", data);

      if (!data.lat || !data.lon) {
        console.warn("Invalid GPS data");
        return;
      }

      const lat = parseFloat(data.lat);
      const lon = parseFloat(data.lon);
      const heading = parseFloat(data.heading) || 0;
      const speed = parseFloat(data.speed) || 0;
      const hdop = parseFloat(data.hdop) || 0;
      const vdop = parseFloat(data.vdop) || 0;
      const altitude = parseFloat(data.altitude) || 0;
      const sats = parseInt(data.sats) || 0;
      const fix = data.fix || "N/A";
      const actual = data.actual || "false";

      updateOverlay(speed, hdop, vdop, altitude, sats, fix);
      updateFixIcon(actual);

      const position = new google.maps.LatLng(lat, lon);

      if (!gpsReady) {
        gpsReady = true;
        loadingScreen.style.display = 'none';
      }

      if (!carMarker) {
        carMarker = new google.maps.Marker({
          position: position,
          map: map,
          icon: {
            path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
            scale: 5,
            rotation: heading,
            fillColor: '#00F',
            fillOpacity: 1,
            strokeWeight: 1
          }
        });
        map.panTo(position);
      } else {
        carMarker.setPosition(position);
        carMarker.setIcon({
          path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
          scale: 5,
          rotation: heading,
          fillColor: '#00F',
          fillOpacity: 1,
          strokeWeight: 1
        });
        const now = Date.now();
        if (now - lastUpdate > 5000) {
          map.panTo(position);
          lastUpdate = now;
        }
      }

    } catch (e) {
      console.error("Error processing MQTT message:", e);
    }
  });
}
