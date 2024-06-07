#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// WiFi credentials
const char* ssid = "ssid";
const char* password = "password";

// HTTP authentication credentials
const char* http_username = "username";
const char* http_password = "password";

// Pre-set key for additional security
const String preSetKey = "key";  // Replace with your key

// Pin definitions
const int magnetSensorPin = D1;  // Digital pin for magnet sensor
const int relayPin = D2;         // Digital pin for relay module

// Cooldown period in milliseconds
const unsigned long cooldownPeriod = 1000; // 1 second

// Variables to track button state
unsigned long lastToggleTime = 0;
bool isButtonDisabled = false;

// Session management
struct UserSession {
    String id;
    unsigned long expiryTime;
};
const int MAX_SESSIONS = 10;
UserSession sessions[MAX_SESSIONS];
const unsigned long sessionDuration = 7 * 24 * 60 * 60 * 1000; // session valid for 7 days

// Text variables for easy editing
//Boot
String textConnectedTo = "Connected to ";
String textIPAddress = "IP address: ";
String textHTTPServerStarted = "HTTP server started";
//Website
String textRootTitle = "Garagentor Steuerung";
String textToggleButton = "Drück mich!";
String textDoorIs = "Tor ist: ";
String textClosed = "Geschlossen";
String textOpen = "Offen";
String textButtonDisabled = "Knopf wieder aktiviert in: ";
String textSeconds = " sekunden";
String textLoginTitle = "Login";
String textLoginHeading = "Login";
String textUsernamePlaceholder = "Benutzername";
String textPasswordPlaceholder = "Passwort";
String textLoginButton = "Login";
String textWrongCredentials = "Falsche Logindaten! Versuche es erneut.";
//Http responses
String textSessionExpired = "Session Expired";
String textCurrentlyDisabled = "Currently disabled";
String textToggled = "Toggled";

// Create an instance of the server
ESP8266WebServer server(80);

// Function prototypes
void handleRoot();
void handleToggle();
void handleStatus();
bool is_authenticated();
void handleLogin();
String generateToken();
void extendSession(const String& sessionId);
int findSession(const String& sessionId);
void addSession(const String& sessionId, unsigned long expiryTime);
void removeSession(const String& sessionId);

void setup() {
    Serial.begin(9600);

    // Connect to WiFi
    WiFi.begin(ssid, password);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print(textConnectedTo);
    Serial.println(ssid);
    Serial.print(textIPAddress);
    Serial.println(WiFi.localIP());

    // Initialize the server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/login", handleLogin);
    server.on("/toggle", HTTP_POST, handleToggle);
    server.on("/status", HTTP_GET, handleStatus);
    //here the list of headers to be recorded
    const char * headerkeys[] = {"Cookie","Pre-Set-Key"} ;
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    //ask server to track these headers
    server.collectHeaders(headerkeys, headerkeyssize );
    server.begin();
    Serial.println(textHTTPServerStarted);

    // Initialize the magnet sensor pin
    pinMode(magnetSensorPin, INPUT_PULLUP);

    // Initialize the relay pin
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);  // Ensure relay is off initially
}

void loop() {
    server.handleClient();
}

// Handle the root URL
void handleRoot() {
    String header;
    if (!is_authenticated()) {
        server.sendHeader("Location", "/login");
        server.sendHeader("Cache-Control", "no-cache");
        server.send(301);
        return;
    }

    String html = "<!DOCTYPE html>\
<html lang=\"en\">\
<head>\
  <meta charset=\"UTF-8\">\
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\
  <title>" + textRootTitle + "</title>\
  <style>\
    body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }\
    h1 { color: #333; }\
    button { padding: 15px; font-size: 18px; border: none; border-radius: 5px; cursor: pointer; }\
    .open { background-color: #f44336; color: white; }\
    .closed { background-color: #4CAF50; color: white; }\
    #timer { font-size: 16px; margin-top: 10px; }\
    @media screen and (max-width: 600px) {\
      button { width: 100%; }\
    }\
  </style>\
</head>\
<body>\
  <h1>" + textRootTitle + "</h1>\
  <button id=\"toggleButton\" onclick=\"toggleDoor()\">" + textToggleButton + "</button>\
  <p id=\"status\"></p>\
  <p id=\"timer\"></p>\
  <script>\
    const textDoorIs = '" + textDoorIs + "';\
    const textClosed = '" + textClosed + "';\
    const textOpen = '" + textOpen + "';\
    const textButtonDisabled = '" + textButtonDisabled + "';\
    const textSeconds = '" + textSeconds + "';\
    const cooldownPeriod = " + String(cooldownPeriod / 1000) + ";\
    let buttonDisabled = false;\
    let remainingTime = 0;\
    \
    function toggleDoor() {\
      fetch('/toggle', { method: 'POST' })\
        .then(response => response.text())\
        .then(result => {\
          if (result.includes('Toggled')) {\
            buttonDisabled = true;\
            remainingTime = cooldownPeriod;\
            updateButtonState();\
          }\
        });\
    }\
    \
    function updateStatus() {\
      fetch('/status')\
        .then(response => response.json())\
        .then(data => {\
          const statusText = textDoorIs + (data.doorStatus === 'Geschlossen' ? textClosed : textOpen);\
          document.getElementById('status').innerText = statusText;\
          const button = document.getElementById('toggleButton');\
          if (data.doorStatus === 'Geschlossen') {\
            button.className = 'closed';\
          } else {\
            button.className = 'open';\
          }\
          if (data.isButtonDisabled) {\
            buttonDisabled = true;\
            remainingTime = data.remainingTime;\
            updateButtonState();\
          } else {\
            buttonDisabled = false;\
            document.getElementById('timer').innerText = '';\
            button.disabled = false;\
          }\
        });\
    }\
    \
    function updateButtonState() {\
      const button = document.getElementById('toggleButton');\
      const timer = document.getElementById('timer');\
      button.disabled = buttonDisabled;\
      if (buttonDisabled) {\
        timer.innerText = textButtonDisabled + remainingTime + textSeconds;\
        const interval = setInterval(() => {\
          remainingTime--;\
          if (remainingTime > 0) {\
            timer.innerText = textButtonDisabled + remainingTime + textSeconds;\
          } else {\
            clearInterval(interval);\
            button.disabled = false;\
            timer.innerText = '';\
            buttonDisabled = false;\
          }\
        }, 1000);\
      } else {\
        timer.innerText = '';\
      }\
    }\
    \
    setInterval(updateStatus, 2000);\
    window.onload = updateStatus;\
  </script>\
</body>\
</html>";
    server.send(200, "text/html", html);
}

void handleLogin() {
    String msg;
    if (server.hasHeader("Cookie")) {
        String cookie = server.header("Cookie");
    }
    if (server.hasArg("DISCONNECT")) {
        Serial.println("Disconnection");
        server.sendHeader("Location", "/login");
        server.sendHeader("Cache-Control", "no-cache");
        server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
        server.send(301);
        return;
    }


    if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")) {
        if (server.arg("USERNAME") == http_username && server.arg("PASSWORD") == http_password) {
            String newSessionID = generateToken();
            unsigned long expiryTime = millis() + sessionDuration;
            addSession(newSessionID, expiryTime);

            server.sendHeader("Location", "/");
            server.sendHeader("Cache-Control", "no-cache");
            server.sendHeader("Set-Cookie", "ESPSESSIONID=" + newSessionID + "; Max-Age=" + String(sessionDuration / 1000));
            server.send(301);
            return;
        }
        msg = textWrongCredentials;
    }
    String content = "<!DOCTYPE html>\
<html lang='en'>\
<head>\
  <meta charset='UTF-8'>\
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
  <title>" + textLoginTitle + "</title>\
  <style>\
    body { font-family: Arial, sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; background-color: #f7f7f7; }\
    .container { max-width: 400px; width: 100%; padding: 20px; background-color: white; box-shadow: 0 0 10px rgba(0, 0, 0, 0.1); border-radius: 8px; }\
    h2 { text-align: center; color: #333; }\
    form { display: flex; flex-direction: column; }\
    input[type='text'], input[type='password'] { padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; }\
    input[type='submit'] { padding: 10px; background-color: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }\
    input[type='submit']:hover { background-color: #45a049; }\
    .message { color: red; text-align: center; }\
  </style>\
</head>\
<body>\
  <div class='container'>\
    <h2>" + textLoginHeading + "</h2>\
    <form action='/login' method='POST'>\
      <input type='text' name='USERNAME' placeholder='" + textUsernamePlaceholder + "' required>\
      <input type='password' name='PASSWORD' placeholder='" + textPasswordPlaceholder + "' required>\
      <input type='submit' value='" + textLoginButton + "'>\
    </form>\
    <p class='message'>" + msg + "</p>\
  </div>\
</body>\
</html>";
    server.send(200, "text/html", content);
}

String generateToken() {
    String token = "";
    for (int i = 0; i < 32; i++) {
        char randomChar = (char)random(48, 122);
        if (isalnum(randomChar)) {
            token += randomChar;
        } else {
            i--;
        }
    }
    return token;
}

void extendSession(const String& sessionId) {
    int sessionIndex = findSession(sessionId);
    if (sessionIndex != -1) {
        sessions[sessionIndex].expiryTime = millis() + sessionDuration;
        server.sendHeader("Set-Cookie", "ESPSESSIONID=" + sessionId + "; Max-Age=" + String(sessionDuration / 1000));
    }
}

// Handle the toggle URL
void handleToggle() {
    unsigned long currentTime = millis();
    if (is_authenticated() || (server.hasHeader("Pre-Set-Key") && server.header("Pre-Set-Key") == preSetKey)) {
        if (!isButtonDisabled) {
            // Activate the relay for a brief period to simulate a button press
            digitalWrite(relayPin, HIGH);
            
            delay(500); // Adjust this delay if needed
            digitalWrite(relayPin, LOW);

            // Update the last toggle time and disable the button
            lastToggleTime = currentTime;
            isButtonDisabled = true;

            server.send(200, "text/plain", textToggled);
        } else {
            server.send(200, "text/plain", textCurrentlyDisabled);
        }
    } else {
        server.send(401, "text/plain", textSessionExpired);
    }
}
// Handle the status URL
void handleStatus() {
    unsigned long currentTime = millis();
    if (is_authenticated() || (server.hasHeader("Pre-Set-Key") && server.header("Pre-Set-Key") == preSetKey)) {
        if (isButtonDisabled && (currentTime - lastToggleTime >= cooldownPeriod)) {
            isButtonDisabled = false;
        }
        bool isClosed = digitalRead(magnetSensorPin) == LOW;
        String doorStatus = isClosed ? textClosed : textOpen;
        unsigned long remainingTime = isButtonDisabled ? (cooldownPeriod - (currentTime - lastToggleTime)) / 1000 : 0;

        // Calculate uptime in seconds
        unsigned long uptimeSeconds = currentTime / 1000;
        unsigned long seconds = uptimeSeconds % 60;
        unsigned long minutes = (uptimeSeconds / 60) % 60;

        unsigned long hours = (uptimeSeconds / 3600) % 24;
        unsigned long days = uptimeSeconds / 86400;

        // Create uptime string
        String uptime = String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";

        // Manually create a JSON response string
        String jsonResponse = "{\"doorStatus\":\"" + doorStatus + "\",";
        jsonResponse += "\"isButtonDisabled\":" + String(isButtonDisabled ? "true" : "false") + ",";
        jsonResponse += "\"remainingTime\":" + String(remainingTime) + ",";
        jsonResponse += "\"uptime\":\"" + uptime + "\"}";

        server.send(200, "application/json", jsonResponse);
    } else {
        server.send(401, "text/plain", textSessionExpired);
    }
}

bool is_authenticated(){
    if (server.hasHeader("Cookie")) {
        String cookie = server.header("Cookie");
        int index = cookie.indexOf("ESPSESSIONID=");
        if (index != -1) {
            int startIndex = index + String("ESPSESSIONID=").length();
            int endIndex = cookie.indexOf(";", startIndex);
            if (endIndex == -1) {
                endIndex = cookie.length();
            }
            String sessionId = cookie.substring(startIndex, endIndex);
            int sessionIndex = findSession(sessionId);
            if (sessionIndex != -1 && millis() < sessions[sessionIndex].expiryTime) {
                extendSession(sessionId);
                return true;
            }
        }
    }
    return false;
}

int findSession(const String& sessionId) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].id == sessionId) {
            return i;
        }
    }
    return -1;
}

void addSession(const String& sessionId, unsigned long expiryTime) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].id == "") {  // Empty slot
            sessions[i].id = sessionId;
            sessions[i].expiryTime = expiryTime;
            return;
        }
    }
    // If no empty slot is found, overwrite the oldest session
    unsigned long oldestTime = sessions[0].expiryTime;
    int oldestIndex = 0;
    for (int i = 1; i < MAX_SESSIONS; i++) {
        if (sessions[i].expiryTime < oldestTime) {
            oldestTime = sessions[i].expiryTime;
            oldestIndex = i;
        }
    }
    sessions[oldestIndex].id = sessionId;
    sessions[oldestIndex].expiryTime = expiryTime;
}

void removeSession(const String& sessionId) {
    int sessionIndex = findSession(sessionId);
    if (sessionIndex != -1) {
        sessions[sessionIndex].id = "";
        sessions[sessionIndex].expiryTime = 0;
    }
}
