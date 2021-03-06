//#define SERIAL_DEDUG
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
//#include <EEPROM.h>
#include "DateTime.h"
#include "Terminal.h"
#include "ScaleMem.h"
#include "tools.h"
#include "BrowserServer.h"
#include "Scales.h"
#include "Task.h"

/*
   This example serves a "hello world" on a WLAN and a SoftAP at the same time.
   The SoftAP allow you to configure WLAN parameters at run time. They are not setup in the sketch but saved on EEPROM.

   Connect your computer or cell phone to wifi network ESP_ap with password 12345678. A popup may appear and it allow you to go to WLAN config. If it does not then navigate to http://192.168.4.1/wifi and config it there.
   Then wait for the module to connect to your wifi and take note of the WLAN IP it got. Then you can disconnect from ESP_ap and return to your regular WLAN.

   Now the ESP8266 is in your network. You can reach it through http://192.168.x.x/ (the IP you took note of) or maybe at http://esp8266.local too.

   This is a captive portal because through the softAP it will redirect any http request to http://192.168.4.1/
*/

TaskController taskController = TaskController();		/*  */
Task taskBlink(takeBlink, 500);							/*  */
Task taskBattery(takeBattery, 20000);					/* 20 Обновляем заряд батареи */
Task taskPower(powerOff, 600000);						/* 10 минут бездействия и выключаем */


unsigned int COUNT_FLASH = 500;
unsigned int COUNT_BLINK = 500;

/** Last time I tried to connect to WLAN */
long lastConnectTry = 0;

/** Current WLAN status */
int status = WL_IDLE_STATUS;

void setup() {	
	pinMode(EN_NCP, OUTPUT);
	digitalWrite(EN_NCP, HIGH);
	pinMode(LED, OUTPUT);	
	//digitalWrite(LED, HIGH);
	pinMode(PWR_SW, INPUT);

	while (digitalRead(PWR_SW) == HIGH){
		delay(100);	
	};
	
	attachInterrupt(digitalPinToInterrupt(PWR_SW), powerSwitchInterrupt, RISING);
	
	taskPower.setInterval(600000);
	taskPower.onRun(powerOff);
  
	taskController.add(&taskBlink);
	taskController.add(&taskBattery);
	taskController.add(&taskPower);

	#if defined SERIAL_DEDUG
	  Serial.begin(9600);
	  Serial.println();
	  Serial.print("Configuring access point...");
	#endif
	delay(1000);
	SPIFFS.begin(); 

	SCALES.begin();

  
	WiFi.hostname(myHostname);
	WiFi.softAPConfig(apIP, apIP, netMsk);
	WiFi.softAP(softAP_ssid, softAP_password);
	delay(500); 
	#if defined SERIAL_DEDUG
	  Serial.print("AP IP address: ");
	  Serial.println(WiFi.softAPIP());
	#endif
	//ESP.eraseConfig();
	browserServer.begin();

	connect = SCALES.getSSID().length() > 0; 
	Rtc.Begin();
	SCALES.saveEvent("power", "ON");
}

/*********************************/
/* */
/*********************************/
void takeBlink() {
	bool led = !digitalRead(LED);
	digitalWrite(LED, led);	
	taskBlink.setInterval(led ? COUNT_BLINK : COUNT_FLASH/SCALES.getFilter());
}

/**/
void takeBattery(){
	//unsigned int charge = ((SCALES.getBattery(1) - MIN_CHG)/DIAPAZONE)*100;
	unsigned int charge = SCALES.getBattery(1);
	charge = map(charge,MIN_CHG,MAX_CHG,0,100);
	/*if (charge <= MIN_CHG ){
		charge = MIN_CHG;
	}else if(charge > MAX_CHG){
		charge = MAX_CHG;
	}
	charge = ((charge - MIN_CHG) / DIAPAZONE) * 100;*/		
	SCALES.setCharge(charge);
}

void powerSwitchInterrupt(){
	unsigned int t=0;
	delay(100);
	if(digitalRead(PWR_SW)==HIGH){ // 
		digitalWrite(LED, HIGH);
		while(digitalRead(PWR_SW)==HIGH){ // 
			delay(100);
			t++;			
			if(t >40){ // 
				digitalWrite(LED, HIGH);
				while(digitalRead(PWR_SW) == HIGH){delay(10);};// 
				powerOff();
				//ESP.reset();				
				break;
			}
			digitalWrite(LED, !digitalRead(LED));
		}
	}
}

void connectWifi() {
	#if defined SERIAL_DEDUG
		Serial.println("Connecting as wifi client...");
	#endif

	WiFi.disconnect();
	/*!  */
	int n = WiFi.scanNetworks();
	if (n == 0)
		return;
	else{		
		for (int i = 0; i < n; ++i)	{
			/*!  */
			if(WiFi.SSID(i) == SCALES.getSSID().c_str()){ 
				WiFi.begin ( SCALES.getSSID().c_str(), SCALES.getPASS().c_str());
				int connRes = WiFi.waitForConnectResult();
				#if defined SERIAL_DEDUG
					Serial.print ( "connRes: " );
					Serial.println ( connRes );
				#endif
				break;
			}
		}
	}		
}

void loop() {  
  
	taskController.run();
  
	if (connect) {
		#if defined SERIAL_DEDUG
			Serial.println ( "Connect requested" );
		#endif
		connect = false;
		connectWifi();
		lastConnectTry = millis();
	}
	{
		int s = WiFi.status();
		if (s == 0 && millis() > (lastConnectTry + 60000) ) {      
			connect = true;
		}
		if (status != s) { 
			#if defined SERIAL_DEDUG
				Serial.print ( "Status: " );
				Serial.println ( s );
			#endif
			status = s;
			if (s == WL_CONNECTED) {        
				#if defined SERIAL_DEDUG
					Serial.println ( "" );
					Serial.print ( "Connected to " );
					Serial.println ( SCALES.getSSID() );
					Serial.print ( "IP address: " );
					Serial.println ( WiFi.localIP() );
				#endif
				// Setup MDNS responder
				if (!MDNS.begin(myHostname)) {
					#if defined SERIAL_DEDUG
						Serial.println("Error setting up MDNS responder!");
					#endif
				} else {
					#if defined SERIAL_DEDUG
						Serial.println("mDNS responder started");
					#endif
					// Add service to MDNS-SD
					MDNS.addService("http", "tcp", 80);
				}
				COUNT_FLASH = 50;
				COUNT_BLINK = 3000;
				SCALES.saveEvent("net", "WLAN");
				//SCALES.saveEvent("ip", SCALES.getIp());
			} else if (s == WL_NO_SSID_AVAIL) {
				WiFi.disconnect();
			}
		}
	}
  
	dnsServer.processNextRequest();
	browserServer.handleClient();

}






