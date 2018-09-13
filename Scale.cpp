﻿#include <FS.h>
#include <ArduinoJson.h>
#include "web_server_config.h"
#include "Scale.h"

ScaleClass Scale(16,14);		/*  gpio16 gpio0  */

ScaleClass::ScaleClass(byte dout, byte pd_sck) : HX711(dout, pd_sck) /*, ExponentialFilter<long>()*/{
	_server = NULL;	
	_authenticated = false;	
	saveWeight.isSave = false;
	saveWeight.value = 0.0;
}

ScaleClass::~ScaleClass(){}
	
void ScaleClass::setup(BrowserServerClass *server){
	init();
	_server = server;	
	_server->on("/wt",HTTP_GET, [this](AsyncWebServerRequest * request){						/* Получить вес и заряд. */
		AsyncResponseStream *response = request->beginResponseStream("text/json");
		DynamicJsonBuffer jsonBuffer;
		JsonObject &json = jsonBuffer.createObject();
		Scale.doData(json);
		json.printTo(*response);
		#if POWER_PLAN
			POWER.updateCache();
		#endif
		request->send(response);
		//request->send(200, "text/html", String("{\"w\":\""+String(getBuffer())+"\",\"c\":"+String(BATTERY.getCharge())+",\"s\":"+String(getStableWeight())+"}"));
	});
	_server->on(PAGE_FILE, [this](AsyncWebServerRequest * request) {							/* Открыть страницу калибровки.*/
		if(!request->authenticate(_scales_value->user, _scales_value->password))
			if (!_server->checkAdminAuth(request)){
				return request->requestAuthentication();
			}
		_authenticated = true;
		saveValueCalibratedHttp(request);
	});
	_server->on("/av", [this](AsyncWebServerRequest * request){									/* Получить значение АЦП усредненное. */
		request->send(200, TEXT_HTML, String(readAverage()));
	});
	_server->on("/tp", [this](AsyncWebServerRequest * request){									/* Установить тару. */
		tare();
		request->send(204, TEXT_HTML, "");
	});
	_server->on("/sl", handleSeal);																/* Опломбировать */
	_server->on("/cdate.json",HTTP_ANY, [this](AsyncWebServerRequest * request){									/* Получить значение АЦП усредненное. */
		if(!request->authenticate(_scales_value->user, _scales_value->password))
			if (!browserServer.checkAdminAuth(request)){
				return request->requestAuthentication();
			}
		AsyncResponseStream *response = request->beginResponseStream("application/json");
		DynamicJsonBuffer jsonBuffer;
		JsonObject &json = jsonBuffer.createObject();
		
		json[STEP_JSON] = _scales_value->step;
		json[AVERAGE_JSON] = _scales_value->average;
		json[WEIGHT_MAX_JSON] = _scales_value->max;
		json[OFFSET_JSON] = _scales_value->offset;
		json[ACCURACY_JSON] = _scales_value->accuracy;
		json[SCALE_JSON] = _scales_value->scale;
		json[FILTER_JSON] = GetFilterWeight();
		json[SEAL_JSON] = _scales_value->seal;
		json[USER_JSON] = _scales_value->user;
		json[PASS_JSON] = _scales_value->password;
		
		json.printTo(*response);
		request->send(response);	
	});	
}

void ScaleClass::init(){
	reset();
	_scales_value = &CoreMemory.eeprom.scales_value;
	//_downloadValue();
	SetFilterWeight(_scales_value->filter);
	mathRound();
	tare();
	SetCurrent(readAverage());
}

void ScaleClass::mathRound(){
	_round = pow(10.0, _scales_value->accuracy) / _scales_value->step; // множитель для округления}
	_stable_step = 1/_round;
}

void ScaleClass::saveValueCalibratedHttp(AsyncWebServerRequest * request) {
	if (request->args() > 0) {
		if (request->hasArg("update")){
			_scales_value->accuracy = request->arg("weightAccuracy").toInt();
			_scales_value->step = request->arg("weightStep").toInt();
			setAverage(request->arg("weightAverage").toInt());
			SetFilterWeight(request->arg("weightFilter").toInt());
			_scales_value->filter = GetFilterWeight();
			_scales_value->max = request->arg("weightMax").toInt();
			mathRound();
			if (CoreMemory.save()){
				goto ok;
			}
			goto err;
		}
		
		if (request->hasArg("zero")){
			_scales_value->offset = readAverage();
		}
		
		if (request->hasArg("weightCal")){
			float rw = request->arg("weightCal").toFloat();			
			long cw = readAverage();
			mathScale(rw,cw);
		}
		
		if (request->hasArg("user")){
			request->arg("user").toCharArray(_scales_value->user,request->arg("user").length()+1);
			request->arg("pass").toCharArray(_scales_value->password,request->arg("pass").length()+1);
			if (CoreMemory.save()){
				goto url;
			}
			goto err;
		}
		
		ok:
			return request->send(200, TEXT_HTML, "");
		err:
			return request->send(400, TEXT_HTML, "Ошибка ");	
	}
	url:
	#ifdef HTML_PROGMEM
		request->send_P(200,F(TEXT_HTML), calibr_html);
	#else
		request->send(SPIFFS, request->url());
	#endif
}

void ScaleClass::fetchWeight(){
	float w = getWeight();
	formatValue(w,_buffer);
	detectStable(w);
}

/*
bool ScaleClass::_downloadValue(){
	_scales_value->average = 1;
	_scales_value->step = 1;
	_scales_value->accuracy = 0;
	_scales_value->offset = 0;
	_scales_value->max = 1000;
	_scales_value->scale = 1;
	SetFilterWeight(80);
	_scales_value->user = "admin";
	_scales_value->password = "1234";
	File dateFile = SPIFFS.open(CDATE_FILE, "r");
	/ *if (SPIFFS.exists(CDATE_FILE)){
		dateFile = SPIFFS.open(CDATE_FILE, "r");
	}else{
		dateFile = SPIFFS.open(CDATE_FILE, "w+");
	}* /
	if (!dateFile) {
		//dateFile.close();
		return false;
	}
	size_t size = dateFile.size();
		
	std::unique_ptr<char[]> buf(new char[size]);
		
	dateFile.readBytes(buf.get(), size);
	dateFile.close();
	DynamicJsonBuffer jsonBuffer(size);
	JsonObject& json = jsonBuffer.parseObject(buf.get());

	if (!json.success()) {
		return false;
	}
	_scales_value->max = json[WEIGHT_MAX_JSON];
	_scales_value->offset = json[OFFSET_JSON];
	setAverage(json[AVERAGE_JSON]);
	_scales_value->step = json[STEP_JSON];
	_scales_value->accuracy = json[ACCURACY_JSON];
	_scales_value->scale = json[SCALE_JSON];
	SetFilterWeight(json[FILTER_JSON]);
	_scales_value->seal = json[SEAL_JSON];
	if (!json.containsKey(USER_JSON)){
		_scales_value->user = "admin";
		_scales_value->password = "1234";	
	}else{
		_scales_value->user = json[USER_JSON].as<String>();
		_scales_value->password = json[PASS_JSON].as<String>();
	}
	
	return true;
	
}*/

/*
bool ScaleClass::saveDate() {	
	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	
	json[STEP_JSON] = _scales_value->step;
	json[AVERAGE_JSON] = _scales_value->average;
	json[WEIGHT_MAX_JSON] = _scales_value->max;
	json[OFFSET_JSON] = _scales_value->offset;
	json[ACCURACY_JSON] = _scales_value->accuracy;
	json[SCALE_JSON] = _scales_value->scale;
	json[FILTER_JSON] = GetFilterWeight();
	json[SEAL_JSON] = _scales_value->seal;
	json[USER_JSON] = _scales_value->user;
	json[PASS_JSON] = _scales_value->password;
	
	File cdateFile = SPIFFS.open(CDATE_FILE, "w");
	if (!cdateFile) {
		return false;
	}
	
	json.printTo(cdateFile);
	cdateFile.flush();
	cdateFile.close();
	return true;
}*/

long ScaleClass::readAverage() {
	long long sum = 0;
	for (byte i = 0; i < _scales_value->average; i++) {
		sum += read();
	}
	return _scales_value->average == 0?sum / 1:sum / _scales_value->average;
}

long ScaleClass::getValue() {
	Filter(readAverage());
	return Current() - _scales_value->offset;
}

float ScaleClass::getUnits() {
	float v = getValue();
	return (v * _scales_value->scale);
}

float ScaleClass::getWeight(){
	return round(getUnits() * _round) / _round; 
}

void ScaleClass::tare() {
	long sum = readAverage();
	setOffset(sum);
}

void ScaleClass::setAverage(unsigned char a){
	_scales_value->average = constrain(a, 1, 5);
}

void ScaleClass::mathScale(float referenceW, long calibrateW){
	_scales_value->scale = referenceW / float(calibrateW - _scales_value->offset);
}

/*! Функция для форматирования значения веса
	value - Форматируемое значение
	digits - Количество знаков после запятой
	accuracy - Точновть шага значений (1, 2, 5, ...)
	string - Выходная строка отфармотронова значение 
*/
void ScaleClass::formatValue(float value, char* string){
	dtostrf(value, 6-_scales_value->accuracy, _scales_value->accuracy, string);
}

/* */
void ScaleClass::detectStable(float w){
	static unsigned char stable_num;
	if (saveWeight.value == w) {
		if (stable_num > STABLE_NUM_MAX) {
			if (!stableWeight){
				if (millis() > saveWeight.time ){
					if(fabs(w) > _stable_step){
						saveWeight.isSave = true;
						saveWeight.time = millis() + 10000;
					}
				}
				stableWeight = true;
			}				
			return;
		}
		stable_num++;
	} else {
		stable_num = 0;
		stableWeight = false;
		saveWeight.value = w;
	}
}

/*
double calculateDistance(double rssi) {
	
	double txPower = -41;   //hard coded power value. Usually ranges between -59 to -65
	
	/ *if(rssi == 0) {
		return -1.0;
	}

	double ratio = rssi * 1.0 / txPower;
	if (ratio < 1.0) {
		return pow(ratio, 10);
	}
	else {
		double distance =  (0.89976)*pow(ratio, 7.7095) + 0.111;
		return distance;
	}* /
	return pow(10, ((double)txPower - rssi) / (10 * 2));
}*/

size_t ScaleClass::doData(JsonObject& json ){
	json["w"]= String(_buffer);
	json["c"]= BATTERY.getCharge();
	json["s"]= stableWeight;	
	return json.measureLength();
}

void handleSeal(AsyncWebServerRequest * request){
	randomSeed(Scale.readAverage());
	Scale.setSeal(random(1000));
	
	if (CoreMemory.save()){
		request->send(200, TEXT_HTML, String(Scale.getSeal()));
	}
}





