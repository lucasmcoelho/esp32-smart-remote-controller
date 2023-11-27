#define WIFI_SSID "NB-DESENV7993"
#define WIFI_PSK "775z5!9R"

// #define WIFI_SSID "INHD - LINK 600"
// #define WIFI_PSK "In306701"

// #define WIFI_SSID "JLX-Bimora"
// #define WIFI_PSK "kaulu123"

// #define WIFI_SSID "GVT-DB19"
// #define WIFI_PSK "7607401559"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <RCSwitch.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <string>

#include <httpsCert.h>
#include <pkServer.h>

decode_results results;

typedef struct {
    decode_results results;
    uint8_t irState[53];
    uint64_t data;
    decode_type_t protocol;
    uint16_t bitLength;
    bool iniciado = true;
    bool finalizado = true;
} irStruct;

typedef struct {
    uint32_t data;
    uint32_t bitLength;
    uint32_t protocol;
    bool iniciado = true;
    bool finalizado = true;
} rfStruct;

using namespace httpsserver;

const uint16_t kIrLedPin = 4;  // Pino D4

const uint16_t kCaptureBufferSize = 1024;  // 1024 == ~511 bits // Considerar buffer maior para longas mensagend IR
const uint8_t kTimeout = 50;               // Tempo após o ultimo bit recebido em ms
const uint16_t kFrequency = 38000;         // Frequencia default

#ifdef ARDUINO_ESP32C3_DEV
const uint16_t kRecvPin = 10;  // 14 on a ESP32-C3 causes a boot loop.
#else
const uint16_t kRecvPin = 14;
#endif

IRsend irsend(kIrLedPin);
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, false);
// IRrecv irrecv(kRecvPin);

irStruct irReceiverData;
irStruct irEmitterData;

rfStruct rfReceiverData;
rfStruct rfEmitterData;

SSLCert* cert;
HTTPSServer* secureServer;

RCSwitch mySwitch = RCSwitch();

bool acaoIR = true;
bool acaoIRFinalizada = true;

bool acaoRF = true;
bool acaoRFFinalizada = true;

void handleRoot(HTTPRequest* req, HTTPResponse* res);
void handle404(HTTPRequest* req, HTTPResponse* res);

void iREmit(HTTPRequest* req, HTTPResponse* res);
void iRSave(HTTPRequest* req, HTTPResponse* res);
void iRSaveComplete(HTTPRequest* req, HTTPResponse* res);

void rFEmit(HTTPRequest* req, HTTPResponse* res);
void rFSave(HTTPRequest* req, HTTPResponse* res);
void rFSaveComplete(HTTPRequest* req, HTTPResponse* res);

void serverTask(void* params);

void setup() {
    Serial.begin(115200);
    delay(1000);

    // TODO - CRIAR SELF SIGNED CERTIFICATE
    Serial.println("Criando Self-Signed Certificate");
    cert = new SSLCert(server_cert, KEYSIZE_2048, server_key, KEYSIZE_2048);

    // // SELF SIGNED CERTIFICATE  ----TEMPORARIO----
    // int createCertResult = createSelfSignedCert(
    //     *cert,
    //     KEYSIZE_2048,
    //     "CN=esp32.local,O=FancyCompany,C=DE",
    //     "20230101000000",  // YYYYMMDDhhmmss
    //     "20250101000000"   // YYYYMMDDhhmmss
    // );

    // if (createCertResult != 0) {
    //     Serial.printf("Erro ao criar certificado. Error Code = 0x%02X, check SSLCert.hpp for details", createCertResult);
    //     while (true) delay(500);
    // }
    // Serial.println("Certificado criado");
    // FIM SELF SIGNED CERTIFICATE


    Serial.println("Setting up WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PSK);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.print("Connected. IP=");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("esp32")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    MDNS.addService("https", "tcp", 443);

    // serverTask - the function that should be run as separate task
    // "https443" - a name for the task (mainly used for logging)
    // 6144       - stack size in byte. If you want up to four clients, you sshould
    //              not go below 6kB. If your stack is too small, you will encounter
    //              Panic and stack canary exceptions, usually during the call to
    //              SSL_accept.
    xTaskCreatePinnedToCore(serverTask, "https443", 6144 * 2, NULL, 1, NULL, ARDUINO_RUNNING_CORE);

    irrecv.enableIRIn();          // Start the IR receiver
    irsend.begin();               // Start up the IR sender.
    mySwitch.enableReceive(27);   // Start RF receiver
    mySwitch.enableTransmit(18);  // Start RF transmitter
}

void loop() {
    // IR SAVE DATA
    if (!irReceiverData.iniciado && !irReceiverData.finalizado) {

        if (irrecv.decode(&results)) {
            //irReceiverData.irState = results.state;
            irReceiverData.data = results.value;
            irReceiverData.protocol = results.decode_type;
            irReceiverData.bitLength = results.bits;
            uint8_t* _irState = results.state;
            
            // print() & println() can't handle printing long longs. (uint64_t)
            serialPrintUint64(results.value, HEX);
            irEmitterData.data = results.value;
            irEmitterData.protocol = results.decode_type;
            irEmitterData.bitLength = results.bits;

            

        // for(int is=0; is < 53; is++){
        //     Serial.print("HEX : ");
        //     serialPrintUint64(results.state[is], HEX);

        //     Serial.println(results.state[is]);

        // }

            Serial.println("_________________________");
            irrecv.resume();

            irReceiverData.iniciado = true;
            irReceiverData.finalizado = true;
        }

    }

    if (!irEmitterData.iniciado && irReceiverData.finalizado) {
        if (irEmitterData.protocol == decode_type_t::UNKNOWN) {  // Yes.
//             // Convert the results into an array suitable for sendRaw().
//             // resultToRawArray() allocates the memory we need for the array.
//             uint16_t* raw_array = resultToRawArray(&irEmitterData.results);
//             // Find out how many elements are in the array.
//             irEmitterData.bitLength = getCorrectedRawLength(&irEmitterData.results);
// #if SEND_RAW

//             irsend.sendRaw(raw_array, irEmitterData.bitLength, kFrequency);
// #endif  // SEND_RAW
//             delete[] raw_array;
        } else if (hasACState(irEmitterData.protocol)) {
            irsend.send(irEmitterData.protocol, irEmitterData.irState, irEmitterData.bitLength / 8);
        } else {
            Serial.println(irEmitterData.results.value);
            Serial.println(results.value);
            irsend.send(irEmitterData.results.decode_type, irEmitterData.results.value, irEmitterData.results.bits);
        }

        irEmitterData.iniciado = true;
        irEmitterData.finalizado = true;

    }

    if (!rfReceiverData.iniciado && !rfReceiverData.finalizado) {
        if (mySwitch.available()) {
            Serial.print("Received ");
            Serial.print(mySwitch.getReceivedValue());
            Serial.print(" / ");
            Serial.print(mySwitch.getReceivedBitlength());
            Serial.print("bit ");
            Serial.print("Protocol: ");
            Serial.println(mySwitch.getReceivedProtocol());

            rfReceiverData.data = mySwitch.getReceivedValue();
            rfReceiverData.bitLength = mySwitch.getReceivedBitlength();
            rfReceiverData.protocol = mySwitch.getReceivedProtocol();
            rfReceiverData.iniciado = true;
            rfReceiverData.finalizado = true;

            mySwitch.resetAvailable();
        }
        Serial.print("_");
    }

    if (!rfEmitterData.iniciado && rfReceiverData.finalizado) {
        mySwitch.setProtocol(rfEmitterData.protocol);  // Default 1
        // mySwitch.setPulseLength(320);
        // mySwitch.setRepeatTransmit(15);

        mySwitch.send(rfEmitterData.data, rfEmitterData.bitLength);
        rfEmitterData.finalizado = true;
        rfEmitterData.iniciado = true;
    }

    delay(1);
}

void serverTask(void* params) {
    secureServer = new HTTPSServer(cert);

    ResourceNode* nodeRoot = new ResourceNode("/", "GET", &handleRoot);
    ResourceNode* node404 = new ResourceNode("", "GET", &handle404);

    ResourceNode* nodeIREmit = new ResourceNode("/ir-emitter", "POST", &iREmit);
    ResourceNode* nodeIRSave = new ResourceNode("/ir-save", "POST", &iRSave);
    ResourceNode* nodeIRSaveComplete = new ResourceNode("/ir-save", "GET", &iRSaveComplete);

    ResourceNode* nodeRFEmit = new ResourceNode("/rf-emitter", "POST", &rFEmit);
    ResourceNode* nodeRFSave = new ResourceNode("/rf-save", "POST", &rFSave);
    ResourceNode* nodeRFSaveComplete = new ResourceNode("/rf-save", "GET", &rFSaveComplete);

    secureServer->registerNode(nodeRoot);
    secureServer->setDefaultNode(node404);

    secureServer->registerNode(nodeIREmit);
    secureServer->registerNode(nodeIRSave);
    secureServer->registerNode(nodeIRSaveComplete);

    secureServer->registerNode(nodeRFEmit);
    secureServer->registerNode(nodeRFSave);
    secureServer->registerNode(nodeRFSaveComplete);

    Serial.println("Starting server...");
    secureServer->start();

    if (secureServer->isRunning()) {
        Serial.println("Server ready.");
        while (true) {
            secureServer->loop();
            delay(1);
        }
    }
}

void handleRoot(HTTPRequest* req, HTTPResponse* res) {
    res->setHeader("Content-Type", "text/html");

    res->println("<!DOCTYPE html>");
    res->println("<html>");
    res->println("<head><title>IR/RF Universal Controller</title></head>");
    res->println("<body>");
    res->println("<h1>Web Server Online!</h1>");
    res->print("<p>Server Uptime: ");
    res->print((int)(millis() / 1000), DEC);
    res->println(" seconds.</p>");
    res->println("</body>");
    res->println("</html>");
}

void handle404(HTTPRequest* req, HTTPResponse* res) {
    req->discardRequestBody();

    res->setStatusCode(404);
    res->setStatusText("Not Found");

    res->setHeader("Content-Type", "text/html");

    res->println("<!DOCTYPE html>");
    res->println("<html>");
    res->println("<head><title>IR/RF Universal Controller - Page Not Found</title></head>");
    res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
    res->println("</html>");
}

void iREmit(HTTPRequest* req, HTTPResponse* res) {
    if (irEmitterData.finalizado) {
        irEmitterData.iniciado = false;
        irEmitterData.finalizado = false;
    }

    std::string contentType = req->getHeader("Content-Type");
    DynamicJsonDocument ddoc(1024);

    size_t contentLength = req->getContentLength();
    char bodyBytes[contentLength+1];
    if (contentType == "application/json") {    
        req->readChars(bodyBytes, contentLength);
        bodyBytes[contentLength] = '\0';
        deserializeJson(ddoc, bodyBytes);


        // irEmitterData.data = ddoc["data"];
        // irEmitterData.bitLength = ddoc["bitLength"];
        // irEmitterData.protocol = ddoc["protocol"];

        //serializeJsonPretty(doc, Serial);

        // String rr = String((char*)bodyBytes);

        // Serial.print(bodyBytes);


        // for(int dd=0; dd<contentLength; dd++) {
        //     Serial.print(uint64ToString(bodyBytes[dd], HEX));
        // }
    }

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["finalizado"] = false;

    String resultString;
    serializeJson(doc, resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void iRSave(HTTPRequest* req, HTTPResponse* res) {
    if (irReceiverData.finalizado) irReceiverData.iniciado = false;
    irReceiverData.finalizado = false;

    const char* c = req->getRequestString().data();
    Serial.print("BODY REQUEST: ");
    Serial.println(c);

    StaticJsonDocument<200> doc;

    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["data"] = "";
    object["finalizado"] = "";

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void iRSaveComplete(HTTPRequest* req, HTTPResponse* res) {
    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";


    if(irReceiverData.finalizado){
        object["data"] = irReceiverData.data;
        object["bitLength"] = rfReceiverData.bitLength;
        object["protocol"] = rfReceiverData.protocol;
    } else{
        object["data"] = 0;
        object["bitLength"] = 0;
        object["protocol"] = 0;
    }
    // object["data"] = uint64ToString(results.value, HEX);

    object["finalizado"] = irReceiverData.finalizado;

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void rFSave(HTTPRequest* req, HTTPResponse* res) {
    if (rfReceiverData.finalizado) rfReceiverData.iniciado = false;
    rfReceiverData.finalizado = false;

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "RF";
    object["data"] = -1;
    object["bitLenght"] = -1;   
    object["protocol"] = -1;
    object["finalizado"] = rfReceiverData.finalizado;

    serializeJson(doc, Serial);

    String resultString;
    serializeJson(doc, resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void rFSaveComplete(HTTPRequest* req, HTTPResponse* res) {
    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "RF";

    if (rfReceiverData.finalizado) {
        object["data"] = rfReceiverData.data;
        object["bitLength"] = rfReceiverData.bitLength;
        object["protocol"] = rfReceiverData.protocol;

    } else {
        object["data"] = 0;
        object["bitLength"] = 0;
        object["protocol"] = 0;
    }

    object["finalizado"] = rfReceiverData.finalizado;

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void rFEmit(HTTPRequest* req, HTTPResponse* res) {
    if (rfEmitterData.finalizado) {
        rfEmitterData.iniciado = false;
        rfEmitterData.finalizado = false;
    }

    

    std::string contentType = req->getHeader("Content-Type");
    DynamicJsonDocument ddoc(1024);

    size_t contentLength = req->getContentLength();
    char bodyBytes[contentLength+1];
    if (contentType == "application/json") {    
        req->readChars(bodyBytes, contentLength);
        bodyBytes[contentLength] = '\0';

        DeserializationError error = deserializeJson(ddoc, bodyBytes);
        if(!error){
            rfEmitterData.data = ddoc["data"];
            rfEmitterData.bitLength = ddoc["bitLength"];
            rfEmitterData.protocol = ddoc["protocol"];
        }
    }

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "RF";
    object["data"] = rfEmitterData.data;
    object["finalizado"] = true;

    String resultString;
    serializeJson(doc, resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}