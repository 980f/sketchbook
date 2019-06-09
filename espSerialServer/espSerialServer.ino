#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char *ssid = "bigbenderAP";
const char *password = "1859";

ESP8266WebServer webserver(80);
const int port = 23;

//WiFiServer telnet(port);


const char *page=
  "<h1>Iolanthe Clock at your service!</h1>"
  "<h3>Telnet</h3>";


char msgbuf[1024];

void handleRoot() {
	sprintf(msgbuf,page);
  webserver.send(200, "text/html", msgbuf);
}

void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.print("Server IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Server MAC address: ");
  Serial.println(WiFi.softAPmacAddress());

  webserver.on("/", handleRoot);
  webserver.begin();

  //start server
  server.begin();
  server.setNoDelay(true);

//  logger->print("Ready! Use 'telnet ");
//  logger->print(WiFi.localIP());
//logger->printf(" %d' to connect\n", port);

  Serial.println("Server listening");
}

void loop() {
  webserver.handleClient();
  
}
