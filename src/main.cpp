#define WIFI_SSID "NB-DESENV7993"
#define WIFI_PSK "775z5!9R"

// #define WIFI_SSID "INHD - LINK 600"
// #define WIFI_PSK "In306701"

// #define WIFI_SSID "JLX-Bimora"
// #define WIFI_PSK "kaulu123"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <RCSwitch.h>
#include <WiFi.h>

#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <string>

typedef struct {
    decode_results results;
    decode_type_t protocol;
    uint16_t irSize;
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

const uint16_t kIrLedPin = 4;  // Pino D2

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

void serverTask(void* params);

void setup() {
    Serial.begin(115200);
    delay(1000);

    // TODO - CRIAR SELF SIGNED CERTIFICATE
    Serial.println("Criando Self-Signed Certificate");
    cert = new SSLCert();

    // SELF SIGNED CERTIFICATE  ----TEMPORARIO----
    int createCertResult = createSelfSignedCert(
        *cert,
        KEYSIZE_2048,
        "CN=myesp32.local,O=FancyCompany,C=DE",
        "20230101000000",  // YYYYMMDDhhmmss
        "20300101000000"   // YYYYMMDDhhmmss
    );

    if (createCertResult != 0) {
        Serial.printf("Erro ao criar certificado. Error Code = 0x%02X, check SSLCert.hpp for details", createCertResult);
        while (true) delay(500);
    }
    Serial.println("Certificado criado");
    // FIM SELF SIGNED CERTIFICATE

    // secureServer = new HTTPSServer(cert);

    Serial.println("Setting up WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PSK);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.print("Connected. IP=");
    Serial.println(WiFi.localIP());

    // We pass:
    // serverTask - the function that should be run as separate task
    // "https443" - a name for the task (mainly used for logging)
    // 6144       - stack size in byte. If you want up to four clients, you should
    //              not go below 6kB. If your stack is too small, you will encounter
    //              Panic and stack canary exceptions, usually during the call to
    //              SSL_accept.
    xTaskCreatePinnedToCore(serverTask, "https443", 6144, NULL, 1, NULL, ARDUINO_RUNNING_CORE);

    irrecv.enableIRIn();  // Start the IR receiver
    irsend.begin();       // Start up the IR sender.
    mySwitch.enableReceive(18);  // Start RF receiver
}

void loop() {
    // Inicia Web Server
    // secureServer->loop();

    // IR SAVE DATA
    if (!irReceiverData.iniciado) {
        irReceiverData.iniciado = true;

        if (irrecv.decode(&irReceiverData.results)) {
            irReceiverData.protocol = irReceiverData.results.decode_type;
            irReceiverData.irSize = irReceiverData.results.bits;
            uint8_t* _irState = irReceiverData.results.state;
            bool success = true;
            // print() & println() can't handle printing long longs. (uint64_t)
            serialPrintUint64(irReceiverData.results.value, HEX);
            Serial.println("");
            irrecv.resume();
        }

        irReceiverData.finalizado = true;
        Serial.println("Acao Finalizada: " + String(acaoIRFinalizada));
    }

    if (!irEmitterData.iniciado && irReceiverData.finalizado) {
        if (irEmitterData.protocol == decode_type_t::UNKNOWN) {  // Yes.
            // Convert the results into an array suitable for sendRaw().
            // resultToRawArray() allocates the memory we need for the array.
            uint16_t* raw_array = resultToRawArray(&irEmitterData.results);
            // Find out how many elements are in the array.
            irEmitterData.irSize = getCorrectedRawLength(&irEmitterData.results);
#if SEND_RAW

            irsend.sendRaw(raw_array, irEmitterData.irSize, kFrequency);
#endif  // SEND_RAW
            delete[] raw_array;
        } else if (hasACState(irEmitterData.protocol)) {
            irsend.send(irEmitterData.protocol, irEmitterData.results.state, irEmitterData.irSize / 8);
        } else {
            irsend.send(irEmitterData.protocol, irEmitterData.results.value, irEmitterData.irSize);
        }

        irEmitterData.finalizado = true;
    }


    //TODO CONTROLE RF
    if (!irReceiverData.iniciado) {
        if (mySwitch.available()) {
            
            Serial.print("Received ");
            Serial.print( mySwitch.getReceivedValue() );
            Serial.print(" / ");
            Serial.print( mySwitch.getReceivedBitlength() );
            Serial.print("bit ");
            Serial.print("Protocol: ");
            Serial.println( mySwitch.getReceivedProtocol() );

            rfReceiverData.data = mySwitch.getReceivedValue();
            rfReceiverData.bitLength = mySwitch.getReceivedBitlength();
            rfReceiverData.protocol = mySwitch.getReceivedProtocol();

            mySwitch.resetAvailable();
        }
    }

    if (!irEmitterData.iniciado && irReceiverData.finalizado) {

    }


    delay(1);
}

void serverTask(void* params) {
    // In the separate task we first do everything that we would have done in the
    // setup() function, if we would run the server synchronously.

    // Note: The second task has its own stack, so you need to think about where
    // you create the server's resources and how to make sure that the server
    // can access everything it needs to access. Also make sure that concurrent
    // access is no problem in your sketch or implement countermeasures like locks
    // or mutexes.

    secureServer = new HTTPSServer(cert);

    // Create nodes
    ResourceNode* nodeRoot = new ResourceNode("/", "GET", &handleRoot);
    ResourceNode* node404 = new ResourceNode("", "GET", &handle404);

    ResourceNode* nodeIREmit = new ResourceNode("/ir-emitter", "POST", &iREmit);
    ResourceNode* nodeIRSave = new ResourceNode("/ir-save", "POST", &iRSave);
    ResourceNode* nodeIRSaveComplete = new ResourceNode("/ir-save", "GET", &iRSaveComplete);

    ResourceNode* nodeRFEmit = new ResourceNode("/rf-emitter", "GET", &iRSave);
    ResourceNode* nodeRFSave = new ResourceNode("/rf-save", "POST", &iRSave);
    ResourceNode* nodeRFSaveComplete = new ResourceNode("/rf-save", "GET", &iRSave);

    // Add nodes to the server
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

        // "loop()" function of the separate task
        while (true) {
            // This call will let the server do its work
            secureServer->loop();

            // Other code would go here...
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
    // Discard request body, if we received any
    // We do this, as this is the default node and may also server POST/PUT requests
    req->discardRequestBody();

    // Set the response status
    res->setStatusCode(404);
    res->setStatusText("Not Found");

    // Set content type of the response
    res->setHeader("Content-Type", "text/html");

    // Write a tiny HTTP page
    res->println("<!DOCTYPE html>");
    res->println("<html>");
    res->println("<head><title>IR/RF Universal Controller - Page Not Found</title></head>");
    res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
    res->println("</html>");
}

void iREmit(HTTPRequest* req, HTTPResponse* res) {
    const char* c = req->getRequestString().data();
    Serial.print("BODY REQUEST: ");
    Serial.println(c);

    // ResourceParameters * params = req->getParams();

    std::string contentType = req->getHeader("Content-Type");
    DynamicJsonDocument ddoc(1024);

    char bodyBytes[req->getContentLength()];
    if (contentType == "application/json") {
        req->readChars(bodyBytes, req->getContentLength());
        // deserializeJson(ddoc, bodyBytes);
        // serializeJsonPretty(doc, Serial);

        String rr = String((char*)bodyBytes);
        Serial.println(rr);
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
    if (acaoIRFinalizada == 1) acaoIRFinalizada = 0;

    const char* c = req->getRequestString().data();
    Serial.print("BODY REQUEST: ");
    Serial.println(c);

    StaticJsonDocument<200> doc;

    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["data"] = uint64ToString(irReceiverData.results.value, HEX);
    object["finalizado"] = irReceiverData.finalizado;

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void rFSave(HTTPRequest* req, HTTPResponse* res){

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["data"] = uint64ToString(irReceiverData.results.value, HEX);
    object["bitLenght"] = "";
    object["protocol"] = "";
    object["finalizado"] = irReceiverData.finalizado;


    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);


}

void rFSaveComplete(HTTPRequest* req, HTTPResponse* res){

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["data"] = uint64ToString(irReceiverData.results.value, HEX);
    object["finalizado"] = irReceiverData.finalizado;

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}

void rFEmitter(HTTPRequest* req, HTTPResponse* res){

    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["tipo"] = "IR";
    object["data"] = uint64ToString(irReceiverData.results.value, HEX);
    object["finalizado"] = irReceiverData.finalizado;

    String resultString;
    serializeJson(doc, resultString);
    Serial.print(resultString);

    res->setHeader("Content-Type", "application/json");
    res->println(resultString);
}