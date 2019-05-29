#include "clockserver.h"  //(C) 2019 Andy Heilveil, github/980F

Login known[] = {
  {"Matt's iPhone XS", "sizzlamofo", 9000},
  {"honeypot", "brigadoon-will-be-back-soon", 4500},
  {"Verizon-MiFi7730L-7D50", "5532f44d", 9000},
};

#include "limitedpointer.h"
LimitedPointer<Login> logins(known, countof(known));

//remove this once we are sure that it is stable:
#define WORKSPACE p.rewind()

//////////////////////////////////////////////////////////////////////////////
const char ClockServer::headbody[] =
  "\n<html><head><meta http-equiv='refresh' content='%d'/><title>BigBen</title></head><body>"
  "<h1>Big Bender</h1> ";


//using single letter arg names to allow use of switch()
const char ClockServer::form[] =
  "<form > <input name='h' type='time'>"
  "\n<button name='s' type='submit'> Set Clock </button><br>"
  "\n<button name='z' type='submit'> Zero. </button><br>"
  "\n<span>Steps:</span>"
  "<button name='p' type=submit' value='-100'> -100 </button>"
  "<button name='p' type=submit' value='-10'>-10 </button>"
  "<button name='p' type=submit' value='-1'>-1 </button>"
  "<button name='p' type=submit' value='1'> 1 </button>"
  "<button name='p' type=submit' value='10'> 10 </button>"
  "<button name='p' type=submit' value='100'> 100 </button>"
  "\n<br><input name='u' type='number' value=%d> \n<button type='submit'> Submit</button><br>"
  "\n</form><br>";

const char ClockServer::clockdisplay[] =
  "\n<svg xmlns='http://www.w3.org/2000/svg' id='clock' width='250' height='250'  viewBox='0 0 250 250' >  <title>dial it up</title>"
  "<circle id='face' cx='125' cy='125' r='100' style='fill: white; stroke: black'/>"
  "<g id='ticks' transform='translate(125,125)'>"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(30)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(60)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(90)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(120)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(150)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(180)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(210)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(240)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(270)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(300)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(330)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(360)' />"
  "</g>"

  "<g id='hands' style='stroke: black;  stroke-width: 5px; stroke-linecap: round;'>"
  "<path id='hour'   d='M125,125 L125,75' transform='rotate(%3d, 125, 125)'/>"
  "<path id='minute' d='M125,125 L125,45' transform='rotate(%3d, 125, 125)'/>"
  "<path id='second' d='M125,125 L125,30' transform='rotate(%3d, 125, 125)' style='stroke: red; stroke-width: 2px' />"
  "</g>"

  "<text x='5' y='25' fill='blue'>%02d:%02d:%02d</text>"
  "\n<circle id='knob' r='6' cx='125' cy='125' style='fill: #333;'/>"
  "</svg>";



const char ClockServer::enddocument[] = "\n</body></html>";

// = 1859, const char * const name = "bigbender"):
ClockServer::ClockServer(unsigned port , const char * const name , ChainPrinter &dbg):
  dbg(dbg),
  ourname(name),
  server(port),
  pollStatus(500),
  bigben(0),
  desired(0),
  p(workspace, sizeof(workspace))


{
  //done.
}

void ClockServer::login() {
  dnserver = &logins.next();
  dbg("\nTrying ", dnserver->ssid, ' ', dnserver->password);
  WiFi.begin(dnserver->ssid, dnserver->password);
  timedOut = dnserver->timeout;
}

/////////////////////////////////////////////////////////
void ClockServer::begin() {
  WiFi.mode(WIFI_STA);
  login();
  since.start();
}

void ClockServer::onTick(void) {
  if (connected) {
    server.handleClient();
    //todo: confirm we don't need something like this    MDNS.update();
  } else {
    if (pollStatus.perCycle()) {
      if (WiFi.status() != WL_CONNECTED) {
        dbg(".");
        if (timedOut) {
          login();//start over.
        }
      } else {
        connected = true;
        timestamp("Connected after ");
        onConnection();
      }
    }
  }
}

void ClockServer::elapsed() {
  HMS c(millis());
  WORKSPACE;
  p.printf(headbody, refreshRate); //refresh rate
  showclock(c);
  p.printf(enddocument);
  server.send(200, "text/html", p.buffer);
}


void ClockServer::diagRequest() {
  WORKSPACE;
  p.printf("\n%s %s ", server.uri().c_str(), (server.method() == HTTP_GET) ? "GET" : "POST");
  for (unsigned i = server.args(); i-- > 0;) {
    p.printf( "\n\t%s:%s ", server.argName(i).c_str() , server.arg(i).c_str() );
  }
  dbg(p.buffer);//for now track web activity
}

void ClockServer::showRequest() {
  diagRequest();
  dbg("Most used: ", Sprinter::mostused);
  server.send(404, "text/plain", p.buffer);
}

void ClockServer::ackIgnore() {
  server.send(410, "text/plain", "Sorry Dave, I can't do that");
}

void ClockServer::slash() {
  timestamp("Begin main page");
  bool dumpem = server.args() == 0;
  for (unsigned i = server.args(); i-- > 0;) {
    const char *value = server.arg(i).c_str(); //you MUST not persist this outside of the function call
    char key = server.argName(i)[0];//single letter names

    switch (key) {
      case 'h': //set time of remote from hh:mm (24H)
        desired.setTimeFrom(value);
        dbg("\nDesired time recorded locally");
        break;
      case 's'://publish time to clock
        updateDesired = true; //not checking if it actually changed, so that we may refresh the remote.
        dbg('!', desired.hour, ':' , desired.minute);
        break;
      case 'z'://tell clock that it is at midnight/noon
        dbg("!0");
        break;
      case 'p'://pulse the stepper
        dbg('!', value);
        break;
      case 'u':
        refreshRate = atoi(value);
        dbg("setting page refresh rate to ", refreshRate);
        break;
      default:
        dumpem = true;
        break;
    }
  }
  if (dumpem) {
    diagRequest();
    dbg(p.buffer);
  }
  HMS c(millis());//will be clock's last report
  WORKSPACE;
  p.printf(headbody, refreshRate);
  p.printf(form, refreshRate);
  showclock(c);
  p.printf(enddocument);
  server.send(200, "text/html", p.buffer);
  timestamp("End main page");
}


void ClockServer::onConnection() {
  dbg("\nConnected to ", dnserver->ssid);
  dbg("\nIP address: ", WiFi.localIP());

  if (MDNS.begin(ourname)) {
    dbg("\nmDNS working as ", ourname);
  } else {
    dbg("\nmDNS failed for ", ourname);
  }

  server.on("/", [this]() {
    slash();
  });
  server.on("/uptime", [this]() {
    elapsed();
  });

  server.on("/favicon.ico", [this]() {//browser insists on asking us for this, ignore it.
    ackIgnore();
  });

  server.onNotFound([this]() {
    showRequest();
  });

  server.begin();
  dbg("\nBig Ben is at your service.\n");
}

void ClockServer::showclock(const HMS &ck) {
  p.printf(clockdisplay, ck.hour * 30, ck.minute * 6, ck.sec * 6, ck.hour , ck.minute, ck.sec ); //6 degrees per second and minute, 360/60, 360/12 = 30 for hour
}
